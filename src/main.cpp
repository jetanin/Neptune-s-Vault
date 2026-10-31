/*
  Safe Challenge - ESP32 (Arduino framework) + Access Point
  ==========================================================
  Staff manage the game through a web page hosted directly on the ESP32:
    - The ESP32 broadcasts its own WiFi access point; no internet is required.
    - Staff connect a phone or laptop to this WiFi and open the ESP32 IP in a browser.
      (The default access point IP is 192.168.4.1)
    - Web page: enter a group (1-20), start the round, view live status (LOCKED, playing,
      incorrect attempts, elapsed time), or reset to SETUP.

  Hardware (same as the original Arduino version):
    - 5x on/off switches    -> Players enter the code (bit0..bit4).
                                Also used to select a group without the web interface.
    - 1x push switch        -> Confirm (short press = confirm, hold >=2 seconds = reset)
    - 1x red LED, 1x green LED
    - 1x buzzer (passive recommended; LEDC generates tones at different frequencies)
    - 1x servo

  Why use the Arduino framework instead of ESP-IDF:
    This project needs WebServer, Servo, and basic digitalRead/millis functions. The Arduino core
    for ESP32 provides WebServer.h and ESP32Servo for concise, readable code.
    Most of the original Nano/Uno code can be reused. ESP-IDF requires an HTTP server
    (esp_http_server) and manual PWM/servo control through low-level APIs, adding complexity
    without benefits for a project of this size, so the Arduino framework is recommended.

  Wiring (avoid ESP32 strapping pins GPIO0,2,5,12,15 and GPIO34-39,
  which are input-only and lack internal pull-ups for switches):
    SW1(bit0) -> GPIO13      SW4(bit3) -> GPIO26
    SW2(bit1) -> GPIO14      SW5(bit4) -> GPIO25
    SW3(bit2) -> GPIO27      CONFIRM   -> GPIO33
    RED LED   -> GPIO32      GREEN LED -> GPIO21
    BUZZER    -> GPIO18      SERVO     -> GPIO19
  Connect each switch/button between its assigned GPIO and GND. The code uses INPUT_PULLUP,
  so no external pull-up resistors are needed. Pressed/ON reads LOW.

  Build/upload with PlatformIO:
    pio run -t upload
    pio device monitor          (View the access point IP and status logs)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "player_page.h"

// ---------------- Access Point ----------------
const char *AP_SSID = "SafeChallenge";
const char *AP_PASS = "safe12345"; // Must contain at least 8 characters for WPA2

WebServer server(80);

// ---------------- Pin assignments ----------------
const uint8_t SW_PIN[5] = {13, 14, 27, 26, 25}; // bit0..bit4
const uint8_t CONFIRM_PIN = 33;
const uint8_t RED_PIN = 32;
const uint8_t GREEN_PIN = 21;
const uint8_t BUZZER_PIN = 18;
const uint8_t SERVO_PIN = 19;

const uint8_t BUZZER_LEDC_CHANNEL = 2; // LEDC channel for the buzzer

enum class BuzzerType
{
  PASSIVE,
  ACTIVE
};
const BuzzerType BUZZER_TYPE = BuzzerType::ACTIVE; // Select ACTIVE for an active buzzer
const uint8_t ACTIVE_BUZZER_ON_LEVEL = HIGH;       // Use LOW for an active-low module

// ---------------- Servo positions ----------------
const uint8_t SERVO_LOCKED = 0;
const uint8_t SERVO_OPEN = 90;
Servo lockServo;
uint8_t currentServoAngle = SERVO_OPEN;

void writeServo(uint8_t angle)
{
  currentServoAngle = angle;
  lockServo.write(angle);
}

// ---------------- LED states ----------------
bool currentRedLed = false;
bool currentGreenLed = false;

void writeRed(bool on)
{
  currentRedLed = on;
  digitalWrite(RED_PIN, on ? HIGH : LOW);
}

void writeGreen(bool on)
{
  currentGreenLed = on;
  digitalWrite(GREEN_PIN, on ? HIGH : LOW);
}

// ---------------- Answer key (from ChallengeKey.csv, one=bit4 ... five=bit0) ----------------
const uint8_t NUM_GROUPS = 20;
const uint8_t ANSWER_KEY[NUM_GROUPS] = {
    /* 1 */ 4,
    /* 2 */ 16,
    /* 3 */ 21,
    /* 4 */ 1,
    /* 5 */ 22,
    /* 6 */ 28,
    /* 7 */ 19,
    /* 8 */ 7,
    /* 9 */ 21,
    /*10 */ 7,
    /*11 */ 4,
    /*12 */ 21,
    /*13 */ 21,
    /*14 */ 25,
    /*15 */ 21,
    /*16 */ 4,
    /*17 */ 8,
    /*18 */ 21,
    /*19 */ 7,
    /*20 */ 28,
};

// ---------------- Game state (maximum 3 incorrect attempts) ----------------
const uint8_t MAX_ATTEMPTS = 3;

enum GameState
{
  SETUP,
  PLAYING,
  SOLVED,
  FAILED
};
GameState state = SETUP;

uint8_t currentGroup = 0;
uint8_t expectedCode = 0;
uint16_t wrongCount = 0;
unsigned long failedElapsedMs = 0;
unsigned long roundStartMs = 0;
unsigned long solvedElapsedMs = 0; // Elapsed time when solved, retained for display after SOLVED

// ---------------- Confirm button debounce/long press ----------------
bool confirmPrevReading = HIGH;
bool confirmStableState = HIGH;
unsigned long confirmDebounceAt = 0;
unsigned long confirmPressedAt = 0;
bool confirmLongFired = false;
const unsigned long DEBOUNCE_MS = 25;
const unsigned long LONG_PRESS_MS = 2000;

// ---------------- Non-blocking LED blinking ----------------
enum BlinkMode
{
  BLINK_OFF,
  BLINK_SETUP_ALT,
  BLINK_ERROR_RED,
  BLINK_WRONG_RED,
  BLINK_SOLVED_GREEN,
  BLINK_FAILED_RED
};
BlinkMode blinkMode = BLINK_SETUP_ALT;
unsigned long blinkLastToggle = 0;
bool blinkPhase = false;
uint8_t blinkPulsesLeft = 0;

// ---------------- Non-blocking audio (no delay() to keep the web server responsive) ----------------
struct Note
{
  uint16_t freq;
  uint16_t durMs;
};
const Note TUNE_CONFIRM[] = {{880, 120}};
const Note TUNE_ERROR[] = {{200, 150}, {0, 80}, {200, 150}};
const Note TUNE_WRONG[] = {{220, 500}};
const Note TUNE_CORRECT[] = {{880, 150}, {1100, 150}, {1320, 150}};

const Note *activeTune = nullptr;
uint8_t tuneLen = 0, tuneIdx = 0;
unsigned long tuneNextAt = 0;

void startTune(const Note *t, uint8_t len)
{
  activeTune = t;
  tuneLen = len;
  tuneIdx = 0;
  tuneNextAt = millis();
}

void setBuzzerTone(uint16_t freq)
{
  if (BUZZER_TYPE == BuzzerType::ACTIVE)
  {
    digitalWrite(BUZZER_PIN, freq > 0 ? ACTIVE_BUZZER_ON_LEVEL
                                      : (ACTIVE_BUZZER_ON_LEVEL == HIGH ? LOW : HIGH));
    return;
  }
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWriteTone(BUZZER_PIN, freq);
#else
  ledcWriteTone(BUZZER_LEDC_CHANNEL, freq);
#endif
}

void updateTune()
{
  if (!activeTune)
    return;
  if (millis() >= tuneNextAt)
  {
    if (tuneIdx < tuneLen)
    {
      uint16_t f = activeTune[tuneIdx].freq;
      setBuzzerTone(f);
      tuneNextAt = millis() + activeTune[tuneIdx].durMs;
      tuneIdx++;
    }
    else
    {
      setBuzzerTone(0);
      activeTune = nullptr;
    }
  }
}

// ---------------- Core game functions (same structure as the original Arduino version) ----------------
void setBlink(BlinkMode mode)
{
  blinkMode = mode;
  blinkLastToggle = millis();
  blinkPhase = false;
  blinkPulsesLeft = (mode == BLINK_WRONG_RED) ? 3 : (mode == BLINK_ERROR_RED ? 4 : 0);
}

void enterSetup()
{
  state = SETUP;
  currentGroup = 0;
  expectedCode = 0;
  wrongCount = 0;
  writeServo(SERVO_OPEN); // Keep the latch unlocked during setup (90 degrees)
  writeRed(false);
  writeGreen(false);
  setBlink(BLINK_SETUP_ALT);
  Serial.println("[SETUP] Set group 1-20 on SW1..SW5 (weights 1,2,4,8,16), then tap CONFIRM GPIO33.");
  Serial.println("[SETUP] All switches OFF = 0 (invalid group). Hold CONFIRM 2 seconds to reset.");
}

void trySetGroup(uint8_t group)
{
  if (group >= 1 && group <= NUM_GROUPS)
  {
    currentGroup = group;
    expectedCode = ANSWER_KEY[group - 1];
    wrongCount = 0;
    roundStartMs = millis();
    state = PLAYING;
    writeServo(SERVO_LOCKED); // Lock the latch when a group starts (0 degrees)
    setBlink(BLINK_OFF);
    writeRed(false);
    writeGreen(false);
    startTune(TUNE_CONFIRM, 1);

    Serial.printf("Starting group %u (answer = %u) -> LOCKED; ready to play (maximum %u attempts)\n",
                  currentGroup, expectedCode, MAX_ATTEMPTS);
  }
  else
  {
    setBlink(BLINK_ERROR_RED);
    startTune(TUNE_ERROR, 3);
    Serial.printf("Invalid group number: %u\n", group);
  }
}

void submitGuess(uint8_t code)
{
  if (state != PLAYING)
    return; // Accept guesses only while PLAYING

  unsigned long elapsed = millis() - roundStartMs;

  if (code == expectedCode)
  {
    state = SOLVED;
    solvedElapsedMs = elapsed;
    setBlink(BLINK_SOLVED_GREEN);
    writeRed(false);
    writeGreen(true);
    writeServo(SERVO_OPEN);
    startTune(TUNE_CORRECT, 3);

    Serial.printf("Correct! Group %u finished in %lu seconds with %u incorrect attempts\n",
                  currentGroup, elapsed / 1000, wrongCount);
  }
  else
  {
    wrongCount++;
    if (wrongCount >= MAX_ATTEMPTS)
    {
      state = FAILED;
      failedElapsedMs = elapsed;
      writeServo(SERVO_LOCKED);
      setBlink(BLINK_FAILED_RED);
      startTune(TUNE_ERROR, 3);
      Serial.printf("Reached %u incorrect attempts! Group %u FAILED; last submitted code = %u\n",
                    MAX_ATTEMPTS, currentGroup, code);
    }
    else
    {
      setBlink(BLINK_WRONG_RED);
      startTune(TUNE_WRONG, 1);
      Serial.printf("Incorrect (attempt %u/%u); submitted code = %u\n", wrongCount, MAX_ATTEMPTS, code);
    }
  }
}

void onResetRequested()
{
  enterSetup();
  Serial.println("Reset -> return to group selection mode");
}

uint8_t readSwitchCode()
{
  uint8_t code = 0;
  for (uint8_t i = 0; i < 5; i++)
  {
    if (digitalRead(SW_PIN[i]) == LOW)
      code |= (1 << i);
  }
  return code;
}

void onConfirmTap()
{
  uint8_t code = readSwitchCode();
  Serial.printf("[CONFIRM] Tap: state=%u switch_value=%u\n",
                static_cast<unsigned>(state), code);
  if (state == SETUP)
    trySetGroup(code); // Fallback: select the group using the switches
  else if (state == PLAYING)
    submitGuess(code);
  else
    Serial.println("[CONFIRM] Round ended. Hold CONFIRM 2 seconds to return to SETUP first.");
}

void handleConfirmButton()
{
  bool reading = digitalRead(CONFIRM_PIN);

  if (reading != confirmPrevReading)
    confirmDebounceAt = millis();
  confirmPrevReading = reading;

  if (millis() - confirmDebounceAt > DEBOUNCE_MS && reading != confirmStableState)
  {
    confirmStableState = reading;
    if (confirmStableState == LOW)
    {
      confirmPressedAt = millis();
      confirmLongFired = false;
      Serial.println("[CONFIRM] GPIO33 pressed (LOW)");
    }
    else
    {
      unsigned long heldMs = millis() - confirmPressedAt;
      Serial.printf("[CONFIRM] Released after %lu ms\n", heldMs);
      if (!confirmLongFired && heldMs < LONG_PRESS_MS)
        onConfirmTap();
    }
  }

  if (confirmStableState == LOW && !confirmLongFired &&
      millis() - confirmPressedAt >= LONG_PRESS_MS)
  {
    confirmLongFired = true;
    onResetRequested();
  }
}

void updateBlink()
{
  const unsigned long interval =
      (blinkMode == BLINK_SETUP_ALT) ? 400 : (blinkMode == BLINK_WRONG_RED || blinkMode == BLINK_ERROR_RED) ? 150
                                                                                                            : 500;

  switch (blinkMode)
  {
  case BLINK_OFF:
    break;
  case BLINK_SOLVED_GREEN:
    writeGreen(true);
    writeRed(false);
    break;
  case BLINK_FAILED_RED:
    writeRed(true);
    writeGreen(false);
    break;
  case BLINK_SETUP_ALT:
    if (millis() - blinkLastToggle >= interval)
    {
      blinkLastToggle = millis();
      blinkPhase = !blinkPhase;
      writeRed(blinkPhase);
      writeGreen(!blinkPhase);
    }
    break;
  case BLINK_WRONG_RED:
  case BLINK_ERROR_RED:
    if (blinkPulsesLeft == 0)
    {
      writeRed(false);
      blinkMode = (state == SETUP) ? BLINK_SETUP_ALT : (state == FAILED) ? BLINK_FAILED_RED
                                                                         : BLINK_OFF;
      break;
    }
    if (millis() - blinkLastToggle >= interval)
    {
      blinkLastToggle = millis();
      blinkPhase = !blinkPhase;
      writeRed(blinkPhase);
      if (!blinkPhase)
        blinkPulsesLeft--;
    }
    break;
  }
}

// ---------------- Web page (embedded HTML/CSS/JS; no internet required) ----------------
const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Safe Challenge - Admin Console</title>
<style>
  :root{
    --bg-void:#020611;
    --bg-deep:#04102b;
    --bg-mid:#082252;
    --panel:rgba(9,25,58,0.72);
    --panel-border:rgba(56,189,248,0.28);
    --cyan:#38bdf8;
    --cyan-bright:#7dd3fc;
    --cyan-dim:#0ea5c9;
    --indigo:#3b5bdb;
    --text:#e6f1ff;
    --text-mute:#8ca3c9;
    --ok:#2dd4bf;
    --ok-glow:#5eead4;
    --warn:#fbbf24;
    --bad:#f43f5e;
    --bad-glow:#fb7185;
  }
  *{box-sizing:border-box}
  body{font-family:'Segoe UI',Tahoma,sans-serif;
       background:
         radial-gradient(ellipse 900px 600px at 50% -10%, var(--bg-mid) 0%, transparent 60%),
         radial-gradient(ellipse 700px 500px at 90% 110%, #0a1a3f 0%, transparent 55%),
         linear-gradient(160deg, var(--bg-deep) 0%, var(--bg-void) 100%);
       background-attachment:fixed;
       color:var(--text);
       display:flex;justify-content:center;padding:16px;margin:0;min-height:100vh}
  .card{background:var(--panel);border:1px solid var(--panel-border);
        border-radius:16px;padding:20px;max-width:450px;width:100%;
        backdrop-filter:blur(10px);
        box-shadow:0 0 0 1px rgba(56,189,248,0.06) inset,
                   0 20px 60px rgba(0,0,0,0.55),
                   0 0 40px rgba(56,189,248,0.08)}
  h1{font-size:19px;margin:0 0 14px;text-align:center;color:var(--cyan-bright);
     letter-spacing:.3px;text-shadow:0 0 18px rgba(56,189,248,0.45)}
  h1::before{content:"\2646  "}
  .badges-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;gap:8px}
  .badge{display:inline-block;padding:6px 14px;border-radius:20px;font-weight:bold;font-size:12px;
         border:1px solid transparent;white-space:nowrap}
  .b-setup{background:#1e293b;color:#cbd5e1;border-color:#334155}
  .b-playing{background:rgba(59,91,219,0.22);color:#93c5fd;border-color:var(--indigo);
             box-shadow:0 0 12px rgba(59,91,219,0.35)}
  .b-solved{background:rgba(45,212,191,0.16);color:var(--ok-glow);border-color:var(--ok);
            box-shadow:0 0 14px rgba(45,212,191,0.4)}
  .b-failed{background:rgba(244,63,94,0.16);color:var(--bad-glow);border-color:var(--bad);
            box-shadow:0 0 14px rgba(244,63,94,0.4)}
  .b-locked{background:rgba(244,63,94,0.14);color:#fca5b5;border-color:#7f1d3d}
  .b-open{background:rgba(45,212,191,0.16);color:var(--ok-glow);border-color:var(--ok)}

  .stats-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px}
  .stat-card{background:rgba(4,16,43,0.65);padding:8px 12px;border-radius:10px;
             border:1px solid rgba(56,189,248,0.12)}
  .stat-label{font-size:11px;color:var(--text-mute);letter-spacing:.2px}
  .stat-val{font-size:15px;font-weight:bold;color:var(--cyan-bright);margin-top:2px}

  /* Answer Key Box */
  .key-card{background:linear-gradient(160deg, rgba(14,165,201,0.10), rgba(4,16,43,0.7));
            border:1px solid var(--cyan-dim);border-radius:12px;padding:12px;margin-bottom:12px;
            box-shadow:0 0 20px rgba(14,165,201,0.12) inset}
  .key-header{font-size:13px;color:var(--cyan);font-weight:bold;display:flex;
              justify-content:space-between;margin-bottom:4px}
  .key-main{font-size:16px;color:#f0f9ff}
  .key-main b{color:var(--cyan-bright);font-size:18px;text-shadow:0 0 10px rgba(125,211,252,0.5)}
  .key-sub{font-size:12px;color:var(--text-mute);margin-top:4px}

  /* Live Hardware Monitor */
  .section-title{font-size:12px;color:var(--cyan-bright);margin:12px 0 8px;font-weight:bold;
                 letter-spacing:.4px;text-transform:uppercase;
                 display:flex;align-items:center;gap:6px;
                 border-bottom:1px solid rgba(56,189,248,0.18);padding-bottom:4px}
  .hw-card{background:rgba(4,16,43,0.6);border-radius:12px;padding:12px;margin-bottom:12px;
           border:1px solid rgba(56,189,248,0.15)}
  .hw-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
  .hw-row:last-child{margin-bottom:0}
  .hw-name{font-size:13px;color:#b9cbe8}

  /* Switches 5-bit (SW5 MSB on left, SW1 LSB on right) */
  .sw-group{display:flex;gap:4px;direction:rtl}
  .sw-pill{padding:4px 7px;border-radius:6px;font-size:11px;font-family:Consolas,monospace;
           background:#101c37;color:#64748b;text-align:center;transition:all 0.15s;
           direction:ltr;border:1px solid rgba(100,116,139,0.3)}
  .sw-pill.on{background:var(--cyan-dim);color:#03151f;font-weight:bold;
              box-shadow:0 0 10px rgba(56,189,248,0.7);border-color:var(--cyan-bright)}

  /* Confirm button pill */
  .pill{padding:3px 10px;border-radius:12px;font-size:12px;font-weight:bold;transition:all 0.15s;
        border:1px solid transparent}
  .pill-idle{background:#101c37;color:#64748b;border-color:rgba(100,116,139,0.3)}
  .pill-pressed{background:var(--warn);color:#1a1200;box-shadow:0 0 12px rgba(251,191,36,0.7)}

  /* LEDs */
  .leds-wrap{display:flex;gap:14px;align-items:center}
  .led-item{display:flex;align-items:center;gap:6px;font-size:12px;color:#b9cbe8}
  .led-dot{width:14px;height:14px;border-radius:50%;display:inline-block;transition:all 0.15s}
  .led-r-off{background:#2a0f16;border:1px solid #7f1d3d}
  .led-r-on{background:var(--bad);box-shadow:0 0 12px var(--bad);border:1px solid var(--bad-glow)}
  .led-g-off{background:#0b2420;border:1px solid #115e51}
  .led-g-on{background:var(--ok);box-shadow:0 0 12px var(--ok);border:1px solid var(--ok-glow)}

  /* Servo status */
  .servo-status{font-weight:bold;font-size:13px}

  /* Action buttons */
  .row{display:flex;gap:8px;margin-bottom:10px}
  input[type=number]{flex:1;padding:10px;border-radius:8px;border:1px solid rgba(56,189,248,0.3);
                      background:#04102b;color:var(--text);font-size:15px}
  input[type=number]:focus{outline:none;border-color:var(--cyan);box-shadow:0 0 0 3px rgba(56,189,248,0.2)}
  button{padding:10px 16px;border:none;border-radius:8px;font-size:14px;cursor:pointer;
         transition:opacity .2s, transform .1s}
  button:active{opacity:.8;transform:scale(0.98)}
  .btn-start{background:linear-gradient(135deg, var(--cyan) 0%, var(--cyan-dim) 100%);
             color:#03151f;font-weight:bold;box-shadow:0 0 14px rgba(56,189,248,0.4)}
  .btn-reset{background:linear-gradient(135deg, var(--bad) 0%, #b91c3c 100%);
             color:#fff;font-weight:bold;width:100%;box-shadow:0 0 14px rgba(244,63,94,0.3)}
  #msg{min-height:22px;font-size:13px;margin:8px 0 0;text-align:center}
</style></head><body>
<div class="card">
  <h1>Neptune's Vault Challenge - Console</h1>
  <div class="badges-row">
    <span id="badge" class="badge b-setup">SETUP</span>
    <span id="servoBadge" class="badge b-open">🔓 SERVO: UNLOCKED (90°)</span>
  </div>

  <div class="stats-grid">
    <div class="stat-card">
      <div class="stat-label">Current Group</div>
      <div id="group" class="stat-val">-</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">Incorrect Attempts (Max 3)</div>
      <div id="wrong" class="stat-val">-</div>
    </div>
    <div class="stat-card" style="grid-column:span 2">
      <div class="stat-label">Elapsed Time</div>
      <div id="elapsed" class="stat-val">-</div>
    </div>
  </div>

  <!-- Answer Key Section -->
  <div class="key-card">
    <div class="key-header">
      <span>🔑 Group Answer Key</span>
      <span id="keyGroupTitle"></span>
    </div>
    <div id="keyBox" class="key-main">- No Group Selected -</div>
    <div id="keySub" class="key-sub">Please enter group number (1-20) and click Start Group</div>
  </div>

  <!-- Live Hardware Monitor -->
  <div class="section-title">⚡ Live Hardware Monitor</div>
  <div class="hw-card">
    <div class="hw-row">
      <span class="hw-name">5-bit Switches:</span>
      <div class="sw-group">
      <span id="sw0" class="sw-pill">SW1: 0</span>
      <span id="sw1" class="sw-pill">SW2: 0</span>
      <span id="sw2" class="sw-pill">SW3: 0</span>
      <span id="sw3" class="sw-pill">SW4: 0</span>
      <span id="sw4" class="sw-pill">SW5: 0</span>
      </div>
    </div>
    <div style="font-size:12px;color:var(--text-mute);text-align:right;margin-bottom:8px">
      Current Value: <b id="swDec" style="color:var(--cyan-bright)">0</b> (Binary: <span id="swBin" style="font-family:monospace;color:var(--cyan-bright)">00000</span>)
    </div>

    <div class="hw-row">
      <span class="hw-name">Confirm Button:</span>
      <span id="btnConfirm" class="pill pill-idle">IDLE</span>
    </div>

    <div class="hw-row">
      <span class="hw-name">Status LEDs:</span>
      <div class="leds-wrap">
        <div class="led-item">
          <span id="ledRedDot" class="led-dot led-r-off"></span> <span>RED</span>
        </div>
        <div class="led-item">
          <span id="ledGreenDot" class="led-dot led-g-off"></span> <span>GREEN</span>
        </div>
      </div>
    </div>

    <div class="hw-row">
      <span class="hw-name">Door Lock (Servo):</span>
      <span id="servoText" class="servo-status" style="color:#4ade80">UNLOCKED (90°)</span>
    </div>
  </div>

  <div class="row" style="margin-top:14px">
    <input type="number" id="groupInput" min="1" max="20" placeholder="Group (1-20)">
    <button class="btn-start" onclick="startGroup()">Start Group</button>
  </div>
  <button class="btn-reset" onclick="doReset()">Reset to SETUP</button>
  <p style="text-align:center"><a href="/play" target="_blank" rel="noopener" style="color:#38bdf8">Open player display ↗</a></p>
  <p id="msg"></p>
</div>

<script>
function fmt(s){ s=Math.floor(s); return String(Math.floor(s/60)).padStart(2,'0')+":"+String(s%60).padStart(2,'0'); }
let inFlight = false;
function refresh(){
  if (inFlight) return;
  inFlight = true;
  fetch('/status').then(r=>r.json()).then(d=>{
    inFlight = false;

    // Game State Badge
    const badge = document.getElementById('badge');
    if (d.state === 'FAILED') {
      badge.textContent = 'FAILED';
      badge.className = 'badge b-failed';
    } else if (d.state === 'SOLVED') {
      badge.textContent = 'SOLVED';
      badge.className = 'badge b-solved';
    } else if (d.state === 'PLAYING') {
      badge.textContent = 'PLAYING';
      badge.className = 'badge b-playing';
    } else {
      badge.textContent = 'SETUP';
      badge.className = 'badge b-setup';
    }

    document.getElementById('group').textContent = d.group > 0 ? ('Group ' + d.group) : '-';

    // Wrong attempts (Max 3)
    const wrongEl = document.getElementById('wrong');
    if (d.state === 'SETUP') {
      wrongEl.textContent = '-';
    } else {
      const remain = Math.max(0, 3 - d.wrong);
      wrongEl.innerHTML = d.wrong + ' / 3 <span style="font-size:12px;color:' +
        (remain === 0 ? '#ef4444' : remain === 1 ? '#f59e0b' : '#10b981') +
        '">(' + remain + ' left)</span>';
    }

    document.getElementById('elapsed').textContent = d.state === 'SETUP' ? '-' : fmt(d.elapsed);

    // Answer Key
    const keyBox = document.getElementById('keyBox');
    const keySub = document.getElementById('keySub');
    const keyGroupTitle = document.getElementById('keyGroupTitle');
    if (d.group > 0 && d.expected !== undefined) {
      keyGroupTitle.textContent = '[Group ' + d.group + ']';
      const expBin = d.expected.toString(2).padStart(5, '0');
      let onSw = [];
      for (let i = 0; i < 5; i++) {
        if ((d.expected >> i) & 1) onSw.push('SW' + (i + 1));
      }
      keyBox.innerHTML = 'Target Code: <b>' + d.expected + '</b> (Binary: <span style="font-family:monospace;color:#38bdf8">' + expBin + '</span>)';
      keySub.textContent = onSw.length > 0 ? ('Required Switches (ON): ' + onSw.join(', ')) : 'All switches OFF (0)';
    } else {
      keyGroupTitle.textContent = '';
      keyBox.textContent = '- No Group Selected -';
      keySub.textContent = 'Please enter group number (1-20) and click Start Group';
    }

    // 5-bit Switches
    if (d.switches) {
      for (let i = 0; i < 5; i++) {
        const el = document.getElementById('sw' + i);
        const val = d.switches[i];
        el.textContent = 'SW' + (i + 1) + ':' + val;
        el.className = 'sw-pill ' + (val ? 'on' : '');
      }
    }
    const sVal = (d.switch_val !== undefined) ? d.switch_val : 0;
    document.getElementById('swDec').textContent = sVal;
    document.getElementById('swBin').textContent = sVal.toString(2).padStart(5, '0');

    // Confirm Button
    const btnC = document.getElementById('btnConfirm');
    if (d.confirm_btn) {
      btnC.textContent = 'PRESSED';
      btnC.className = 'pill pill-pressed';
    } else {
      btnC.textContent = 'IDLE';
      btnC.className = 'pill pill-idle';
    }

    // LEDs
    const rDot = document.getElementById('ledRedDot');
    rDot.className = 'led-dot ' + (d.led_red ? 'led-r-on' : 'led-r-off');
    const gDot = document.getElementById('ledGreenDot');
    gDot.className = 'led-dot ' + (d.led_green ? 'led-g-on' : 'led-g-off');

    // Door Lock (Servo)
    const sBadge = document.getElementById('servoBadge');
    const sText = document.getElementById('servoText');
    const isLocked = (d.servo_angle === 0 || d.servo_status === 'LOCKED');
    if (isLocked) {
      sBadge.textContent = '🔒 LOCKED (' + (d.servo_angle || 0) + '°)';
      sBadge.className = 'badge b-locked';
      sText.textContent = 'LOCKED (' + (d.servo_angle || 0) + '°)';
      sText.style.color = '#f87171';
    } else {
      sBadge.textContent = '🔓 UNLOCKED (' + (d.servo_angle || 90) + '°)';
      sBadge.className = 'badge b-open';
      sText.textContent = 'UNLOCKED (' + (d.servo_angle || 90) + '°)';
      sText.style.color = '#4ade80';
    }

    // Status Messages
    const msg = document.getElementById('msg');
    if (d.state === 'FAILED') {
      msg.innerHTML = '<span style="color:#ef4444;font-weight:bold;">❌ 3 incorrect attempts reached! (Click Reset to retry)</span>';
    } else if (d.state === 'SOLVED') {
      msg.innerHTML = '<span style="color:#22c55e;font-weight:bold;">🎉 Safe Solved & Unlocked!</span>';
    } else if (!msg.getAttribute('data-temp')) {
      msg.textContent = '';
    }
  }).catch(()=>{ inFlight = false; });
}

function startGroup(){
  const g = document.getElementById('groupInput').value;
  const msg = document.getElementById('msg');
  msg.setAttribute('data-temp', '1');
  fetch('/start', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'group=' + encodeURIComponent(g)
  })
  .then(r => r.json())
  .then(d => {
    if (!d.ok) {
      msg.innerHTML = '<span style="color:#f59e0b">Invalid group number (must be 1-20)</span>';
    } else {
      msg.textContent = '';
      msg.removeAttribute('data-temp');
    }
    refresh();
  });
}

function doReset(){
  fetch('/reset', {method: 'POST'}).then(refresh);
}

setInterval(refresh, 300);
refresh();
</script></body></html>
)HTML";

void handlePlayerStatus()
{
  unsigned long elapsedSec = 0;
  if (state == PLAYING)
    elapsedSec = (millis() - roundStartMs) / 1000;
  else if (state == SOLVED)
    elapsedSec = solvedElapsedMs / 1000;
  else if (state == FAILED)
    elapsedSec = failedElapsedMs / 1000;

  const char *stateStr = (state == SETUP) ? "SETUP"
                         : (state == PLAYING) ? "PLAYING"
                         : (state == SOLVED) ? "SOLVED" : "FAILED";
  const uint8_t swCode = readSwitchCode();
  char buf[220];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"group\":%u,\"wrong\":%u,\"max_wrong\":%u,\"elapsed\":%lu,"
           "\"switches\":[%u,%u,%u,%u,%u]}",
           stateStr, currentGroup, wrongCount, MAX_ATTEMPTS, elapsedSec,
           (swCode >> 0) & 1, (swCode >> 1) & 1, (swCode >> 2) & 1,
           (swCode >> 3) & 1, (swCode >> 4) & 1);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

void handlePlayer()
{
  server.send_P(200, "text/html; charset=utf-8", PLAYER_PAGE_HTML);
}

void handleRoot()
{
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleStatus()
{
  unsigned long elapsedSec = 0;
  if (state == PLAYING)
    elapsedSec = (millis() - roundStartMs) / 1000;
  else if (state == SOLVED)
    elapsedSec = solvedElapsedMs / 1000;

  const char *stateStr = (state == SETUP)     ? "SETUP"
                         : (state == PLAYING) ? "PLAYING"
                         : (state == SOLVED)  ? "SOLVED"
                                              : "FAILED";

  // Read the live states of all 5 switches from GPIO
  uint8_t swCode = 0;
  uint8_t sw[5];
  for (uint8_t i = 0; i < 5; i++)
  {
    bool isPressed = (digitalRead(SW_PIN[i]) == LOW);
    sw[i] = isPressed ? 1 : 0;
    if (isPressed)
      swCode |= (1 << i);
  }

  // Read the live confirm button state (pressed = LOW)
  bool confirmPressed = (digitalRead(CONFIRM_PIN) == LOW);

  // LEDs and servo
  bool ledRed = currentRedLed;
  bool ledGreen = currentGreenLed;
  uint8_t servoAngle = currentServoAngle;
  const char *servoStr = (servoAngle == SERVO_OPEN) ? "UNLOCKED" : "LOCKED";

  // Answer key for the current group
  uint8_t expCode = (currentGroup >= 1 && currentGroup <= NUM_GROUPS) ? ANSWER_KEY[currentGroup - 1] : 0;

  char buf[380];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"group\":%u,\"wrong\":%u,\"max_wrong\":%u,\"elapsed\":%lu,"
           "\"expected\":%u,\"switches\":[%u,%u,%u,%u,%u],\"switch_val\":%u,"
           "\"confirm_btn\":%s,\"led_red\":%s,\"led_green\":%s,"
           "\"servo_angle\":%u,\"servo_status\":\"%s\"}",
           stateStr, currentGroup, wrongCount, MAX_ATTEMPTS, elapsedSec,
           expCode, sw[0], sw[1], sw[2], sw[3], sw[4], swCode,
           confirmPressed ? "true" : "false",
           ledRed ? "true" : "false",
           ledGreen ? "true" : "false",
           servoAngle, servoStr);
  server.send(200, "application/json", buf);
}

void handleStart()
{
  int g = server.hasArg("group") ? server.arg("group").toInt() : 0;
  bool ok = (g >= 1 && g <= NUM_GROUPS);
  trySetGroup((uint8_t)g); // The existing function flashes the error LED for an invalid group number
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
  server.send(200, "application/json", buf);
}

void handleReset()
{
  onResetRequested();
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup()
{
  Serial.begin(115200);
  for (uint8_t i = 0; i < 5; i++)
    pinMode(SW_PIN[i], INPUT_PULLUP);
  pinMode(CONFIRM_PIN, INPUT_PULLUP);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  writeRed(false);
  writeGreen(false);

  if (BUZZER_TYPE == BuzzerType::PASSIVE)
  {
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(BUZZER_PIN, 2000, 8);
#else
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
#endif
  }
  else
  {
    digitalWrite(BUZZER_PIN, ACTIVE_BUZZER_ON_LEVEL == HIGH ? LOW : HIGH);
    pinMode(BUZZER_PIN, OUTPUT);
  }
  setBuzzerTone(0);

  lockServo.setPeriodHertz(50);
  lockServo.attach(SERVO_PIN, 500, 2400);
  writeServo(SERVO_OPEN);

  enterSetup();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Access Point: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("Open a browser at: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/admin", HTTP_GET, handleRoot);
  server.on("/play", HTTP_GET, handlePlayer);
  server.on("/player-status", HTTP_GET, handlePlayerStatus);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();

  Serial.println("Web server ready");
}

void loop()
{
  server.handleClient();
  handleConfirmButton();
  updateBlink();
  updateTune();
}
