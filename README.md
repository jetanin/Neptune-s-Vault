# Neptune's Vault — Safe Challenge (ESP32)

An interactive, microcontroller-based electronic safe box challenge designed for **Circuit & Logic Workshops**. Powered by an **ESP32 Dev Module**, the system operates in a completely standalone environment without requiring an external internet connection. Staff and workshop organizers can manage game rounds, inspect group answer keys, and monitor physical hardware signals in real-time via a built-in **Web Admin Console** served over ESP32's local WiFi Access Point.

---

## 📌 Key Features

- **Embedded WiFi Access Point (SoftAP)**: Broadcasts its own private WiFi network (`SafeChallenge`). Staff can connect using any smartphone, tablet, or laptop and open `http://192.168.4.1`.
- **Real-Time Live Hardware Monitor**: The web interface polls board status every **300 ms**, showing live states of all 5 binary switches, the confirm button, status LEDs, and current servo angle.
- **Instant Group Answer Key Display**: Selecting a group (1–20) immediately displays the target code in both decimal and 5-bit binary formats, explicitly listing which switches must be turned `ON`.
- **3-Attempt Lockout Enforcement**: Contestants are allowed up to **3 incorrect attempts per group**. Reaching 3 failed attempts triggers the `FAILED` state, sounding an alarm tone, illuminating the red LED, and keeping the door locked until staff reset the system.
- **Smart Servo Latch Lifecycle**:
  - **`SETUP` (Standby / Preparation)**: Servo is **UNLOCKED (90°)** so staff can easily open the door, configure the safe, or place flags and prizes inside.
  - **`PLAYING` (Round Active)**: Servo transitions immediately to **LOCKED (0°)** upon starting a group, securing the vault for contestants.
  - **`SOLVED` (Correct Code)**: Servo transitions to **UNLOCKED (90°)**, turning on the green LED and playing a victory chime.
  - **`FAILED` (3 Failed Attempts)**: Servo remains **LOCKED (0°)** with red error feedback.
- **Multi-Function Confirm Button**:
  - **Short Press (< 2 seconds, then release)**: In `SETUP`, starts the group selected by the switches (1–20). In `PLAYING`, submits the current switch code.
  - **Long Press (≥ 2 seconds)**: Resets the game back to `SETUP` mode (hardware fallback for the web reset button).
- **Non-Blocking Architecture**: LED flashing and buzzer tone melodies are driven asynchronously using `millis()`, ensuring zero latency or stuttering for web server requests.
- **Cross-Framework Compatibility**: Fully compatible with both **PlatformIO** and **Arduino IDE** (ESP32 Core 2.x and 3.x supported).

---

## 🔌 Hardware Wiring & Pinout

The GPIO pins are chosen to avoid ESP32 strapping pins (`GPIO 0, 2, 5, 12, 15`) and input-only pins without internal pull-ups (`GPIO 34–39`):

| Component                     | ESP32 GPIO |    Pin Mode    | Circuit Description                                                    |
| :---------------------------- | :--------: | :------------: | :--------------------------------------------------------------------- |
| **SW1 (Bit 0 - LSB)**         | `GPIO 13`  | `INPUT_PULLUP` | One pin to GPIO, other to GND (`LOW` when switch is ON / closed = `1`) |
| **SW2 (Bit 1)**               | `GPIO 14`  | `INPUT_PULLUP` | One pin to GPIO, other to GND (`LOW` when switch is ON / closed = `1`) |
| **SW3 (Bit 2)**               | `GPIO 27`  | `INPUT_PULLUP` | One pin to GPIO, other to GND (`LOW` when switch is ON / closed = `1`) |
| **SW4 (Bit 3)**               | `GPIO 26`  | `INPUT_PULLUP` | One pin to GPIO, other to GND (`LOW` when switch is ON / closed = `1`) |
| **SW5 (Bit 4 - MSB)**         | `GPIO 25`  | `INPUT_PULLUP` | One pin to GPIO, other to GND (`LOW` when switch is ON / closed = `1`) |
| **Confirm Button**            | `GPIO 33`  | `INPUT_PULLUP` | Momentary push button to GND (`LOW` when pressed)                      |
| **Red LED**                   | `GPIO 32`  |    `OUTPUT`    | Series resistor 220Ω–330Ω to GND                                       |
| **Green LED**                 | `GPIO 21`  |    `OUTPUT`    | Series resistor 220Ω–330Ω to GND                                       |
| **Buzzer (Passive / Active)** | `GPIO 18`  |    `OUTPUT`    | Select PWM tones or digital on/off with `BUZZER_TYPE`                  |
| **Servo Motor**               | `GPIO 19`  |    `OUTPUT`    | 50Hz PWM control signal (0° = LOCKED, 90° = UNLOCKED)                  |

> [!NOTE]
> All switches and buttons utilize ESP32 internal pull-up resistors (`INPUT_PULLUP`). No external pull-up resistors are required.

### Buzzer selection

Set the buzzer type near the pin definitions in `src/main.cpp` (PlatformIO) or `Neptunes-Vault.ino` (Arduino IDE), then build and upload:

```cpp
const BuzzerType BUZZER_TYPE = BuzzerType::PASSIVE; // Default: PWM tones
// For an active buzzer, change PASSIVE above to ACTIVE.
const uint8_t ACTIVE_BUZZER_ON_LEVEL = HIGH; // Change to LOW for an active-low module
```

Passive mode plays the configured note frequencies. Active mode switches the buzzer on for nonzero notes and off for rests, keeping the existing timing without changing its pitch. Consecutive notes without rests sound as one continuous beep in active mode. The buzzer signal remains on **GPIO18**.

### Servo setup (MG90S / S3003)

- The servos used in this project are MG90S and S3003. Connect the signal wire of the installed servo to **GPIO19**; **GPIO18** is reserved for the buzzer. Both firmware entry points use this pin mapping.
- The firmware controls one servo signal. Test each servo individually when diagnosing movement.
- Current settings are `setPeriodHertz(50)` and `attach(SERVO_PIN, 500, 2400)`, with commands of **0°** for locked and **90°** for unlocked. These pulse endpoints are project settings; check them against the specifications of the actual servo before calibration.
- The dashboard displays the **commanded angle**, not a measured shaft angle. If travel is short, first check the signal pin, power supply, common ground, and mechanical binding with the latch disconnected. Calibrate pulse endpoints for the actual unit if needed.

---

## 🌐 Web Admin Console Connection

### Player display

After connecting to `SafeChallenge` WiFi, players can open **http://192.168.4.1/play** on a phone or a shared display. This English-language page shows the current group, elapsed time, incorrect attempts, remaining attempts, and round result. It refreshes automatically, reports connection loss, and freezes the displayed time when the round is solved or failed. A centered result dialog appears for success or failure after all attempts are used. Dismiss it with OK or Escape; polling will not reopen it for the same result. It resets for the next observed round. Players submit their code using the physical switches and confirm button on the box.

The player page uses `/player-status`, which returns no answer key, and has no start/reset controls. Staff can open `/` or `/admin` and use the player-page link to open the display. These separate views do not add access control: the existing admin page and `/status` endpoint remain accessible to anyone on the device WiFi.

The player display also shows live **SW1–SW5 ON/OFF** indicators, highlighting active switches in green and listing which ones are ON. These reflect the physical inputs and refresh every 500 ms. If the connection is lost, the indicators show an unavailable state until status updates resume.

### Staff connection

1. Power on the ESP32 board via USB (5V).
2. Connect your device (laptop/phone) to the WiFi network:
   - **SSID**: `SafeChallenge`
   - **Password**: `safe12345`
3. Open any web browser and navigate to:
   ```text
   http://192.168.4.1
   ```
4. The web dashboard provides a live interface to monitor hardware, inspect answer keys, start rounds, and reset the vault.

---

## 🔑 Challenge Answer Key (Groups 1–20)

Target codes based on the Binary column below (`SW5` = MSB / Bit 4, `SW1` = LSB / Bit 0):

| Group  | Decimal Code | Binary (`SW5` `SW4` `SW3` `SW2` `SW1`) | Required Switches (ON) |
| :----: | :----------: | :------------------------------------: | :--------------------- |
| **1** | **4** | `0 0 1 0 0` | SW3 |
| **2** | **16** | `1 0 0 0 0` | SW5 |
| **3** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **4** | **1** | `0 0 0 0 1` | SW1 |
| **5** | **22** | `1 0 1 1 0` | SW5, SW3, SW2 |
| **6** | **28** | `1 1 1 0 0` | SW5, SW4, SW3 |
| **7** | **19** | `1 0 0 1 1` | SW5, SW2, SW1 |
| **8** | **7** | `0 0 1 1 1` | SW3, SW2, SW1 |
| **9** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **10** | **7** | `0 0 1 1 1` | SW3, SW2, SW1 |
| **11** | **4** | `0 0 1 0 0` | SW3 |
| **12** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **13** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **14** | **25** | `1 1 0 0 1` | SW5, SW4, SW1 |
| **15** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **16** | **4** | `0 0 1 0 0` | SW3 |
| **17** | **8** | `0 1 0 0 0` | SW4 |
| **18** | **21** | `1 0 1 0 1` | SW5, SW3, SW1 |
| **19** | **7** | `0 0 1 1 1` | SW3, SW2, SW1 |
| **20** | **28** | `1 1 1 0 0` | SW5, SW4, SW3 |

---

## 🧠 Challenge Logic & Algorithm

During the workshop challenge, contestants are provided with 4 decimal numbers. They convert each number to 4-bit binary (`bit3` down to `bit0`) and follow these boolean logic rules to derive the 5-bit password (`bit4` down to `bit0`):

1. Convert all 4 decimal values into 4-bit binary.
2. **Password bit0**: Extracted from `bit0` of the 1st number.
3. **Carry Forward #1**: Store `bit1` of the 1st number.
4. **Password bit1**: Evaluated as `(Carry Forward #1) AND (bit1 of 2nd number)`.
5. **Carry Forward #2**: Store `bit2` of the 2nd number.
6. **Password bit2**: Evaluated as `(Carry Forward #2) OR (bit2 of 3rd number)`.
7. **Carry Forward #3**: Store `bit3` of the 3rd number.
8. **Password bit3**: Evaluated as `(Carry Forward #3) AND (NOT of bit3 of 4th number)`.
9. **Password bit4 (Odd Parity)**: Set to `1` if the total count of `1`s in the first 4 password bits (`bit0..bit3`) is **even**; otherwise, set to `0`. This makes the total number of `1`s across all 5 password bits odd.

---

## 🛠️ Build and Upload

### Option 1: PlatformIO (Recommended)

1. Open the project folder `Neptunes-Vault` in **Visual Studio Code** with the PlatformIO extension installed.
2. Build the firmware:
   ```powershell
   pio run
   ```
3. Flash firmware to the ESP32:
   ```powershell
   pio run -t upload
   ```
4. Open the Serial Monitor (Baud rate: `115200`):
   ```powershell
   pio device monitor
   ```

### Option 2: Arduino IDE

1. Open the **Arduino IDE**.
2. Install the **esp32 by Espressif Systems** board package via the Boards Manager.
3. Install the **ESP32Servo** library via the Library Manager.
4. Open `Neptunes-Vault.ino`.
5. Select Board: **ESP32 Dev Module**.
6. Select your COM Port and click **Upload**.

---

## 📋 Staff Operation Workflow

### Start directly on the box (no dashboard required)

1. Start in `SETUP` (red and green LEDs alternate). After a finished round, hold Confirm for at least 2 seconds, then release to return to `SETUP`.
2. Set the group number using the switches: **SW1=1, SW2=2, SW3=4, SW4=8, SW5=16**. Add the values of the ON switches. For example, group 1 = SW1 only; group 3 = SW1 + SW2; group 20 = SW3 + SW5. ON means the GPIO is connected to GND.
3. Tap and release Confirm in less than 2 seconds. The servo locks and the round starts. All switches OFF selects 0, which is not a valid group; values 21–31 are also invalid.
4. Set the answer code on the same switches, then tap Confirm again to submit it.

**Group 2 example:** In `SETUP`, turn ON **SW2 only** (value 2) and tap Confirm to start. Once the round is `PLAYING`, turn ON **SW5 only** and turn all other switches OFF (answer 16) and tap Confirm to solve it. The group number and answer code are different inputs; using answer 16 to start would select group 16 instead of group 2.

If it does not start, open `pio device monitor` at 115200 baud. A detected press logs `[CONFIRM] GPIO33 pressed (LOW)`; releasing logs the duration and `switch_value`. No press log means GPIO33 is not detecting the button: check that the Confirm button connects **GPIO33 to GND** when pressed. A tap in `SETUP` uses `state=0`; a `switch_value` outside 1–20 must be corrected before starting. A long press resets instead of starting.

```mermaid
flowchart TD
    A[Power on ESP32] --> B[Enter SETUP state]
    B --> C[Servo UNLOCKED 90°: Staff place prizes / prep safe]
    C --> D[Staff connect to WiFi 'SafeChallenge']
    D --> E[Open browser at 192.168.4.1]
    E --> F[Enter Group Number 1-20 & Click 'Start Group']
    F --> G[Enter PLAYING state: Servo LOCKED 0° immediately]
    G --> H[Contestants set switches SW1-SW5 & Press Confirm]
    H --> I{Is code correct?}
    I -- YES --> J[SOLVED: Servo UNLOCKED 90° + Green LED + Victory Chime]
    I -- NO --> K[Wrong Count + 1]
    K --> L{Wrong Count >= 3?}
    L -- NO --> M[Red blink pulse + Error buzzer] --> H
    L -- YES --> N[FAILED: Servo remains LOCKED 0° + Solid Red LED + Alarm]
    J --> O[Staff click 'Reset to SETUP' on web or hold Confirm button >= 2s]
    N --> O
    O --> B
```

---

## 📁 Repository Structure

```text
Neptunes-Vault/
├── .pio/                     # PlatformIO build files and dependencies
├── include/                  # Custom C/C++ header files
├── lib/                      # Project-specific private libraries
├── src/
│   └── main.cpp              # Primary application source code (PlatformIO)
├── test/                     # Unit test scripts
├── .gitignore                # Git ignore rules
├── Neptunes-Vault.ino        # Arduino IDE compatible sketch
├── platformio.ini            # PlatformIO project configuration file
└── README.md                 # Project documentation
```
