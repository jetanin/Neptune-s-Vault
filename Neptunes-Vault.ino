/*
  Safe Challenge - ESP32 (Arduino framework) + Access Point
  ==========================================================
  ทีมงานสั่งงาน "จากหลังบ้าน" ผ่านเว็บเพจในตัว ESP32 เอง:
    - ESP32 ปล่อย WiFi ของตัวเอง (Access Point) ไม่ต้องมีอินเทอร์เน็ต
    - ทีมงานต่อ WiFi นี้จากมือถือ/โน้ตบุ๊ก แล้วเปิดเบราว์เซอร์ไปที่ IP ของ ESP32
      (ค่าเริ่มต้นของ Access Point คือ 192.168.4.1)
    - หน้าเว็บ: กรอกเลขกลุ่ม (1-20) กดเริ่ม / ดูสถานะสด (LOCKED, กำลังเล่น, ผิดกี่ครั้ง,
      เวลาที่ใช้) / ปุ่มรีเซ็ตกลับ SETUP

  ฮาร์ดแวร์ (เหมือนเวอร์ชัน Arduino เดิม):
    - 5x สวิตช์ on/off      -> ผู้เข้าร่วมกรอกรหัส (bit0..bit4)
                                ใช้เป็นสำรองตั้งเลขกลุ่มได้ด้วยถ้าไม่สะดวกใช้เว็บ
    - 1x push switch        -> ปุ่มยืนยัน (กดสั้น = ยืนยัน, กดค้าง >=2วิ = รีเซ็ต)
    - 1x red LED, 1x green LED
    - 1x buzzer (passive แนะนำ ใช้ ledc สร้างเสียงได้หลายความถี่)
    - 1x servo

  ทำไมเลือก Arduino framework (ไม่ใช่ ESP-IDF):
    งานนี้ต้องการ WebServer + Servo + digitalRead/millis ธรรมดา ซึ่ง Arduino core
    ของ ESP32 มี WebServer.h และไลบรารี ESP32Servo ให้ใช้ได้ทันที โค้ดสั้น อ่านง่าย
    ต่อยอดจากเวอร์ชัน Nano/Uno เดิมได้เกือบทั้งหมด ส่วน ESP-IDF ต้องเขียน HTTP server
    (esp_http_server) และคุม PWM/servo เองทั้งหมดด้วย low-level API ซึ่งซับซ้อนกว่า
    มากโดยไม่ได้ประโยชน์เพิ่มสำหรับโปรเจกต์ขนาดนี้ จึงแนะนำ Arduino framework

  การต่อขา (เลี่ยง strapping pin ของ ESP32: GPIO0,2,5,12,15 และเลี่ยง GPIO34-39
  เพราะเป็น input-only ไม่มี internal pull-up ให้ใช้กับสวิตช์):
    SW1(bit0) -> GPIO13      SW4(bit3) -> GPIO26
    SW2(bit1) -> GPIO14      SW5(bit4) -> GPIO25
    SW3(bit2) -> GPIO27      CONFIRM   -> GPIO33
    RED LED   -> GPIO32      GREEN LED -> GPIO21
    BUZZER    -> GPIO19      SERVO     -> GPIO18
  สวิตช์/ปุ่มทุกตัวต่อขาหนึ่งเข้าพินที่กำหนด อีกขาลง GND (โค้ดใช้ INPUT_PULLUP
  จึงไม่ต้องมีตัวต้านทานภายนอก) กด/on แล้วจะอ่านได้ LOW

  ติดตั้ง/อัปโหลดด้วย PlatformIO:
    pio run -t upload
    pio device monitor          (ดู IP ของ Access Point และ log สถานะ)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ---------------- Access Point ----------------
const char *AP_SSID = "SafeChallenge";
const char *AP_PASS = "safe12345"; // ต้อง >= 8 ตัวอักษรสำหรับ WPA2

WebServer server(80);

// ---------------- ขา ----------------
const uint8_t SW_PIN[5] = {13, 14, 27, 26, 25}; // bit0..bit4
const uint8_t CONFIRM_PIN = 33;
const uint8_t RED_PIN = 32;
const uint8_t GREEN_PIN = 21;
const uint8_t BUZZER_PIN = 19;
const uint8_t SERVO_PIN = 18;

const uint8_t BUZZER_LEDC_CHANNEL = 2; // channel ของ ledc สำหรับ buzzer

// ---------------- ตำแหน่ง servo ----------------
const uint8_t SERVO_LOCKED = 0;
const uint8_t SERVO_OPEN = 90;
Servo lockServo;
uint8_t currentServoAngle = SERVO_LOCKED;

void setServoAngle(uint8_t angle)
{
  currentServoAngle = angle;
  lockServo.write(angle);
}

// ---------------- สถานะไฟ LED ----------------
bool currentRedLed = false;
bool currentGreenLed = false;

void setRedLed(bool on)
{
  currentRedLed = on;
  digitalWrite(RED_PIN, on ? HIGH : LOW);
}

void setGreenLed(bool on)
{
  currentGreenLed = on;
  digitalWrite(GREEN_PIN, on ? HIGH : LOW);
}

// ---------------- เฉลย (จาก ChallengeKey.csv, one=bit4 ... five=bit0) ----------------
const uint8_t NUM_GROUPS = 20;
const uint8_t ANSWER_KEY[NUM_GROUPS] = {
    /* 1 */ 20,
    /* 2 */ 0,
    /* 3 */ 5,
    /* 4 */ 17,
    /* 5 */ 6,
    /* 6 */ 12,
    /* 7 */ 3,
    /* 8 */ 23,
    /* 9 */ 5,
    /*10 */ 23,
    /*11 */ 20,
    /*12 */ 5,
    /*13 */ 5,
    /*14 */ 9,
    /*15 */ 5,
    /*16 */ 20,
    /*17 */ 24,
    /*18 */ 5,
    /*19 */ 23,
    /*20 */ 12,
};

// ---------------- สถานะเกม ----------------
enum GameState
{
  SETUP,
  PLAYING,
  SOLVED
};
GameState state = SETUP;

uint8_t currentGroup = 0;
uint8_t expectedCode = 0;
uint16_t wrongCount = 0;
unsigned long roundStartMs = 0;
unsigned long solvedElapsedMs = 0; // เวลาที่ใช้ ณ ตอนตอบถูก (ไว้แสดงหลัง SOLVED)

// ---------------- debounce/long-press ปุ่มยืนยัน ----------------
bool confirmPrevReading = HIGH;
bool confirmStableState = HIGH;
unsigned long confirmDebounceAt = 0;
unsigned long confirmPressedAt = 0;
bool confirmLongFired = false;
const unsigned long DEBOUNCE_MS = 25;
const unsigned long LONG_PRESS_MS = 2000;

// ---------------- ไฟกระพริบแบบ non-blocking ----------------
enum BlinkMode
{
  BLINK_OFF,
  BLINK_SETUP_ALT,
  BLINK_ERROR_RED,
  BLINK_WRONG_RED,
  BLINK_SOLVED_GREEN
};
BlinkMode blinkMode = BLINK_SETUP_ALT;
unsigned long blinkLastToggle = 0;
bool blinkPhase = false;
uint8_t blinkPulsesLeft = 0;

// ---------------- เสียงแบบ non-blocking (ไม่ใช้ delay() กัน WebServer หน่วง) ----------------
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
      if (f == 0)
        ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
      else
        ledcWriteTone(BUZZER_LEDC_CHANNEL, f);
      setBuzzerTone(f);
      tuneNextAt = millis() + activeTune[tuneIdx].durMs;
      tuneIdx++;
    }
    else
    {
      ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
      setBuzzerTone(0);
      activeTune = nullptr;
    }
  }
}

// ---------------- ฟังก์ชันหลักของเกม (โครงเดียวกับเวอร์ชัน Arduino เดิม) ----------------
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
  wrongCount = 0;
  setServoAngle(SERVO_LOCKED);
  setRedLed(false);
  setGreenLed(false);
  setBlink(BLINK_SETUP_ALT);
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
    setServoAngle(SERVO_LOCKED);
    setBlink(BLINK_OFF);
    setRedLed(false);
    setGreenLed(false);
    startTune(TUNE_CONFIRM, 1);

    Serial.printf("เริ่มกลุ่ม %u -> LOCKED พร้อมให้กรอกรหัส\n", currentGroup);
  }
  else
  {
    setBlink(BLINK_ERROR_RED);
    startTune(TUNE_ERROR, 3);
    Serial.printf("เลขกลุ่มไม่ถูกต้อง: %u\n", group);
  }
}

void submitGuess(uint8_t code)
{
  unsigned long elapsed = millis() - roundStartMs;

  if (code == expectedCode)
  {
    state = SOLVED;
    solvedElapsedMs = elapsed;
    setBlink(BLINK_SOLVED_GREEN);
    setRedLed(false);
    setServoAngle(SERVO_OPEN);
    startTune(TUNE_CORRECT, 3);

    Serial.printf("ถูกต้อง! กลุ่ม %u ใช้เวลา %lu วิ ผิด %u ครั้ง\n",
                  currentGroup, elapsed / 1000, wrongCount);
  }
  else
  {
    wrongCount++;
    setBlink(BLINK_WRONG_RED);
    startTune(TUNE_WRONG, 1);
    Serial.printf("ผิด (ครั้งที่ %u) รหัสที่กรอก = %u\n", wrongCount, code);
  }
}

void onResetRequested()
{
  enterSetup();
  Serial.println("รีเซ็ต -> กลับสู่โหมดตั้งเลขกลุ่ม");
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
  if (state == SETUP)
    trySetGroup(code); // สำรอง: ตั้งกลุ่มด้วยสวิตช์ก็ได้
  else if (state == PLAYING)
    submitGuess(code);
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
    }
    else
    {
      unsigned long heldMs = millis() - confirmPressedAt;
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
    setGreenLed(true);
    break;
  case BLINK_SETUP_ALT:
    if (millis() - blinkLastToggle >= interval)
    {
      blinkLastToggle = millis();
      blinkPhase = !blinkPhase;
      setRedLed(blinkPhase);
      setGreenLed(!blinkPhase);
    }
    break;
  case BLINK_WRONG_RED:
  case BLINK_ERROR_RED:
    if (blinkPulsesLeft == 0)
    {
      setRedLed(false);
      blinkMode = (state == SETUP) ? BLINK_SETUP_ALT : BLINK_OFF;
      break;
    }
    if (millis() - blinkLastToggle >= interval)
    {
      blinkLastToggle = millis();
      blinkPhase = !blinkPhase;
      setRedLed(blinkPhase);
      if (!blinkPhase)
        blinkPulsesLeft--;
    }
    break;
  }
}

// ---------------- หน้าเว็บ (ฝัง HTML/CSS/JS ไว้ในตัว ไม่ต้องมีอินเทอร์เน็ต) ----------------
// ---------------- หน้าเว็บ (ฝัง HTML/CSS/JS ไว้ในตัว ไม่ต้องมีอินเทอร์เน็ต) ----------------
const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Safe Challenge - หลังบ้าน</title>
<style>
  body{font-family:Tahoma,sans-serif;background:#181b20;color:#e6e8ec;
       display:flex;justify-content:center;padding:16px;margin:0}
  .card{background:#23272f;border-radius:14px;padding:20px;max-width:440px;width:100%;box-shadow:0 8px 24px rgba(0,0,0,0.4)}
  h1{font-size:19px;margin:0 0 14px;text-align:center;color:#61dafb}
  .badges-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
  .badge{display:inline-block;padding:6px 14px;border-radius:20px;font-weight:bold;font-size:13px}
  .b-setup{background:#4b5563;color:#fff}
  .b-playing{background:#dc2626;color:#fff}
  .b-solved{background:#16a34a;color:#fff}
  .b-locked{background:#7f1d1d;color:#fca5a5;border:1px solid #dc2626}
  .b-open{background:#14532d;color:#86efac;border:1px solid #16a34a}
  
  .stats-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:14px}
  .stat-card{background:#1a1d24;padding:8px 12px;border-radius:8px}
  .stat-label{font-size:12px;color:#9ca3af}
  .stat-val{font-size:16px;font-weight:bold;color:#93c5fd;margin-top:2px}
  
  .section-title{font-size:13px;color:#93c5fd;margin:12px 0 8px;font-weight:bold;
                 display:flex;align-items:center;gap:6px;border-bottom:1px solid #374151;padding-bottom:4px}
  .hw-card{background:#1a1d24;border-radius:10px;padding:12px;margin-bottom:12px;border:1px solid #2e3440}
  .hw-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
  .hw-row:last-child{margin-bottom:0}
  .hw-name{font-size:13px;color:#d1d5db}
  
  /* Switches 5-bit (SW5 MSB on left, SW1 LSB on right) */
  .sw-group{display:flex;gap:4px;direction:rtl}
  .sw-pill{padding:4px 7px;border-radius:6px;font-size:11px;font-family:Consolas,monospace;
           background:#374151;color:#9ca3af;text-align:center;transition:all 0.15s;direction:ltr}
  .sw-pill.on{background:#0284c7;color:#fff;font-weight:bold;box-shadow:0 0 8px rgba(2,132,199,0.6)}
  
  /* Confirm button pill */
  .pill{padding:3px 10px;border-radius:12px;font-size:12px;font-weight:bold;transition:all 0.15s}
  .pill-idle{background:#374151;color:#9ca3af}
  .pill-pressed{background:#f59e0b;color:#000;box-shadow:0 0 10px #f59e0b}
  
  /* LEDs */
  .leds-wrap{display:flex;gap:14px;align-items:center}
  .led-item{display:flex;align-items:center;gap:6px;font-size:12px}
  .led-dot{width:14px;height:14px;border-radius:50%;display:inline-block;transition:all 0.15s}
  .led-r-off{background:#451a1a;border:1px solid #7f1d1d}
  .led-r-on{background:#ef4444;box-shadow:0 0 12px #ef4444;border:1px solid #fca5a5}
  .led-g-off{background:#143823;border:1px solid #14532d}
  .led-g-on{background:#22c55e;box-shadow:0 0 12px #22c55e;border:1px solid #86efac}
  
  /* Servo status */
  .servo-status{font-weight:bold;font-size:13px}
  
  /* Action buttons */
  .row{display:flex;gap:8px;margin-bottom:10px}
  input[type=number]{flex:1;padding:10px;border-radius:8px;border:1px solid #4b5563;background:#1a1d24;color:#fff;font-size:15px}
  button{padding:10px 16px;border:none;border-radius:8px;font-size:14px;cursor:pointer;transition:opacity 0.2s}
  button:active{opacity:0.8}
  .btn-start{background:#22c55e;color:#052e16;font-weight:bold}
  .btn-reset{background:#dc2626;color:#fff;font-weight:bold;width:100%}
  #msg{min-height:20px;color:#f59e0b;font-size:13px;margin:8px 0 0;text-align:center}
</style></head><body>
<div class="card">
  <h1>Safe Challenge - แผงควบคุม</h1>
  <div class="badges-row">
    <span id="badge" class="badge b-setup">SETUP</span>
    <span id="servoBadge" class="badge b-locked">🔒 SERVO: LOCKED (0°)</span>
  </div>

  <div class="stats-grid">
    <div class="stat-card">
      <div class="stat-label">กลุ่มปัจจุบัน</div>
      <div id="group" class="stat-val">-</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">ผิดกี่ครั้ง</div>
      <div id="wrong" class="stat-val">-</div>
    </div>
    <div class="stat-card" style="grid-column:span 2">
      <div class="stat-label">เวลาที่ใช้</div>
      <div id="elapsed" class="stat-val">-</div>
    </div>
  </div>

  <div class="section-title">⚡ สถานะฮาร์ดแวร์ (Live Hardware)</div>
  <div class="hw-card">
    <div class="hw-row">
      <span class="hw-name">สวิตช์ 5 บิต:</span>
      <div class="sw-group">
        <span id="sw4" class="sw-pill">SW5: 0</span>
        <span id="sw3" class="sw-pill">SW4: 0</span>
        <span id="sw2" class="sw-pill">SW3: 0</span>
        <span id="sw1" class="sw-pill">SW2: 0</span>
        <span id="sw0" class="sw-pill">SW1: 0</span>
      </div>
    </div>
    <div style="font-size:12px;color:#9ca3af;text-align:right;margin-bottom:8px">
      ค่า: <b id="swDec" style="color:#67e8f9">0</b> (Binary: <span id="swBin" style="font-family:monospace;color:#67e8f9">00000</span>)
    </div>

    <div class="hw-row">
      <span class="hw-name">ปุ่มยืนยัน (Confirm):</span>
      <span id="btnConfirm" class="pill pill-idle">ปล่อย</span>
    </div>

    <div class="hw-row">
      <span class="hw-name">ไฟ LED:</span>
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
      <span class="hw-name">เซอร์โว (กลอน):</span>
      <span id="servoText" class="servo-status" style="color:#f87171">LOCKED (0°)</span>
    </div>
  </div>

  <div class="row" style="margin-top:14px">
    <input type="number" id="groupInput" min="1" max="20" placeholder="เลขกลุ่ม 1-20">
    <button class="btn-start" onclick="startGroup()">เริ่ม/เปลี่ยนกลุ่ม</button>
  </div>
  <button class="btn-reset" onclick="doReset()">รีเซ็ตกลับ SETUP</button>
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
    const badge=document.getElementById('badge');
    badge.textContent=d.state;
    badge.className='badge ' + (d.state==='SETUP'?'b-setup':d.state==='PLAYING'?'b-playing':'b-solved');
    document.getElementById('group').textContent = d.group>0 ? ('กลุ่ม ' + d.group) : '-';
    document.getElementById('wrong').textContent = d.state==='SETUP' ? '-' : (d.wrong + ' ครั้ง');
    document.getElementById('elapsed').textContent = d.state==='SETUP' ? '-' : fmt(d.elapsed);

    // Switches
    if (d.switches) {
      for(let i=0; i<5; i++){
        const el = document.getElementById('sw'+i);
        const val = d.switches[i];
        el.textContent = 'SW' + (i+1) + ':' + val;
        el.className = 'sw-pill ' + (val ? 'on' : '');
      }
    }
    const sVal = (d.switch_val !== undefined) ? d.switch_val : 0;
    document.getElementById('swDec').textContent = sVal;
    document.getElementById('swBin').textContent = sVal.toString(2).padStart(5, '0');

    // Confirm button
    const btnC = document.getElementById('btnConfirm');
    if (d.confirm_btn) {
      btnC.textContent = 'กดอยู่ (PRESSED)';
      btnC.className = 'pill pill-pressed';
    } else {
      btnC.textContent = 'ปล่อย (IDLE)';
      btnC.className = 'pill pill-idle';
    }

    // LEDs
    const rDot = document.getElementById('ledRedDot');
    rDot.className = 'led-dot ' + (d.led_red ? 'led-r-on' : 'led-r-off');
    const gDot = document.getElementById('ledGreenDot');
    gDot.className = 'led-dot ' + (d.led_green ? 'led-g-on' : 'led-g-off');

    // Servo
    const sBadge = document.getElementById('servoBadge');
    const sText = document.getElementById('servoText');
    const isLocked = (d.servo_angle === 0 || d.servo_status === 'LOCKED');
    if (isLocked) {
      sBadge.textContent = '🔒 LOCKED (' + (d.servo_angle||0) + '°)';
      sBadge.className = 'badge b-locked';
      sText.textContent = 'LOCKED (' + (d.servo_angle||0) + '°)';
      sText.style.color = '#f87171';
    } else {
      sBadge.textContent = '🔓 OPEN (' + (d.servo_angle||90) + '°)';
      sBadge.className = 'badge b-open';
      sText.textContent = 'OPEN (' + (d.servo_angle||90) + '°)';
      sText.style.color = '#4ade80';
    }
  }).catch(()=>{ inFlight = false; });
}
function startGroup(){
  const g = document.getElementById('groupInput').value;
  const msg = document.getElementById('msg');
  fetch('/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
                  body:'group='+encodeURIComponent(g)})
    .then(r=>r.json()).then(d=>{ msg.textContent = d.ok ? '' : 'เลขกลุ่มไม่ถูกต้อง (1-20)'; refresh(); });
}
function doReset(){
  fetch('/reset',{method:'POST'}).then(refresh);
}
setInterval(refresh, 500);
refresh();
</script></body></html>
)HTML";

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

  const char *stateStr = (state == SETUP) ? "SETUP" : (state == PLAYING) ? "PLAYING"
                                                                         : "SOLVED";

  uint8_t swCode = readSwitchCode();
  uint8_t sw[5];
  for (uint8_t i = 0; i < 5; i++)
  {
    sw[i] = (digitalRead(SW_PIN[i]) == LOW) ? 1 : 0;
  }

  bool confirmPressed = (digitalRead(CONFIRM_PIN) == LOW);
  const char *servoStr = (currentServoAngle == SERVO_OPEN) ? "OPEN" : "LOCKED";

  char buf[360];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"group\":%u,\"wrong\":%u,\"elapsed\":%lu,"
           "\"switches\":[%u,%u,%u,%u,%u],\"switch_val\":%u,"
           "\"confirm_btn\":%s,\"led_red\":%s,\"led_green\":%s,"
           "\"servo_angle\":%u,\"servo_status\":\"%s\"}",
           stateStr, currentGroup, wrongCount, elapsedSec,
           sw[0], sw[1], sw[2], sw[3], sw[4], swCode,
           confirmPressed ? "true" : "false",
           currentRedLed ? "true" : "false",
           currentGreenLed ? "true" : "false",
           currentServoAngle, servoStr);
  server.send(200, "application/json", buf);
}

void handleStart()
{
  int g = server.hasArg("group") ? server.arg("group").toInt() : 0;
  bool ok = (g >= 1 && g <= NUM_GROUPS);
  trySetGroup((uint8_t)g); // ฟังก์ชันเดิมจะกระพริบไฟ error เองถ้าเลขไม่ถูกต้อง
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
  setRedLed(false);
  setGreenLed(false);

#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(BUZZER_PIN, 2000, 8);
#else
  ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
#endif

  lockServo.setPeriodHertz(50);
  lockServo.attach(SERVO_PIN, 500, 2400);
  setServoAngle(SERVO_LOCKED);

  enterSetup();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Access Point: ");
  Serial.println(AP_SSID);
  Serial.print("รหัสผ่าน: ");
  Serial.println(AP_PASS);
  Serial.print("เปิดเบราว์เซอร์ไปที่: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();

  Serial.println("เว็บเซิร์ฟเวอร์พร้อมทำงาน");
}

void loop()
{
  server.handleClient();
  handleConfirmButton();
  updateBlink();
  updateTune();
}