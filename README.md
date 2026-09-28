# IRIS: Interactive Reactive Info Screen

IRIS is a small desk companion built on an ESP32-C3. It has an animated face on a 128x64 OLED that looks where you tilt it, blinks and glances on its own, gets dizzy and angry when shaken, gets suspicious when poked, and loves being petted. It also works as a clock, a weather display, and a timer, all controlled through a single touch sensor.

There is no companion app. WiFi is set up from your phone through a captive portal, and once IRIS is on your network it serves a small web dashboard you can use from any browser.

<!-- Add photos or a short GIF here, e.g. ![IRIS](docs/iris.gif) -->

## Contents

- [Features](#features)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Software setup](#software-setup)
- [Configuration](#configuration)
- [First boot and WiFi setup](#first-boot-and-wifi-setup)
- [Using IRIS](#using-iris)
- [Moods](#moods)
- [Dashboard and API](#dashboard-and-api)
- [How it works](#how-it-works)
- [Tuning](#tuning)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Security notes](#security-notes)
- [Project structure](#project-structure)
- [License](#license)

## Features

**Face and personality**
- Eyes are driven by a spring-damper physics model, so blinks, squash-and-stretch and pupil movement look organic instead of frame-by-frame.
- 12 moods: Normal, Happy, Surprised, Sleepy, Angry, Sad, Excited, Love, Suspicious, Dizzy, Gloomy, Recovering.
- Idle behavior with no input: random glances, spontaneous blinks, occasional head tilt, and a breathing bounce.
- Pupils and eye position follow the tilt of the device (MPU6050).
- Shake it and it goes Dizzy, then Angry, then Recovering, then back to its baseline mood. A gentle touch during the sequence calms it down early.
- Double-tap the face to poke it. The first two pokes make it suspicious, and a third within 3.5 seconds makes it angry.
- Hold the face to pet it and it switches to Love.

**Pages**
- Face, Clock (with a World Clock subpage), Weather (with a 3-day Forecast subpage), and Timer.
- A quick left-to-right wipe transition when you change pages.

**Weather**
- Current conditions and a 3-day forecast from OpenWeatherMap, refreshed every 10 minutes by a background task so the animation never stalls on the network.
- The weather sets IRIS's baseline mood (clear is happy, rain is sad, thunderstorm is surprised, and so on).
- Weather-change reactions: a temperature drop of 5 C or more makes it shiver, a rise of 5 C or more makes it wilt, and a thunderstorm starting makes it jump with surprise.

**Timer, Pomodoro and reminders**
- Timer presets of 5, 15, 30 and 60 minutes, selectable by touch or from the dashboard.
- Pomodoro mode (25 minutes work, 5 minutes break, repeating) started from the dashboard.
- Sticky-note reminders: type a short message and a time on the dashboard and IRIS pops it up on screen at that time.
- A particle "boom" celebration when a timer ends.

**Connectivity**
- Captive portal for WiFi setup with signal-strength bars, a password show/hide toggle and a Forget Saved Network button.
- Saved credentials live in flash. On boot, and on any later disconnect, IRIS scans first (including hidden networks). If your network is not detected in the initial scan pass, it automatically attempts directed probe connections. If connection fails repeatedly (3 attempts), it reopens the setup portal so you can move it to a new network without reflashing. Saving new credentials replaces the old ones.
- The setup access point is password protected.
- Permanent on-device web dashboard at the device's IP address, or `http://iris.local` where mDNS is supported.

**Display**
- Automatic brightness by time of day, using NTP-synced local time: dim from 23:00 to 06:00, medium from 06:00 to 08:00, full brightness otherwise.

## Hardware

### Bill of materials

| Part | Role |
|------|------|
| ESP32-C3 Super Mini | Main controller (single-core RISC-V, 2.4 GHz WiFi only) |
| 1.3" OLED display, SH1106, I2C, 128x64 | The face and all pages. I2C address `0x3C`. A 0.96" SSD1306 needs code changes |
| MPU6050 (GY-521 style breakout) | Tilt and shake sensing. I2C address `0x68` (`0x69` is also checked) |
| TTP223 capacitive touch module | The only input. Reads LOW at rest and HIGH while touched |
| TP4056 charging module (with battery protection) | Charges the LiPo from USB-C. Uses the `OUT+` and `OUT-` pads |
| 3.7 V LiPo battery, 1000 mAh | Power source |
| 4 mm slide switch | Main power switch on the battery output |
| Jumper wires, USB-C cable | |

There is no buzzer, LED or other sensor.

## Wiring

### Power

| From | Pin | To | Pin | Notes |
|------|-----|----|-----|-------|
| LiPo battery | + | TP4056 | B+ | Battery positive to the charger |
| LiPo battery | - | TP4056 | B- | Battery negative to the charger |
| USB-C cable | 5 V | TP4056 | IN+ / IN- | Charging input only |
| TP4056 | OUT+ | Slide switch | one terminal | Switched discharge line |
| Slide switch | other terminal | ESP32-C3 | 5V | Powers the board when the switch is ON |
| TP4056 | OUT- | ESP32-C3 | GND | Common ground |

```mermaid
flowchart LR
    USB["USB-C<br/>5 V charging input"]

    subgraph CHG["TP4056 charging module"]
        IN["IN+ / IN-"]
        BAT["B+ / B-"]
        OUT["OUT+ / OUT-"]
    end

    LIPO[("LiPo battery<br/>3.7 V, 1000 mAh")]
    SW{{"Slide switch<br/>main power ON / OFF"}}

    subgraph MCU["ESP32-C3 Super Mini"]
        V5["5V pin"]
        GND["GND"]
    end

    USB -->|"5 V"| IN
    BAT <-->|"charge / discharge"| LIPO
    OUT -->|"OUT+"| SW
    SW -->|"switched +"| V5
    OUT -.->|"OUT- (common ground)"| GND

    classDef source fill:#e8f1ff,stroke:#2563eb,stroke-width:2px,color:#0f172a
    classDef module fill:#f1f5f9,stroke:#475569,stroke-width:1.5px,color:#0f172a
    classDef battery fill:#fff4e0,stroke:#d97706,stroke-width:2px,color:#0f172a
    classDef switch fill:#fde8e8,stroke:#dc2626,stroke-width:2px,color:#0f172a
    classDef mcu fill:#e7f8ee,stroke:#16a34a,stroke-width:1.5px,color:#0f172a

    class USB source
    class IN,BAT,OUT module
    class LIPO battery
    class SW switch
    class V5,GND mcu
```

### Signals and peripherals

| From | Pin | To | ESP32-C3 pin | Notes |
|------|-----|----|--------------|-------|
| OLED (SH1106) | VCC | ESP32-C3 | 3V3 | Most 1.3" SH1106 modules run on 3.3 V. Check that yours is not a 5 V-only variant |
| OLED (SH1106) | GND | ESP32-C3 | GND | |
| OLED (SH1106) | SDA | ESP32-C3 | GPIO 6 | Shared I2C bus |
| OLED (SH1106) | SCL | ESP32-C3 | GPIO 7 | Shared I2C bus |
| MPU6050 | VCC | ESP32-C3 | 3V3 | |
| MPU6050 | GND | ESP32-C3 | GND | |
| MPU6050 | SDA | ESP32-C3 | GPIO 6 | Same bus as the OLED, address `0x68` |
| MPU6050 | SCL | ESP32-C3 | GPIO 7 | Same bus as the OLED, address `0x68` |
| TTP223 | VCC | ESP32-C3 | 3V3 | |
| TTP223 | GND | ESP32-C3 | GND | |
| TTP223 | OUT / SIG | ESP32-C3 | GPIO 4 | Digital read, matches `TOUCH_PIN` in the code |

The OLED and the MPU6050 share the same two I2C lines. The pin numbers are defined at the top of the sketch as `SDA_PIN`, `SCL_PIN` and `TOUCH_PIN` if you want to change them.

### Power notes

- **Check battery polarity twice** before connecting. A reversed LiPo can damage the module and is a fire risk.
- **Use the protected TP4056 variant.** The circuit above uses the `OUT+` and `OUT-` pads, which are present on the version with the DW01 protection chip. That version cuts the battery off on over-discharge and short circuit. The plain version does not.
- **Charge with the slide switch OFF.** A simple TP4056 board has no power-path management. If the load is running while it charges, the module may not detect end of charge correctly and the charge LED can be misleading.
- **Flash with the slide switch OFF.** When you plug the ESP32-C3's own USB-C port into a computer, turn the switch off so the battery circuit and USB are not both feeding the 5V pin.
- **The battery feeds the 5V pin.** The board's onboard 3.3 V regulator brings it down. A LiPo runs from about 4.2 V full to about 3.0 V empty, and depending on your board's regulator the ESP32 may brown out and reset before the battery is truly empty.
- **No battery gauge and no deep sleep.** The firmware does not read the battery voltage and never sleeps, so runtime is limited by the OLED and WiFi. Battery life has not been measured.

## Software setup

1. Install the Arduino IDE (or `arduino-cli`).
2. Add ESP32 board support (Espressif's `esp32` core, version 3.x was used during development).
3. Install these libraries from the Library Manager:
   - Adafruit GFX Library
   - Adafruit SH110X
   - MPU6050_tockn
   - Arduino_JSON
4. The remaining libraries (WiFi, WebServer, DNSServer, ESPmDNS, Preferences, HTTPClient, WiFiClientSecure) ship with the ESP32 core.
5. Put the sketch in a folder with the same name as the file: `iris_esp32/iris_esp32.ino`.
6. Select the board **ESP32C3 Dev Module** and set **USB CDC On Boot** to **Enabled** (required to see Serial output on the Super Mini).
7. Edit the settings below, then build and upload.

## Configuration

All user settings are constants near the top of `iris_esp32.ino`. **The API key and city are placeholders in this repository (`YOUR_API_KEY_HERE` and `YOUR_CITY_HERE`). Replace them before uploading.**

| Constant | Purpose |
|----------|---------|
| `OPENWEATHER_API_KEY` | Your own free API key from [openweathermap.org](https://openweathermap.org/api), replacing `YOUR_API_KEY_HERE`. Required for weather, the forecast, weather moods and weather reactions. Without it those features do not work, but the rest of IRIS still runs |
| `CITY` | Your city, replacing `YOUR_CITY_HERE`, e.g. `"London"` |
| `COUNTRY_CODE` | Two-letter country code for your city, e.g. `"GB"`. Check that it matches the city you set |
| `TIMEZONE` | POSIX timezone string, e.g. `"IST-5:30"` for India or `"EST5EDT,M3.2.0,M11.1.0"` for US Eastern. The sign is inverted in POSIX format, so UTC+5:30 is written `IST-5:30` |
| `AP_SETUP_PASSWORD` | Password for the `IRIS-Setup` access point. Defaults to `iris1234` so first-time setup is easy. Change it to anything from 8 to 63 characters |

New OpenWeatherMap keys can take a while to activate after you create them.

## First boot and WiFi setup

1. On first boot there are no saved credentials, so IRIS starts a WiFi access point named **IRIS-Setup**. The OLED shows the network name, password and address.
2. Join `IRIS-Setup` from your phone. The default password is `iris1234`, or whatever you set in `AP_SETUP_PASSWORD`. The password is also shown on the OLED.
3. A setup page should open automatically. If it does not, open `http://192.168.4.1` in a browser.
4. Pick your network from the list (bars show signal strength, `[####]` is strongest), enter the password, and tap **Save & Connect**.
5. IRIS restarts, connects, plays a short celebration, and shows its dashboard address for a few seconds.

To force setup mode later, hold the touch sensor for 1.5 seconds while powering on, or use **Forget WiFi & Reconfigure** on the dashboard. If your saved network disappears (you took IRIS somewhere else), it opens the setup portal on its own after retrying.

## Using IRIS

Everything is done with one touch sensor.

| Where | Gesture | What happens |
|-------|---------|--------------|
| Any page | Single tap | Next page: Face, Clock, Weather, Timer, back to Face. From a subpage, returns to the main page |
| Face | Double tap | Poke. Pokes 1 and 2 make it suspicious, a 3rd within 3.5 s makes it angry |
| Face | Hold 0.8 s to 2.5 s | Petting, switches to Love |
| Face | Hold 2.5 s or more | Mood picker: cycles through 7 moods while held, release to lock one in |
| Clock | Double tap or hold | Toggle World Clock (displays London, New York, and your configured city with dynamic 3-letter abbreviation) |
| Weather | Double tap or hold | Toggle 3-day Forecast |
| Timer (idle) | Double tap | Start the timer |
| Timer (idle) | Hold | Cycle presets: 5, 15, 30, 60 minutes |
| Timer (running) | Double tap | Stop the timer |
| Timer (running) | Hold 2.5 s | Cancel with a progress bar (safe against accidental stops) |
| Any non-face page | Hold 3 s | Go home to the Face |
| Any popup | Tap | Dismiss it |

Other interactions: shake the device for the Dizzy, Angry, Recovering sequence; tilt it and the eyes follow.

Selectable moods in the picker: Happy, Love, Sleepy, Excited, Gloomy, Sad, Normal. Picking one overrides the weather mood until you shake or otherwise reset it.

## Moods

| Trigger | Mood |
|---------|------|
| Clear weather | Happy |
| Clouds, mist, fog, haze, smoke, dust | Gloomy |
| Rain, drizzle | Sad |
| Thunderstorm | Surprised |
| Snow | Sleepy |
| Any other condition above 35 C | Angry |
| Any other condition below 5 C | Sleepy |
| Anything else | Normal |
| Shake | Dizzy, then Angry, then Recovering |
| Pokes | Suspicious, then Angry |
| Petting | Love |
| Mood picker or dashboard | Whichever you choose |

## Dashboard and API

Once connected, open the address IRIS shows on boot, or `http://iris.local`. The dashboard lets you check status, set a mood, start or stop the timer, start Pomodoro, set a reminder, and forget the WiFi network. It refreshes every 3 seconds.

You can also drive it from scripts. Replace `IRIS_IP` with the device address.

| Method | Path | Parameters | Description |
|--------|------|------------|-------------|
| GET | `/` | none | Dashboard page |
| GET | `/api/state` | none | Current status as JSON |
| POST | `/api/mood` | `mood` (Happy, Love, Sleepy, Excited, Gloomy, Sad, Normal) | Set a mood |
| POST | `/api/timer` | `action` = `start`, `stop` or `pomodoro_start`; optional `minutes` (1 to 180) for `start` | Control the timer |
| POST | `/api/reminder` | `text` (max 40 chars) and `time` (`HH:MM`), or `clear=1` | Set or clear the reminder |
| POST | `/api/forget-wifi` | none | Erase saved WiFi and reboot into setup mode |

```bash
# Status
curl http://IRIS_IP/api/state

# Make IRIS happy
curl -X POST -d "mood=Happy" http://IRIS_IP/api/mood

# 15 minute timer
curl -X POST -d "action=start&minutes=15" http://IRIS_IP/api/timer

# Reminder at 16:30
curl -X POST --data-urlencode "text=Stand up and stretch" -d "time=16:30" http://IRIS_IP/api/reminder
```

`/api/state` returns fields such as `mood`, `tempC`, `weather`, `timerActive`, `timerRemainingSec`, `pomodoroActive`, `pomodoroPhase`, `reminderArmed`, `time`, `ssid` and `rssi`.

## How it works

**Rendering.** The main `loop()` reads touch and motion, services the web server, updates eye physics, and draws one frame at roughly 60 fps. Only this loop ever touches the display. The ESP32-C3 is single-core, so the design avoids two tasks sharing the I2C bus.

**Weather task.** A FreeRTOS task fetches current weather and the 5-day forecast over HTTPS every 10 minutes and writes the result under a mutex. It never draws anything. When a reading changes meaningfully it sets a flag, and the main loop plays the matching reaction. Reactions only play when IRIS is idle on the Face page. Otherwise they are dropped rather than queued.

**WiFi state machine.** Connecting is non-blocking: scan for the saved SSID (including hidden networks), connect, and retry. If a scan pass misses the beacon or the network is hidden, IRIS attempts directed probe connection before reopening the portal. A visible or probed SSID that fails to connect is retried up to 3 times (15 s timeout, 8 s between attempts) before the portal reopens.

**Page transitions.** The old and new frames are captured through `getPixel()` into 1 KB buffers, then composited in 6 steps. This uses only the public Adafruit GFX API, so it does not depend on the display driver's internal buffer layout. Most of the time cost is the I2C push of each frame.

**Storage.** WiFi credentials are stored with `Preferences` (NVS) under the `iris-wifi` namespace. Timer presets, Pomodoro state and reminders live in RAM only.

## Tuning

| Setting | Where | Effect |
|---------|-------|--------|
| `SHAKE_THRESHOLD` | top of sketch | Sensitivity of shake detection |
| `WIFI_TIMEOUT`, `WIFI_RETRY_INTERVAL`, `WIFI_MAX_ATTEMPTS_BEFORE_PORTAL` | WiFi section | How long IRIS tries a saved network before reopening the portal |
| `WEATHER_REACTION_TEMP_DELTA` | weather section | Temperature swing that triggers a reaction (default 5 C) |
| `WEATHER_UPDATE_INTERVAL` | weather section | Weather refresh period (default 10 minutes) |
| `ENABLE_MINUTE_CLOCK_POPUP` | clock section | Brief 3-second time popup on every minute change (default `false`) |
| `TIMER_PRESETS_MIN` | timer section | Preset durations |
| `POMODORO_WORK_MS`, `POMODORO_BREAK_MS` | timer section | Pomodoro lengths |
| `STEPS` in `playPageTransition()` | rendering section | Fewer steps is faster, more is smoother |
| Contrast levels | `updateDisplayContrast()` | Night, morning and day brightness |

## Troubleshooting

**IRIS does not turn on from the battery.** Check that the slide switch is ON, the battery is charged, and the switch is wired between `OUT+` and the ESP32-C3 `5V` pin. Confirm battery polarity at the TP4056 `B+` and `B-` pads.

**IRIS resets or reboots on battery.** The battery is probably low. See the power notes in the Wiring section.

**Blank screen.** Check the wiring and that your OLED is an SH1106 at `0x3C`. Serial prints a note if the display is not found.

**"MPU6050 NOT FOUND" on screen.** Check SDA and SCL wiring and power. The sketch looks for the sensor at `0x68` and `0x69`.

**No Serial output.** Enable **USB CDC On Boot** in the Arduino IDE board settings and use 115200 baud.

**The setup page does not open automatically.** Go to `http://192.168.4.1` manually. Some phones need mobile data turned off while joining a network without internet.

**Cannot reach the dashboard.** The phone or computer must be on the same WiFi as IRIS. The address is shown on the OLED at first connect and printed to Serial. `iris.local` depends on mDNS support, which some Android versions lack, so use the IP address if it does not resolve.

**IRIS keeps opening the setup portal.** Either the saved network is out of range or the password is wrong. The ESP32-C3 supports 2.4 GHz only, so a 5 GHz-only network will never be visible.

**Weather does not update.** Confirm you replaced `YOUR_API_KEY_HERE` and `YOUR_CITY_HERE`, that the key is valid and activated (new OpenWeatherMap keys can take a while to work), and that `COUNTRY_CODE` matches your city.

**`undefined reference to setup()` or `loop()` at link time.** Arduino's automatic prototype generator can misfire on complex sketches. This sketch declares its functions explicitly to avoid it. If you add new functions and see this error, add forward declarations for them next to the existing block.

## Known limitations

- The dashboard has no authentication and uses plain HTTP. Anyone on your local network can control IRIS.
- Reminders, timer presets and Pomodoro state are not saved across reboots.
- Long animations (weather reactions, page transitions, the boom) briefly block the main loop, so the dashboard can respond a little slower during them.
- Weather-change reactions are skipped if IRIS is busy with something else when the reading arrives.
- Page transitions apply to single-tap navigation only. Switching subpages is instant.

## Security notes

- The OpenWeatherMap API key in this repository is a placeholder. Use your own, and do not commit it if you publish a fork. A `secrets.h` file listed in `.gitignore` is a simple way to keep it out.
- The default `AP_SETUP_PASSWORD` (`iris1234`) is published here on purpose so first-time setup is easy. It only matters while the setup portal is open: on first boot, after forgetting the network, or when the saved network is out of range. If that matters to you, change the constant before flashing.
- WiFi credentials are stored unencrypted in the ESP32's NVS flash.
- The dashboard has no login and uses plain HTTP (see Known limitations).

## Project structure

```
iris_esp32/
  iris_esp32.ino   # entire firmware: eye engine, moods, touch, weather, WiFi, portal, dashboard
README.md
```

## License

MIT. Add a `LICENSE` file to the repository.

## Author

Shreyans Jain
