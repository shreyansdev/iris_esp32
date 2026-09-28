// ==================================================
// IRIS - PERFECT EYES & SMALL MOUTH + MPU6050 MOTION
// Features: FAST 3D eye tracking, Shake = Dizzy then Angry automatically
// ==================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "time.h"
#include <math.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include <MPU6050_tockn.h>
MPU6050 mpu6050(Wire);

// ==================================================
// CONFIGURATION & OPENWEATHER API
// ==================================================
const char* OPENWEATHER_API_KEY = "YOUR_API_KEY_HERE";
const char* CITY = "YOUR_CITY_HERE";
const char* COUNTRY_CODE = "IN";
const char* TIMEZONE = "IST-5:30";  // India Standard Time (UTC+5:30)

// WiFi Credentials managed via Preferences & Captive Portal
Preferences preferences;
String wifiSsid = "";
String wifiPassword = "";

// Password for the "IRIS-Setup" access point itself (WPA2-PSK requires 8-63 chars).
// Without this, anyone in range could join the setup AP and read/replace your saved WiFi.
const char* AP_SETUP_PASSWORD = "iris1234";

// Captive Portal State
bool inPortalMode = false;
DNSServer dnsServer;
WebServer server(80);
const byte DNS_PORT = 53;

// On-device Dashboard: unlike the captive portal (only up during setup), this stays
// running for the device's entire normal operation once connected to home WiFi.
// Guarded by this flag so repeated WiFi reconnects don't re-register routes / re-begin().
bool dashboardStarted = false;

// ==================================================
// HARDWARE CONFIGURATION
// ==================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 6
#define SCL_PIN 7
#define TOUCH_PIN 4

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==================================================
// MPU6050 MOTION & EMOTION VARIABLES
// ==================================================
#define CALIBRATION_SAMPLES 100
float calibAngleX = 0, calibAngleY = 0;
bool isCalibrated = false;

float currentRoll = 0, currentPitch = 0;
float shakeIntensity = 0;

// Shake detection threshold:
// Accelerometer delta (in Gs) scaled by 10 across all 3 axes: (|dAx| + |dAy| + |dAz|) * 10.
// Threshold of 20.0 corresponds to an aggregate instantaneous acceleration shift of > 2.0g,
// which represents a firm, deliberate physical shake rather than gentle table bumps or normal handling.
const float SHAKE_THRESHOLD = 20.0f;

// Multi-stage Shake Recovery: Dizzy (2.2s) -> Angry (3.2s) -> Dazed Recovery (3.0s) -> Weather Baseline
const int SHAKE_DURATION    = 2200;
const int ANGRY_DURATION    = 3200;
const int RECOVERY_DURATION = 3000;

bool isShaking = false;
bool isAngry = false;
bool isRecovering = false;
unsigned long shakeStartTime = 0;
unsigned long shakeEndTime = 0;
unsigned long angryEndTime = 0;
unsigned long recoveryEndTime = 0;

// Touch Emotion Escalation: Quick Poke = Suspicious; Rapid Multi-touch = Angry
int pokeCount = 0;
unsigned long lastPokeTime = 0;
bool isSuspicious = false;
unsigned long suspiciousEndTime = 0;
const unsigned long SUSPICIOUS_DURATION = 2500;
bool isTouchAngry = false;
unsigned long touchAngryEndTime = 0;
const unsigned long TOUCH_ANGRY_DURATION = 3500;

float targetPupilX = 0, targetPupilY = 0;
float currentPupilX = 0, currentPupilY = 0;

float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 0;

// ==================================================
// WEATHER ICONS
// ==================================================
const unsigned char bmp_clear[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,
  0x00,0x00,0x00,0x00,0x01,0x03,0xc0,0x80,0x00,0x0f,0xf0,0x00,0x00,0x3f,0xfc,0x00,
  0x00,0x7f,0xfe,0x00,0x00,0xff,0xff,0x00,0x06,0xff,0xff,0x60,0x06,0xff,0xff,0x60,
  0x06,0xff,0xff,0x60,0x00,0xff,0xff,0x00,0x3e,0xff,0xff,0x7c,0x3e,0xff,0xff,0x7c,
  0x3e,0xff,0xff,0x7c,0x00,0xff,0xff,0x00,0x06,0xff,0xff,0x60,0x06,0xff,0xff,0x60,
  0x06,0xff,0xff,0x60,0x00,0xff,0xff,0x00,0x00,0x7f,0xfe,0x00,0x00,0x3f,0xfc,0x00,
  0x01,0x0f,0xf0,0x80,0x00,0x03,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,
  0x00,0x01,0x80,0x00,0x00,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_clouds[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xe0,0x00,
  0x00,0x0f,0xf8,0x00,0x00,0x1f,0xfc,0x00,0x00,0x3f,0xfe,0x00,0x00,0x3f,0xff,0x00,
  0x00,0x7f,0xff,0x80,0x00,0xff,0xff,0xc0,0x00,0xff,0xff,0xe0,0x01,0xff,0xff,0xf0,
  0x03,0xff,0xff,0xf8,0x07,0xff,0xff,0xfc,0x07,0xff,0xff,0xfc,0x0f,0xff,0xff,0xfe,
  0x0f,0xff,0xff,0xfe,0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,
  0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x0f,0xff,0xff,0xfe,
  0x07,0xff,0xff,0xfc,0x03,0xff,0xff,0xf8,0x00,0xff,0xff,0xe0,0x00,0x3f,0xff,0x80,
  0x00,0x0f,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_rain[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x03,0xe0,0x00,0x00,0x0f,0xf8,0x00,0x00,0x1f,0xfc,0x00,
  0x00,0x3f,0xfe,0x00,0x00,0x7f,0xff,0x80,0x00,0xff,0xff,0xc0,0x01,0xff,0xff,0xf0,
  0x03,0xff,0xff,0xf8,0x07,0xff,0xff,0xfc,0x0f,0xff,0xff,0xfe,0x1f,0xff,0xff,0xff,
  0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0x0f,0xff,0xff,0xfe,
  0x07,0xff,0xff,0xfc,0x03,0xff,0xff,0xf8,0x00,0xff,0xff,0xe0,0x00,0x3f,0xff,0x80,
  0x00,0x0f,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x0c,0x00,0x00,0x60,0x0c,0x00,
  0x00,0xe0,0x1c,0x00,0x00,0xc0,0x18,0x00,0x03,0x80,0x70,0x00,0x03,0x80,0x70,0x00,
  0x03,0x00,0x60,0x00,0x02,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char mini_sun[] PROGMEM = {
  0x00,0x00,0x01,0x80,0x00,0x00,0x10,0x08,0x04,0x20,0x03,0xc0,0x27,0xe4,0x07,0xe0,
  0x07,0xe0,0x27,0xe4,0x03,0xc0,0x04,0x20,0x10,0x08,0x00,0x00,0x01,0x80,0x00,0x00
};
const unsigned char mini_cloud[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x01,0xc0,0x07,0xe0,0x0f,0xf0,0x1f,0xf8,0x1f,0xf8,0x3f,0xfc,
  0x3f,0xfc,0x7f,0xfe,0x3f,0xfe,0x1f,0xfc,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char mini_rain[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x01,0xc0,0x07,0xe0,0x0f,0xf0,0x1f,0xf8,0x1f,0xf8,0x3f,0xfc,
  0x3f,0xfc,0x7f,0xfe,0x3f,0xfe,0x1f,0xfc,0x00,0x00,0x44,0x44,0x22,0x22,0x11,0x11
};
const unsigned char bmp_tiny_drop[] PROGMEM = { 0x10,0x38,0x7c,0xfe,0xfe,0x7c,0x38,0x00 };
const unsigned char bmp_dizzy_stars[] PROGMEM = {
  0x08,0x20,0x14,0x50,0x22,0x88,0x41,0x04,0x82,0x02,0x41,0x04,0x22,0x88,0x14,0x50,
  0x08,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_angry_mark[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x08,0x00,0x1c,0x00,0x3e,0x00,0x7f,0x00,0x3e,0x00,0x1c,0x00,
  0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// ==================================================
// EMOTION PARTICLES
// ==================================================
const unsigned char bmp_heart[] PROGMEM = {
  0x00,0x00,0x0c,0x60,0x1e,0xf0,0x3f,0xf8,0x7f,0xfc,0x7f,0xfc,0x7f,0xfc,0x3f,0xf8,
  0x1f,0xf0,0x0f,0xe0,0x07,0xc0,0x03,0x80,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_zzz[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,0x00,0x30,0x00,0x7e,
  0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,0x00,0x30,0x00,0x7c,0x00,0x00,0x00,0x00,0x00
};
const unsigned char bmp_exclamation[] PROGMEM = {
  0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00
};
const unsigned char bmp_question[] PROGMEM = {
  0x3c, 0x66, 0x06, 0x0c, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00
};
const unsigned char mini_hourglass[] PROGMEM = {
  0x7c, 0x44, 0x28, 0x10, 0x28, 0x44, 0x7c
};

// ==================================================
// GLOBALS
// ==================================================
int currentPage = 0;
int subPage = 0;
int tapCounter = 0;
unsigned long lastTapTime = 0;
bool lastPinState = false;
unsigned long pressStartTime = 0;
bool isLongPressHandled = false;
const unsigned long LONG_PRESS_TIME = 800;
const unsigned long TAP_REGISTER_DELAY = 300; // Time window (ms) to accumulate short taps before committing:
                                               // - Single-tap universally advances pages (0 -> 1 -> 2 -> 3 -> 0).
                                               // - Double-tap on Face pokes IRIS (escalating 1-2 Suspicious, 3+ Angry).
                                               // - Double-tap on Timer starts/stops countdown.

// Page transition: set whenever a tap changes what's on screen (page or subpage), so the
// next render does a quick left-to-right wipe instead of an instant cut. See playPageTransition().
bool pageTransitionPending = false;

#define MOOD_NORMAL     0
#define MOOD_HAPPY      1
#define MOOD_SURPRISED  2
#define MOOD_SLEEPY     3
#define MOOD_ANGRY      4
#define MOOD_SAD        5
#define MOOD_EXCITED    6
#define MOOD_LOVE       7
#define MOOD_SUSPICIOUS 8
#define MOOD_DIZZY      9
#define MOOD_GLOOMY     10
#define MOOD_RECOVERING 11

int currentMood = MOOD_HAPPY;
int weatherMood = MOOD_HAPPY;
bool moodManualOverride = false; // true once the user long-touches to pick a mood by hand;
                                  // stops updateWeatherMood() from silently reverting it
unsigned long moodChangeStartTime = 0;

// Idle Head-Tilt & Personality micro-gestures
float headTiltOffsetY = 0;
float targetHeadTiltY = 0;
unsigned long nextHeadTiltTime = 0;

// Pick-up / approach motion detection
unsigned long lastStationaryTime = 0;
bool wasStationary = false;
int doubleBlinkCount = 0;

// Interaction tracking for idle popup guards
unsigned long lastUserInteractionTime = 0;

// Gentle Touch / Petting State
bool isPetting = false;
unsigned long loveEndTime = 0;

// Manual Mood Selector State (cycles moods on hold > 2.5s on Face)
bool isMoodSelecting = false;
int selectedMoodIndex = 0;
unsigned long lastMoodCycleTime = 0;
const int SELECTABLE_MOODS[] = {
  MOOD_HAPPY, MOOD_LOVE, MOOD_SLEEPY, MOOD_EXCITED, MOOD_GLOOMY, MOOD_SAD, MOOD_NORMAL
};
const char* const MOOD_NAMES[] = {
  "HAPPY", "LOVE", "SLEEPY", "EXCITED", "GLOOMY", "SAD", "NORMAL"
};
const int NUM_SELECTABLE_MOODS = 7;

// Timer Cancellation Safety (hold >= 2.5s on Timer page to cancel)
bool isTimerCancelling = false;
const unsigned long TIMER_CANCEL_HOLD_TIME = 2500;

// ==================================================
// WIFI & WEATHER STATE MACHINE
// ==================================================
enum WifiState {
  WIFI_STATE_DISCONNECTED,
  WIFI_STATE_SCANNING,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_FAILED
};
WifiState wifiState = WIFI_STATE_DISCONNECTED;
unsigned long wifiConnectStartTime = 0;
unsigned long lastWifiRetryTime = 0;
const unsigned long WIFI_TIMEOUT = 15000;        // 15s connection timeout per attempt
const unsigned long WIFI_RETRY_INTERVAL = 8000;  // Wait 8s between consecutive retry attempts

unsigned long wifiScanStartTime = 0;
const unsigned long WIFI_SCAN_TIMEOUT = 10000; // safety cap in case the async scan never reports back

// Auto-Reprovisioning: if the saved network is out of range / unreachable, IRIS will
// retry it a limited number of times and then automatically reopen the "IRIS-Setup"
// captive portal so a different WiFi network can be entered. Saving new credentials
// in the portal replaces the old ones (see handlePortalSave), and the same escalation
// logic applies again if the new network also turns out to be unreachable.
int wifiFailedAttempts = 0;
const int WIFI_MAX_ATTEMPTS_BEFORE_PORTAL = 3; // consecutive failed attempts tolerated before reopening the AP

bool isWeatherPopupActive = false;
unsigned long weatherPopupEndTime = 0;
const unsigned long WEATHER_POPUP_DURATION = 20000;   // Display weather for 20 seconds
const unsigned long WEATHER_UPDATE_INTERVAL = 600000; // Fetch fresh weather every 10 minutes

// ==================================================
// MINUTE ROLLOVER & 30-MIN TIMER
// ==================================================
int lastObservedMinute = -1;
bool isMinutePopupActive = false;
unsigned long minutePopupEndTime = 0;
const unsigned long MINUTE_POPUP_DURATION = 3000; // 3 seconds on minute change

bool isTimerActive = false;
unsigned long timerStartTime = 0;
unsigned long timerDuration = 30UL * 60UL * 1000UL; // default 30 mins (1,800,000 ms)
unsigned long timerEndTime = 0;

// Configurable timer duration presets. Selected either from the on-device Dashboard,
// or by holding on the *idle* Timer page (800ms+, cycles every 600ms while held).
const int TIMER_PRESETS_MIN[] = { 5, 15, 30, 60 };
const int NUM_TIMER_PRESETS = 4;
int selectedPresetIndex = 2; // index into TIMER_PRESETS_MIN — starts at 30 min, matching the old default
bool isPresetSelecting = false;
unsigned long lastPresetCycleTime = 0;

// Pomodoro mode: layers work/break cycling on top of the existing timer machinery above.
// Controlled from the Dashboard (see handleApiTimer). When active, the normal timer-completion
// handling in loop() auto-advances to the next phase once the boom celebration finishes,
// instead of just stopping.
bool pomodoroActive = false;
int pomodoroPhase = 0; // 0 = work, 1 = break
int pomodoroCyclesCompleted = 0;
bool pomodoroAdvancePending = false;
const unsigned long POMODORO_WORK_MS  = 25UL * 60UL * 1000UL;
const unsigned long POMODORO_BREAK_MS = 5UL  * 60UL * 1000UL;

// Sticky-note reminder: a short text + time-of-day set via the Dashboard. Pops up on the
// Face page the moment local time reaches it, the same way the weather popup already does,
// then auto-clears itself so it won't re-fire again until a new one is set.
String reminderText = "";
int reminderHour = -1;   // -1 = no reminder armed
int reminderMinute = 0;
bool reminderArmed = false;   // true once set, until it fires (or is cleared)
bool isReminderPopupActive = false;
unsigned long reminderPopupEndTime = 0;
const unsigned long REMINDER_POPUP_DURATION = 15000; // 15 seconds on screen

// Non-blocking Boom animation state
bool isBoomActive = false;
unsigned long boomStartTime = 0;
struct BlastParticle { float x, y, vx, vy; };
BlastParticle boomParticles[24];

float temperature = 0.0;
float feelsLike = 0.0;
int humidity = 0;
String weatherMain = "Clear";
String weatherDesc = "Sunny";

// Weather-change reactions: fires a one-off animation the moment a NEW reading differs
// meaningfully from the PREVIOUS one, instead of only ever reflecting the current state.
// Set (under weatherMutex) by fetchWeatherAndForecastHttps() on the background task;
// consumed by loop() on the main task, which is the only code allowed to touch the display.
bool weatherBaselineEstablished = false; // guards against reacting to the very first-ever reading
#define WEATHER_REACTION_NONE           0
#define WEATHER_REACTION_TEMP_DROP      1
#define WEATHER_REACTION_TEMP_RISE      2
#define WEATHER_REACTION_STORM_STARTED  3
const float WEATHER_REACTION_TEMP_DELTA = 5.0; // deg C change that counts as "meaningful"
volatile int pendingWeatherReaction = WEATHER_REACTION_NONE;

struct ForecastDay {
  String dayName;
  int temp;
  String iconType;
};
ForecastDay fcast[3];

// FreeRTOS background task & synchronization for non-blocking HTTPS weather fetch
TaskHandle_t weatherTaskHandle = NULL;
SemaphoreHandle_t weatherMutex = NULL;
volatile bool weatherDataReady = false;

// ==================================================
// EYE PHYSICS ENGINE
// ==================================================
struct Eye {
  float x, y, w, h;
  float targetX, targetY, targetW, targetH;
  float pupilX, pupilY, targetPupilX, targetPupilY;
  float velX, velY, velW, velH, pVelX, pVelY;
  float k = 0.28, d = 0.60, pk = 0.30, pd = 0.55;
  int blinkState; // 0 = open, 1 = closing, 2 = opening
  unsigned long blinkStartTime;
  unsigned long nextBlinkTime;

  void init(float _x, float _y, float _w, float _h) {
    x = targetX = _x; y = targetY = _y;
    w = targetW = _w; h = targetH = _h;
    pupilX = targetPupilX = 0; pupilY = targetPupilY = 0;
    velX = velY = velW = velH = pVelX = pVelY = 0;
    blinkState = 0;
    blinkStartTime = 0;
    nextBlinkTime = millis() + random(1500, 4000);
  }

  void update() {
    float ax = (targetX - x) * k; float ay = (targetY - y) * k;
    float aw = (targetW - w) * k; float ah = (targetH - h) * k;
    velX = (velX + ax) * d; velY = (velY + ay) * d;
    velW = (velW + aw) * d; velH = (velH + ah) * d;
    x += velX; y += velY; w += velW; h += velH;

    if (h < 2.0) h = 2.0;

    float pax = (targetPupilX - pupilX) * pk;
    float pay = (targetPupilY - pupilY) * pk;
    pVelX = (pVelX + pax) * pd; pVelY = (pVelY + pay) * pd;
    pupilX += pVelX; pupilY += pVelY;
  }
};

Eye leftEye, rightEye;
float breathVal = 0;
unsigned long squashStretchEndTime = 0;

// Squash and stretch character physics (horizontal stretch + vertical squash for bouncy organic response)
void triggerSquashStretch(unsigned long durationMs = 150) {
  leftEye.targetW = rightEye.targetW = 38.0; // horizontal stretch
  leftEye.targetH = rightEye.targetH = 24.0; // vertical compress
  squashStretchEndTime = millis() + durationMs;
}

// Trigger an immediate coordinated eye blink
void triggerBlink() {
  leftEye.blinkState = rightEye.blinkState = 1; // 1 = closing
  leftEye.blinkStartTime = rightEye.blinkStartTime = millis();
}

// Trigger an inquisitive / surprised rapid double blink
void triggerDoubleBlink() {
  triggerBlink();
  doubleBlinkCount = 1;
}

// Switch mood with a smooth coordinated blink transition and cute squash/stretch settle
void setMood(int newMood, bool forceBlink = true) {
  if (currentMood != newMood) {
    currentMood = newMood;
    moodChangeStartTime = millis();
    triggerSquashStretch(150);
    if (forceBlink && !isBoomActive) {
      triggerBlink();
    }
  }
}

// ==================================================
// MPU6050 FUNCTIONS
// ==================================================
void calibrateMPU6050() {
  // Check if MPU6050 responds on I2C address 0x68 or 0x69
  Wire.beginTransmission(0x68);
  byte error = Wire.endTransmission();
  if (error != 0) {
    Wire.beginTransmission(0x69);
    error = Wire.endTransmission();
  }

  if (error != 0) {
    Serial.println("MPU6050 not detected on I2C bus! Check SDA/SCL wiring.");
    display.clearDisplay();
    display.setFont(NULL);
    display.setCursor(10, 20);
    display.print("MPU6050 NOT FOUND!");
    display.setCursor(10, 36);
    display.print("Check SDA/SCL pins");
    display.display();
    delay(2000);
    isCalibrated = false;
    return;
  }

  display.clearDisplay();
  display.setFont(NULL);
  display.setCursor(20, 20);
  display.print("Calibrating...");
  display.setCursor(10, 35);
  display.print("Keep device STILL");
  display.display();

  float sumX = 0, sumY = 0;
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    mpu6050.update();
    sumX += mpu6050.getAngleX();
    sumY += mpu6050.getAngleY();
    delay(10);
  }
  calibAngleX = sumX / CALIBRATION_SAMPLES;
  calibAngleY = sumY / CALIBRATION_SAMPLES;
  isCalibrated = true;

  display.clearDisplay();
  display.setCursor(25, 30);
  display.print("Calibrated!");
  display.display();
  delay(1000);
}

// ==================================================
// LIVING AUTONOMOUS SACCADE & GAZE ENGINE
// ==================================================
float autoGazeX = 0, autoGazeY = 0;
float targetGazeX = 0, targetGazeY = 0;
unsigned long nextSaccadeTime = 0;

void updateAutonomousGaze() {
  unsigned long now = millis();
  
  // During dizzy or post-shake anger or sleeping, spontaneous saccades pause
  if (isShaking || isAngry || currentMood == MOOD_DIZZY) return;

  if (now >= nextSaccadeTime) {
    if (currentMood == MOOD_SUSPICIOUS) {
      // Dart sideways towards who touched it!
      targetGazeX = (random(0, 2) == 0) ? -10 : 10;
      targetGazeY = random(-2, 3);
      nextSaccadeTime = now + random(1800, 2800);
    } else if (currentMood == MOOD_SAD || currentMood == MOOD_GLOOMY) {
      // Downward melancholic wandering gaze
      targetGazeX = random(-6, 7);
      targetGazeY = random(4, 9);
      nextSaccadeTime = now + random(2500, 4500);
    } else if (currentMood == MOOD_SLEEPY) {
      // Heavy downward droop
      targetGazeX = random(-3, 4);
      targetGazeY = random(6, 10);
      nextSaccadeTime = now + random(3000, 5000);
    } else if (currentMood == MOOD_RECOVERING) {
      // Weak slow drifting as it recovers
      targetGazeX = random(-5, 6);
      targetGazeY = random(-2, 4);
      nextSaccadeTime = now + random(1500, 2500);
    } else if (currentMood == MOOD_EXCITED) {
      // High-energy darting in all directions
      int r = random(0, 5);
      if (r == 0)      { targetGazeX = -9; targetGazeY = -4; }
      else if (r == 1) { targetGazeX = 9;  targetGazeY = -4; }
      else if (r == 2) { targetGazeX = -8; targetGazeY = 4;  }
      else if (r == 3) { targetGazeX = 8;  targetGazeY = 4;  }
      else             { targetGazeX = 0;  targetGazeY = -2; }
      nextSaccadeTime = now + random(700, 1400);
    } else {
      // MOOD_HAPPY / MOOD_NORMAL: Natural lifelike eye saccades
      int r = random(0, 10);
      if (r < 4) {
        // Look center / forward
        targetGazeX = 0;
        targetGazeY = 0;
      } else if (r == 4) {
        // Glance left
        targetGazeX = random(-10, -6);
        targetGazeY = random(-3, 4);
      } else if (r == 5) {
        // Glance right
        targetGazeX = random(6, 11);
        targetGazeY = random(-3, 4);
      } else if (r == 6) {
        // Look up-left
        targetGazeX = random(-8, -4);
        targetGazeY = random(-8, -3);
      } else if (r == 7) {
        // Look up-right
        targetGazeX = random(4, 9);
        targetGazeY = random(-8, -3);
      } else if (r == 8) {
        // Look down
        targetGazeX = random(-4, 5);
        targetGazeY = random(5, 9);
      } else {
        // Subtle drift
        targetGazeX = random(-4, 5);
        targetGazeY = random(-3, 4);
      }
      nextSaccadeTime = now + random(1400, 3200);
    }
  }

  // Smooth biological filtering towards target gaze
  autoGazeX = autoGazeX * 0.75 + targetGazeX * 0.25;
  autoGazeY = autoGazeY * 0.75 + targetGazeY * 0.25;
}

void updateMotion() {
  if (!isCalibrated) return;
  mpu6050.update();

  currentRoll  = mpu6050.getAngleX() - calibAngleX;
  currentPitch = mpu6050.getAngleY() - calibAngleY;

  float accelX = mpu6050.getAccX();
  float accelY = mpu6050.getAccY();
  float accelZ = mpu6050.getAccZ();

  float deltaX = fabs(accelX - lastAccelX);
  float deltaY = fabs(accelY - lastAccelY);
  float deltaZ = fabs(accelZ - lastAccelZ);
  shakeIntensity = (deltaX + deltaY + deltaZ) * 10;

  // Trigger shake reaction
  if (shakeIntensity > SHAKE_THRESHOLD && !isShaking && !isAngry && !isRecovering) {
    if (isWeatherPopupActive) isWeatherPopupActive = false;
    isSuspicious = false;
    isTouchAngry = false;
    pokeCount = 0;
    isPetting = false;
    isMoodSelecting = false;
    currentPage = 0;
    subPage = 0;

    isShaking = true;
    shakeStartTime = millis();
    shakeEndTime = millis() + SHAKE_DURATION;
    setMood(MOOD_DIZZY);
  }

  // State 1 -> State 2: Dizzy ends -> Outraged Angry
  if (isShaking && millis() >= shakeEndTime) {
    isShaking = false;
    isAngry = true;
    angryEndTime = millis() + ANGRY_DURATION;
    setMood(MOOD_ANGRY);
  }

  // State 2 -> State 3: Angry ends -> Dazed Recovery / Calming down
  if (isAngry && millis() >= angryEndTime) {
    isAngry = false;
    isRecovering = true;
    recoveryEndTime = millis() + RECOVERY_DURATION;
    setMood(MOOD_RECOVERING);
  }

  // State 3 -> State 4: Recovery ends -> Returns to weather baseline (or manual override)
  if (isRecovering && millis() >= recoveryEndTime) {
    isRecovering = false;
    if (!moodManualOverride) {
      setMood(weatherMood);
    }
  }

  // Touch Suspicious timeout
  if (isSuspicious && millis() >= suspiciousEndTime) {
    isSuspicious = false;
    if (!isTouchAngry && !isShaking && !isAngry && !isRecovering && !moodManualOverride && loveEndTime == 0) {
      setMood(weatherMood);
    }
  }

  // Touch Angry timeout -> Soft 2s recovery then back to weather baseline
  if (isTouchAngry && millis() >= touchAngryEndTime) {
    isTouchAngry = false;
    if (!isShaking && !isAngry && !isRecovering) {
      isRecovering = true;
      recoveryEndTime = millis() + 2000;
      setMood(MOOD_RECOVERING);
    }
  }

  // Petting Love Duration timeout (return to baseline 3s after release)
  if (currentMood == MOOD_LOVE && loveEndTime > 0 && millis() >= loveEndTime) {
    loveEndTime = 0;
    if (!isShaking && !isAngry && !isRecovering && !isSuspicious && !isTouchAngry && !moodManualOverride) {
      setMood(weatherMood);
    }
  }

  // Pickup / Approach detection: triggers an "ooh" double-blink & squash when picked up after stillness
  float totalMotion = deltaX + deltaY + deltaZ;
  unsigned long now = millis();
  if (totalMotion < 0.35) {
    if (!wasStationary && (now - lastStationaryTime > 4000)) {
      wasStationary = true;
    }
  } else {
    lastStationaryTime = now;
    if (wasStationary && totalMotion > 1.2 && !isShaking && !isAngry && currentPage == 0) {
      wasStationary = false;
      triggerDoubleBlink();
      triggerSquashStretch(140);
    }
  }

  // Idle Head-Tilt: occasional curious asymmetrical head tilt every 8-15 seconds
  if (now >= nextHeadTiltTime) {
    int r = random(0, 4);
    if (r == 0)      targetHeadTiltY = 2.5;  // Tilt right
    else if (r == 1) targetHeadTiltY = -2.5; // Tilt left
    else             targetHeadTiltY = 0.0;  // Level
    nextHeadTiltTime = now + random(8000, 15000);
  }
  headTiltOffsetY = headTiltOffsetY * 0.88 + targetHeadTiltY * 0.12;

  // Pupil and eye position updates
  if (!isShaking && !isAngry && currentPage == 0) {
    updateAutonomousGaze();

    float tiltX = currentRoll / 5.0;
    float tiltY = currentPitch / 5.0;

    float jitterX = (isTouchAngry) ? sin(millis() * 0.08) * 1.5 : 0;
    float jitterY = (isTouchAngry) ? cos(millis() * 0.08) * 1.5 : 0;

    targetPupilX = constrain(tiltX + autoGazeX + jitterX, -12, 12);
    targetPupilY = constrain(tiltY + autoGazeY + jitterY, -10, 10);

    currentPupilX = currentPupilX * 0.55 + targetPupilX * 0.45;
    currentPupilY = currentPupilY * 0.55 + targetPupilY * 0.45;

    leftEye.targetPupilX  = currentPupilX;
    leftEye.targetPupilY  = currentPupilY;
    rightEye.targetPupilX = currentPupilX;
    rightEye.targetPupilY = currentPupilY;

    // Apply continuous lifelike breath micro-bounce + curious head tilt to eye sockets
    leftEye.targetX  = 28 + (currentRoll / 15.0) + (autoGazeX * 0.2);
    leftEye.targetY  = 18 + (currentPitch / 15.0) + (autoGazeY * 0.2) + breathVal + headTiltOffsetY;
    rightEye.targetX = 80 + (currentRoll / 15.0) + (autoGazeX * 0.2);
    rightEye.targetY = 18 + (currentPitch / 15.0) + (autoGazeY * 0.2) + breathVal - headTiltOffsetY;
  }

  lastAccelX = accelX; lastAccelY = accelY; lastAccelZ = accelZ;
}

// ==================================================
// WEATHER FUNCTIONS
// ==================================================
const unsigned char* getBigIcon(String w) {
  if (w == "Clear")                      return bmp_clear;
  if (w == "Clouds")                     return bmp_clouds;
  if (w == "Rain" || w == "Drizzle")     return bmp_rain;
  return bmp_clouds;
}

const unsigned char* getMiniIcon(String w) {
  if (w == "Clear")                                        return mini_sun;
  if (w == "Rain" || w == "Drizzle" || w == "Thunderstorm") return mini_rain;
  return mini_cloud;
}

void updateWeatherMood() {
  if (weatherMain == "Clear") {
    weatherMood = MOOD_HAPPY;
  } else if (weatherMain == "Clouds" || weatherMain == "Mist" || weatherMain == "Smoke" || weatherMain == "Haze" || weatherMain == "Dust" || weatherMain == "Fog") {
    weatherMood = MOOD_GLOOMY;
  } else if (weatherMain == "Rain" || weatherMain == "Drizzle") {
    weatherMood = MOOD_SAD;
  } else if (weatherMain == "Thunderstorm") {
    weatherMood = MOOD_SURPRISED;
  } else if (weatherMain == "Snow") {
    weatherMood = MOOD_SLEEPY;
  } else if (temperature > 35) {
    weatherMood = MOOD_ANGRY;
  } else if (temperature < 5) {
    weatherMood = MOOD_SLEEPY;
  } else {
    weatherMood = MOOD_NORMAL;
  }

  if (!isShaking && !isAngry && !isRecovering && !isSuspicious && !isTouchAngry && !moodManualOverride && !isPetting && !isMoodSelecting && loveEndTime == 0) {
    setMood(weatherMood);
  }
}

// Plays a short, one-off physical reaction the moment a NEW weather reading differs
// meaningfully from the previous one — a shiver on a big temp drop, a slow wilt on a
// big temp rise, or a startled flash the instant a storm starts — instead of the face
// only ever passively reflecting whatever the current reading happens to be.
// Draws using local COPIES of leftEye/rightEye so the real eye-physics state (targets,
// velocities) is never touched; updateMotion() picks right back up afterward untouched.
void playWeatherChangeReaction(int reaction) {
  if (reaction == WEATHER_REACTION_NONE) return;

  unsigned long start = millis();
  unsigned long duration = (reaction == WEATHER_REACTION_STORM_STARTED) ? 900 : 1300;

  while (millis() - start < duration) {
    unsigned long elapsed = millis() - start;
    display.clearDisplay();
    updateEyePhysics();

    Eye leftDraw = leftEye;
    Eye rightDraw = rightEye;

    if (reaction == WEATHER_REACTION_TEMP_DROP) {
      // Quick tiny side-to-side shiver, like a cold chill
      float jitterX = sin(elapsed * 0.09) * 2.2;
      leftDraw.x += jitterX;
      rightDraw.x += jitterX;
    } else if (reaction == WEATHER_REACTION_TEMP_RISE) {
      // Slow droopy wilt, like heat fatigue
      float wilt = fabs(sin(elapsed * 0.02)) * 1.8;
      leftDraw.y += wilt;
      rightDraw.y += wilt;
    } else if (reaction == WEATHER_REACTION_STORM_STARTED) {
      setMood(MOOD_SURPRISED, false);
      if (elapsed < 400) display.drawBitmap(60, 2, bmp_exclamation, 8, 12, SH110X_WHITE);
    }

    drawNormalEye(leftDraw, true);
    drawNormalEye(rightDraw, false);
    drawMouth();
    display.display();
    delay(20);
  }
}

void fetchWeatherAndForecastHttps() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Skip TLS certificate verification for lightweight embedded HTTPS
  client.setTimeout(8);

  HTTPClient http;
  http.setTimeout(8000);

  float tempT = 0, feelsT = 0;
  int humT = 0;
  String mainT = "", descT = "";
  bool currentOk = false;

  // 1. Fetch Current Weather via HTTPS
  String url = "https://api.openweathermap.org/data/2.5/weather?q="
    + String(CITY) + "," + String(COUNTRY_CODE)
    + "&appid=" + String(OPENWEATHER_API_KEY) + "&units=metric";

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      JSONVar myObject = JSON.parse(payload);
      // Validate JSON structure and required keys before indexing
      if (JSON.typeof(myObject) != "undefined" && myObject.hasOwnProperty("main") && myObject.hasOwnProperty("weather")) {
        if (myObject["weather"].length() > 0) {
          tempT  = double(myObject["main"]["temp"]);
          feelsT = double(myObject["main"]["feels_like"]);
          humT   = int(myObject["main"]["humidity"]);
          mainT  = (const char*)myObject["weather"][0]["main"];
          descT  = (const char*)myObject["weather"][0]["description"];
          if (descT.length() > 0) descT[0] = toupper(descT[0]);
          currentOk = true;
        }
      }
    } else {
      Serial.print("Weather HTTPS error, HTTP code: ");
      Serial.println(httpCode);
    }
    http.end();
  }

  // 2. Fetch 5-Day / 3-Hour Forecast via HTTPS
  ForecastDay fcastTemp[3];
  bool forecastOk = false;

  url = "https://api.openweathermap.org/data/2.5/forecast?q="
    + String(CITY) + "," + String(COUNTRY_CODE)
    + "&appid=" + String(OPENWEATHER_API_KEY) + "&units=metric";

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      JSONVar fo = JSON.parse(payload);
      if (JSON.typeof(fo) != "undefined" && fo.hasOwnProperty("list")) {
        struct tm t;
        if (getLocalTime(&t, 0)) {
          int today = t.tm_wday;
          const char* days[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
          // OpenWeatherMap 5-day forecast API returns data in 3-hour steps (8 data points per 24-hour day).
          // Index calculation for roughly +24h, +48h, +72h at approximately the same time of day:
          // - Index 7:  (7 + 1) * 3h = 24h into the future (Day +1 / Tomorrow)
          // - Index 15: (15 + 1) * 3h = 48h into the future (Day +2 / Day after tomorrow)
          // - Index 23: (23 + 1) * 3h = 72h into the future (Day +3 / 3 days ahead)
          int indices[3] = { 7, 15, 23 };
          int listLen = fo["list"].length();
          forecastOk = true;
          for (int i = 0; i < 3; i++) {
            int idx = indices[i];
            if (idx < listLen && fo["list"][idx].hasOwnProperty("main") && fo["list"][idx].hasOwnProperty("weather") && fo["list"][idx]["weather"].length() > 0) {
              fcastTemp[i].temp     = (int)double(fo["list"][idx]["main"]["temp"]);
              fcastTemp[i].iconType = (const char*)fo["list"][idx]["weather"][0]["main"];
              int nextDayIndex      = (today + i + 1) % 7;
              fcastTemp[i].dayName  = days[nextDayIndex];
            } else {
              forecastOk = false;
            }
          }
        }
      }
    } else {
      Serial.print("Forecast HTTPS error, HTTP code: ");
      Serial.println(httpCode);
    }
    http.end();
  }

  // Safely commit new weather data under mutex lock
  if (currentOk || forecastOk) {
    if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (currentOk) {
        // Compare the NEW reading against the OLD one (still held in temperature/weatherMain
        // at this point) before overwriting, so we can react to the *change*, not just the
        // current value. Skipped on the very first reading after boot (nothing to compare to).
        if (weatherBaselineEstablished) {
          if (mainT == "Thunderstorm" && weatherMain != "Thunderstorm") {
            pendingWeatherReaction = WEATHER_REACTION_STORM_STARTED; // storm starting takes priority
          } else if ((temperature - tempT) >= WEATHER_REACTION_TEMP_DELTA) {
            pendingWeatherReaction = WEATHER_REACTION_TEMP_DROP;
          } else if ((tempT - temperature) >= WEATHER_REACTION_TEMP_DELTA) {
            pendingWeatherReaction = WEATHER_REACTION_TEMP_RISE;
          }
        }
        weatherBaselineEstablished = true;

        temperature = tempT;
        feelsLike   = feelsT;
        humidity    = humT;
        weatherMain = mainT;
        weatherDesc = descT;
        updateWeatherMood();
      }
      if (forecastOk) {
        for (int i = 0; i < 3; i++) {
          fcast[i] = fcastTemp[i];
        }
      }
      weatherDataReady = true;
      xSemaphoreGive(weatherMutex);
    }
  }
}

void weatherTaskCode(void* pvParameters) {
  for (;;) {
    // Wait for notification to fetch immediately, or timeout every 10 minutes
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_UPDATE_INTERVAL));

    if (wifiState == WIFI_STATE_CONNECTED && WiFi.status() == WL_CONNECTED) {
      fetchWeatherAndForecastHttps();
    }
  }
}

// Forward declarations
void startBoomAnimation();
void drawBoomAnimation();
void drawTimerPage();

// Forward declarations for functions added in this update. Arduino's automatic prototype
// generator uses a simple ctags-style scanner (not a full C++ parser) and can misfire on
// lambdas, multi-line string literals, or unusual signatures — declaring these explicitly
// sidesteps that entirely, the same workaround already used above for the original three.
void playWeatherChangeReaction(int reaction);
void renderCurrentFrame();
void captureFrame(uint8_t* buf);
bool getFrameBit(const uint8_t* buf, int x, int y);
void playPageTransition();
void updateDisplayContrast();
void advancePomodoroPhase();
void stopPomodoro();
void drawReminderPopup();
String signalBars(int rssi);
void handlePortalRoot();
void handlePortalSave();
void handlePortalForget();
void handlePortalNotFound();
const char* moodIntToName(int m);
int moodNameToSelectableInt(const String& nameIn);
void handleDashboardRoot();
void handleDashboardNotFound();
void handleApiState();
void handleApiSetMood();
void handleApiTimer();
void handleApiReminder();
void handleApiForgetWifi();
void startDashboardServer();

// ==================================================
// TIME-OF-DAY DISPLAY CONTRAST
// ==================================================
uint8_t currentContrastLevel = 255;
unsigned long lastContrastCheckTime = 0;
const unsigned long CONTRAST_CHECK_INTERVAL = 60000; // recheck once a minute, not every frame

// Dims the OLED at night and brings it back up during the day, using NTP-synced local time.
// This is a 1-bit monochrome display, so "dimming" means lower drive contrast/current draw,
// not grayscale — it still reduces glare and eye strain in a dark room at negligible cost.
void updateDisplayContrast() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return; // not synced yet (still connecting) — leave contrast as-is

  uint8_t targetContrast;
  int hour = t.tm_hour;
  if (hour >= 23 || hour < 6) {
    targetContrast = 20;   // 11pm-6am: dim, quiet-hours
  } else if (hour < 8) {
    targetContrast = 120;  // 6am-8am: ramping up
  } else {
    targetContrast = 255;  // 8am-11pm: full brightness
  }

  if (targetContrast != currentContrastLevel) {
    currentContrastLevel = targetContrast;
    display.setContrast(currentContrastLevel);
  }
}

// ==================================================
// TOUCH HANDLER
// ==================================================
void handleTouch() {
  bool currentPinState = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  if (currentPinState && !lastPinState) {
    // 1. Touch Pressed
    pressStartTime = now;
    isLongPressHandled = false;
    isPetting = false;
    isMoodSelecting = false;
    isTimerCancelling = false;
    isPresetSelecting = false;
    lastUserInteractionTime = now;
  } else if (currentPinState && lastPinState) {
    // 2. Touch Held Down
    unsigned long holdDuration = now - pressStartTime;
    lastUserInteractionTime = now;

    // --- ON FACE (PAGE 0) ---
    if (currentPage == 0 && !isBoomActive) {
      // A) Gentle Touch / Petting & Early Calm Down (800ms - 2500ms)
      if (holdDuration >= 800 && holdDuration < 2500 && !isMoodSelecting) {
        if (!isPetting) {
          isPetting = true;
          // Sustained gentle touch calms it down early if angry, dizzy, or suspicious
          if (isAngry || isTouchAngry || isShaking || isSuspicious) {
            isAngry = false;
            isTouchAngry = false;
            isShaking = false;
            isSuspicious = false;
            pokeCount = 0;
            isRecovering = true;
            recoveryEndTime = now + 2000;
            setMood(MOOD_RECOVERING);
          } else {
            pokeCount = 0;
            setMood(MOOD_LOVE);
          }
        }
      }
      // B) Long Hold (> 2500ms): Manual Mood Selector
      else if (holdDuration >= 2500) {
        if (!isMoodSelecting) {
          isMoodSelecting = true;
          isPetting = false;
          selectedMoodIndex = 0;
          lastMoodCycleTime = now;
          setMood(SELECTABLE_MOODS[selectedMoodIndex], false);
        } else {
          // Cycle to next mood every 700ms while holding
          if (now - lastMoodCycleTime >= 700) {
            lastMoodCycleTime = now;
            selectedMoodIndex = (selectedMoodIndex + 1) % NUM_SELECTABLE_MOODS;
            setMood(SELECTABLE_MOODS[selectedMoodIndex]);
          }
        }
      }
    }
    // --- ON NON-FACE PAGES (PAGES 1, 2, 3) ---
    else {
      // Universal Reset / Return Home: Hold 3.0s anywhere to go home
      if (holdDuration >= 3000 && !isLongPressHandled) {
        currentPage = 0;
        subPage = 0;
        isLongPressHandled = true;
        isTimerCancelling = false;
      }
      // Page 1 (Clock): Long press toggles World Clock
      else if (currentPage == 1 && holdDuration >= LONG_PRESS_TIME && !isLongPressHandled) {
        subPage = (subPage == 1) ? 0 : 1;
        isLongPressHandled = true;
      }
      // Page 2 (Weather): Long press toggles 3-Day Forecast
      else if (currentPage == 2 && holdDuration >= LONG_PRESS_TIME && !isLongPressHandled) {
        subPage = (subPage == 2) ? 0 : 2;
        isLongPressHandled = true;
      }
      // Page 3 (Timer): Deliberate hold (2500ms) to cancel active countdown safely
      else if (currentPage == 3 && isTimerActive) {
        if (holdDuration >= 800) {
          isTimerCancelling = true;
        }
        if (holdDuration >= TIMER_CANCEL_HOLD_TIME && !isLongPressHandled) {
          isTimerActive = false;
          stopPomodoro();
          isTimerCancelling = false;
          startBoomAnimation();
          currentPage = 0;
          subPage = 0;
          isLongPressHandled = true;
        }
      }
      // Page 3 (Timer), idle: hold to cycle through duration presets (5/15/30/60 min)
      else if (currentPage == 3 && !isTimerActive) {
        if (holdDuration >= 800) {
          if (!isPresetSelecting) {
            isPresetSelecting = true;
            lastPresetCycleTime = now;
          } else if (now - lastPresetCycleTime >= 600) {
            lastPresetCycleTime = now;
            selectedPresetIndex = (selectedPresetIndex + 1) % NUM_TIMER_PRESETS;
            timerDuration = (unsigned long)TIMER_PRESETS_MIN[selectedPresetIndex] * 60UL * 1000UL;
          }
        }
      }
    }
  } else if (!currentPinState && lastPinState) {
    // 3. Touch Released
    unsigned long holdDuration = now - pressStartTime;
    lastUserInteractionTime = now;

    if (isMoodSelecting) {
      // Release in Mood Selector: Lock in selected mood manual override
      moodManualOverride = true;
      setMood(SELECTABLE_MOODS[selectedMoodIndex]);
      isMoodSelecting = false;
      isLongPressHandled = true;
    } else if (isPetting) {
      isPetting = false;
      if (!isRecovering) {
        setMood(MOOD_LOVE);
        loveEndTime = now + 3000; // Keep loving mood for 3s after gentle touch
      }
      isLongPressHandled = true;
    } else if (isPresetSelecting) {
      // Release while cycling timer presets: the duration is already committed as you cycle,
      // so releasing just ends the cycling gesture (no extra "lock in" step needed).
      isPresetSelecting = false;
      isLongPressHandled = true;
    }

    isTimerCancelling = false;

    // Short tap (< 500ms) accumulator
    if (holdDuration < 500 && !isLongPressHandled) {
      tapCounter++;
      lastTapTime = now;
    }
  }
  lastPinState = currentPinState;

  // 4. Tap Counter Evaluation
  if (tapCounter > 0) {
    if (now - lastTapTime > TAP_REGISTER_DELAY) {
      if (isBoomActive) {
        // Tap during Boom dismisses animation early
        isBoomActive = false;
        setMood(weatherMood);
        currentPage = 0;
        subPage = 0;
      } else if (isWeatherPopupActive) {
        isWeatherPopupActive = false;
        currentPage = 0;
        subPage = 0;
      } else if (isReminderPopupActive) {
        isReminderPopupActive = false;
      } else if (isMinutePopupActive) {
        isMinutePopupActive = false;
      } else if (tapCounter == 1) {
        // ============================================
        // SINGLE TAP: UNIVERSAL PAGE NAVIGATION
        // ============================================
        if (subPage != 0) {
          subPage = 0; // Return from subpage to main page
        } else {
          currentPage++;
          if (currentPage > 3) currentPage = 0;
        }
        pageTransitionPending = true;
        // Clear transient touch reactions on navigation
        isSuspicious = false;
        isTouchAngry = false;
      } else if (tapCounter >= 2) {
        // ============================================
        // DOUBLE TAP: CONTEXT ACTIONS
        // ============================================
        if (currentPage == 0) {
          // --- ON FACE (PAGE 0): POKE ESCALATION ---
          if (!isShaking && !isAngry && !isRecovering) {
            if (now - lastPokeTime < 3500) {
              pokeCount += 1;
            } else {
              pokeCount = 1;
            }
            lastPokeTime = now;
            triggerSquashStretch(120);

            if (pokeCount >= 3) {
              // 3rd poke: gets ANGRY!
              isTouchAngry = true;
              touchAngryEndTime = now + TOUCH_ANGRY_DURATION;
              isSuspicious = false;
              setMood(MOOD_ANGRY);
              pokeCount = 0;
            } else {
              // 1st or 2nd poke: gets SUSPICIOUS with escalating eyebrows!
              isSuspicious = true;
              suspiciousEndTime = now + SUSPICIOUS_DURATION;
              isTouchAngry = false;
              setMood(MOOD_SUSPICIOUS);
              targetGazeX = (pokeCount % 2 == 1) ? -10 : 10;
              targetGazeY = 0;
            }
          }
        } else if (currentPage == 3) {
          // --- ON TIMER PAGE (PAGE 3): TOGGLE TIMER ---
          if (!isTimerActive) {
            isTimerActive = true;
            timerStartTime = now;
            timerEndTime = now + timerDuration;
          } else {
            isTimerActive = false;
            stopPomodoro();
            currentPage = 0;
            subPage = 0;
            startBoomAnimation();
          }
        } else {
          // Double tap on Clock/Weather toggles subpage
          if (currentPage == 1) subPage = (subPage == 1) ? 0 : 1;
          else if (currentPage == 2) subPage = (subPage == 2) ? 0 : 2;
        }
      }
      tapCounter = 0;
    }
  }
}

// ==================================================
// DRAW FUNCTIONS & UI AFFORDANCES
// ==================================================

// Draw mini glanceable timer badge on Face page (hourglass + remaining time)
void drawTimerBadge() {
  if (!isTimerActive) return;
  unsigned long now = millis();
  unsigned long remain = (timerEndTime > now) ? (timerEndTime - now) : 0;
  int mins = (remain + 59999) / 60000;
  int secs = (remain / 1000) % 60;

  display.drawBitmap(3, 2, mini_hourglass, 7, 7, SH110X_WHITE);
  display.setFont(NULL);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(12, 2);
  if (mins >= 1) {
    display.print(mins);
    display.print("m");
  } else {
    display.print(secs);
    display.print("s");
  }
}

// Draw expressive escalating eyebrows & forehead agitation marks before full anger
void drawPokeEscalation() {
  if (currentPage != 0 || isShaking || isAngry || isTouchAngry) return;
  if (pokeCount == 0 && !isSuspicious) return;

  int lx = (int)leftEye.x, ly = (int)leftEye.y, lw = (int)leftEye.w;
  int rx = (int)rightEye.x, ry = (int)rightEye.y, rw = (int)rightEye.w;

  if (pokeCount == 1) {
    // Poke 1: Subtle quizzical / annoyed eyebrow tilt
    display.drawLine(lx + 2, ly - 3, lx + lw - 2, ly - 5, SH110X_WHITE);
    display.drawLine(rx + 2, ry - 5, rx + rw - 2, ry - 3, SH110X_WHITE);
  } else if (pokeCount >= 2 || isSuspicious) {
    // Poke 2: Sharp, furrowed double-line angry brow + agitation stress ticks
    display.drawLine(lx, ly - 2, lx + lw, ly - 7, SH110X_WHITE);
    display.drawLine(lx, ly - 1, lx + lw, ly - 6, SH110X_WHITE);

    display.drawLine(rx, ry - 7, rx + rw, ry - 2, SH110X_WHITE);
    display.drawLine(rx, ry - 6, rx + rw, ry - 1, SH110X_WHITE);

    // Agitation stress lines in forehead center
    display.drawLine(61, 4, 61, 8, SH110X_WHITE);
    display.drawLine(67, 4, 67, 8, SH110X_WHITE);
    display.drawLine(64, 2, 64, 5, SH110X_WHITE);
  }
}

void drawDizzyEye(Eye& e, bool isLeft) {
  int ix = (int)e.x, iy = (int)e.y, iw = (int)e.w, ih = (int)e.h;
  int cx = ix + iw / 2, cy = iy + ih / 2;

  display.fillCircle(cx, cy, iw / 2, SH110X_WHITE);

  unsigned long now = millis();
  float angle = (now - shakeStartTime) * 0.025;
  int radius = iw / 3;
  int pupilX = cx + cos(angle) * radius;
  int pupilY = cy + sin(angle) * radius;
  display.fillCircle(pupilX, pupilY, 4, SH110X_BLACK);
  display.fillCircle(pupilX + 2, pupilY - 2, 1, SH110X_WHITE);

  int pupilX2 = cx - cos(angle) * radius;
  int pupilY2 = cy - sin(angle) * radius;
  display.fillCircle(pupilX2, pupilY2, 3, SH110X_BLACK);

  int starOffset = (now / 80) % 4;
  display.drawBitmap(6 - starOffset, 0, bmp_dizzy_stars, 16, 16, SH110X_WHITE);
  display.drawBitmap(106 + starOffset, 0, bmp_dizzy_stars, 16, 16, SH110X_WHITE);
}

void drawAngryEye(Eye& e, bool isLeft) {
  int ix = (int)e.x, iy = (int)e.y, iw = (int)e.w, ih = (int)e.h;

  display.fillRoundRect(ix, iy, iw, ih, 8, SH110X_WHITE);
  int cx = ix + iw / 2, cy = iy + ih / 2;
  int pupilSize = iw / 2.5;
  int px = cx - 2, py = cy;
  display.fillRoundRect(px - pupilSize/2, py - pupilSize/2, pupilSize, pupilSize, pupilSize/2, SH110X_BLACK);

  if (isLeft) {
    for (int i = 0; i < 8; i++) display.drawLine(ix-2, iy-2+i, ix+iw+2, iy+6+i, SH110X_BLACK);
    display.drawBitmap(ix - 12, iy - 6, bmp_angry_mark, 16, 16, SH110X_WHITE);
  } else {
    for (int i = 0; i < 8; i++) display.drawLine(ix-2, iy+6+i, ix+iw+2, iy-2+i, SH110X_BLACK);
    display.drawBitmap(ix + iw - 4, iy - 6, bmp_angry_mark, 16, 16, SH110X_WHITE);
  }
  for (int i = 0; i < 3; i++) display.drawLine(ix+2+i*4, iy+ih-2, ix+6+i*4, iy+ih-6, SH110X_WHITE);
}

void drawNormalEye(Eye& e, bool isLeft) {
  int ix = (int)e.x, iy = (int)e.y, iw = (int)e.w, ih = (int)e.h;
  if (ih < 2) ih = 2;

  // If eye is closed / thin slit in mid-blink, draw a clean crisp eyelid line
  if (ih <= 4) {
    display.fillRoundRect(ix, iy + 14, iw, 3, 1, SH110X_WHITE);
    return;
  }

  int eyeTop = iy + (32 - ih) / 2;
  int eyeBottom = eyeTop + ih;
  int r = (ih < 8) ? 2 : ((iw < 20) ? 3 : 8);

  display.fillRoundRect(ix, eyeTop, iw, ih, r, SH110X_WHITE);

  int cx = ix + iw / 2;
  int cy = iy + 16; // centered vertically in the socket
  int pw = iw / 2.2;
  int ph = constrain(ih - 4, 2, (int)(iw / 2.2));

  int px = cx + (int)e.pupilX - pw / 2;
  int py = cy + (int)e.pupilY - ph / 2;

  // Keep pupil strictly within eye bounds
  if (px < ix + 2) px = ix + 2;
  if (px + pw > ix + iw - 2) px = ix + iw - 2 - pw;
  if (py < eyeTop + 1) py = eyeTop + 1;
  if (py + ph > eyeBottom - 1) py = eyeBottom - 1 - ph;

  if (ih >= 8) {
    display.fillRoundRect(px, py, pw, ph, min(r / 2, 3), SH110X_BLACK);
    if (iw > 15 && ih > 16) {
      display.fillCircle(px + pw - 4, py + 3, 2, SH110X_WHITE);
      // Secondary shining catchlight sparkle on Love or Excited
      if (currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
        display.fillCircle(px + 4, py + ph - 4, 1, SH110X_WHITE);
      }
    }
  }

  // Only apply mood eyelid cutouts when the eye is open (not in mid-blink)
  if (ih >= 18 && e.blinkState == 0) {
    if (currentMood == MOOD_HAPPY || currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED) {
      display.fillRect(ix, eyeBottom - 8, iw, 10, SH110X_BLACK);
    } else if (currentMood == MOOD_SLEEPY) {
      display.fillRect(ix, eyeTop, iw, ih / 2, SH110X_BLACK);
    } else if (currentMood == MOOD_GLOOMY) {
      display.fillRect(ix, eyeTop, iw, (int)(ih * 0.38), SH110X_BLACK);
    } else if (currentMood == MOOD_RECOVERING) {
      display.fillRect(ix, eyeTop, iw, (int)(ih * 0.3), SH110X_BLACK);
      display.fillRect(ix, eyeBottom - 5, iw, 6, SH110X_BLACK);
    } else if (currentMood == MOOD_SAD) {
      if (isLeft)
        for (int i = 0; i < 7; i++) display.drawLine(ix, eyeTop+i, ix+iw, eyeTop+4+i, SH110X_BLACK);
      else
        for (int i = 0; i < 7; i++) display.drawLine(ix, eyeTop+4+i, ix+iw, eyeTop+i, SH110X_BLACK);
    } else if (currentMood == MOOD_SUSPICIOUS) {
      if (isLeft) {
        // Squinted side-eye with smooth curved upper and lower lids
        display.fillRoundRect(ix - 2, eyeTop - 6, iw + 4, (int)(ih * 0.42) + 6, 8, SH110X_BLACK);
        display.fillRoundRect(ix - 2, eyeBottom - (int)(ih * 0.22), iw + 4, (int)(ih * 0.25) + 6, 6, SH110X_BLACK);
      } else {
        // Skeptical arched side-eye with subtle smooth curved top lid
        display.fillRoundRect(ix - 2, eyeTop - 6, iw + 4, (int)(ih * 0.20) + 6, 6, SH110X_BLACK);
      }
    }
  }
}

void drawMouth() {
  int mx = 64, my = 54;

  if (currentMood == MOOD_DIZZY && isShaking) {
    unsigned long now = millis();
    for (int i = -8; i <= 8; i++) {
      int y = my + sin(i * 0.8 + now * 0.03) * 4;
      display.drawPixel(mx + i, y, SH110X_WHITE);
    }
    return;
  }
  if (currentMood == MOOD_ANGRY && (isAngry || isTouchAngry)) {
    display.fillRect(mx - 8, my - 2, 16, 5, SH110X_BLACK);
    for (int i = -5; i <= 5; i += 3) display.drawLine(mx+i, my-2, mx+i, my+2, SH110X_WHITE);
    return;
  }

  switch (currentMood) {
    case MOOD_HAPPY: {
      unsigned long elapsedHappy = millis() - moodChangeStartTime;
      if (elapsedHappy < 200) {
        // Anticipation phase (0-200ms): subtle playful dip before rising into full smile
        float t = (float)elapsedHappy / 200.0;
        float dip = sin(t * 3.14159) * 2.0;
        for (int i = -6; i <= 6; i++) {
          int y = my + (int)dip - (int)((i * i / 16.0) * t);
          display.drawPixel(mx + i, y, SH110X_WHITE);
        }
      } else {
        for (int i = -6; i <= 6; i++) {
          int y = my - (i * i / 16);
          if (y < my + 2) display.drawPixel(mx + i, y, SH110X_WHITE);
        }
      }
      break;
    }
    case MOOD_EXCITED:
      for (int i = -8; i <= 8; i++) {
        int y = my - (i * i / 18);
        if (y < my + 3) display.drawPixel(mx + i, y, SH110X_WHITE);
      }
      display.drawCircle(mx, my + 1, 2, SH110X_WHITE);
      break;
    case MOOD_SUSPICIOUS:
      display.drawLine(mx - 6, my + 2, mx + 6, my - 2, SH110X_WHITE);
      break;
    case MOOD_GLOOMY:
      display.drawLine(mx - 5, my + 1, mx + 5, my + 1, SH110X_WHITE);
      break;
    case MOOD_RECOVERING: {
      unsigned long remain = (recoveryEndTime > millis()) ? (recoveryEndTime - millis()) : 0;
      if (remain > 1500) {
        display.drawLine(mx - 4, my, mx + 4, my, SH110X_WHITE); // weary exhale
      } else {
        for (int i = -4; i <= 4; i++) {
          int y = my - (i * i / 24);
          display.drawPixel(mx + i, y, SH110X_WHITE); // soft gentle relief smile
        }
      }
      break;
    }
    case MOOD_SAD:
      for (int i = -6; i <= 6; i++) {
        int y = my + 2 + (i * i / 16);
        display.drawPixel(mx + i, y, SH110X_WHITE);
      }
      break;
    case MOOD_SURPRISED:
      display.fillCircle(mx, my + 1, 3, SH110X_BLACK);
      display.drawCircle(mx, my + 1, 3, SH110X_WHITE);
      break;
    case MOOD_SLEEPY:
      display.drawLine(mx - 4, my, mx + 4, my, SH110X_WHITE);
      break;
    case MOOD_LOVE:
      display.drawBitmap(mx - 6, my - 2, bmp_heart, 12, 12, SH110X_WHITE);
      break;
    default:
      display.drawLine(mx - 4, my, mx + 4, my, SH110X_WHITE);
      break;
  }
}

void updateEyePhysics() {
  unsigned long now = millis();
  
  if (currentMood == MOOD_RECOVERING) {
    breathVal = sin(now / 500.0) * 2.5; // slow deep relief breath
  } else {
    breathVal = sin(now / 800.0) * 1.5;
  }

  int blinkMin = (isShaking || isAngry || isTouchAngry) ? 400 : ((currentMood == MOOD_SLEEPY || currentMood == MOOD_GLOOMY) ? 3000 : 1600);
  int blinkMax = (isShaking || isAngry || isTouchAngry) ? 1000 : ((currentMood == MOOD_SLEEPY || currentMood == MOOD_GLOOMY) ? 6000 : 3800);

  // Trigger new blink if idle and time has come
  if (leftEye.blinkState == 0 && now >= leftEye.nextBlinkTime) {
    leftEye.blinkState = rightEye.blinkState = 1; // 1 = closing
    leftEye.blinkStartTime = rightEye.blinkStartTime = now;
  }

  // Handle blink animation states
  if (leftEye.blinkState == 1) {
    // Closing phase (snappy 65ms) with deliberate deep overshoot past closed (0.0)
    leftEye.targetH = rightEye.targetH = 0.0;
    if (now - leftEye.blinkStartTime >= 65) {
      leftEye.blinkState = rightEye.blinkState = 2; // 2 = opening
      leftEye.targetH = rightEye.targetH = (squashStretchEndTime > now) ? 24.0 : 32.0;
    }
  } else if (leftEye.blinkState == 2) {
    // Opening phase (smooth elastic spring 105ms)
    leftEye.targetH = rightEye.targetH = (squashStretchEndTime > now) ? 24.0 : 32.0;
    if (now - leftEye.blinkStartTime >= 170) {
      leftEye.blinkState = rightEye.blinkState = 0; // 0 = open
      leftEye.h = rightEye.h = (squashStretchEndTime > now) ? 24.0 : 32.0;
      leftEye.velH = rightEye.velH = 0;
      if (doubleBlinkCount > 0) {
        doubleBlinkCount--;
        leftEye.nextBlinkTime = now + 90; // Immediate second blink for "ooh" double-blink reaction
      } else {
        leftEye.nextBlinkTime = now + random(blinkMin, blinkMax);
      }
    }
  }

  // Handle squash & stretch returning to standard dimensions (32x32)
  if (squashStretchEndTime > 0 && now >= squashStretchEndTime) {
    squashStretchEndTime = 0;
    leftEye.targetW = rightEye.targetW = 32.0;
    if (leftEye.blinkState == 0) {
      leftEye.targetH = rightEye.targetH = 32.0;
    }
  }

  leftEye.update();
  rightEye.update();
}

void drawEmoPage() {
  updateEyePhysics();
  if (currentMood == MOOD_DIZZY && isShaking) {
    drawDizzyEye(leftEye, true);
    drawDizzyEye(rightEye, false);
  } else if (currentMood == MOOD_ANGRY && (isAngry || isTouchAngry)) {
    drawAngryEye(leftEye, true);
    drawAngryEye(rightEye, false);
  } else {
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawPokeEscalation();
  }
  drawMouth();

  // Corner timer badge whenever timer countdown is active
  drawTimerBadge();

  // Particle & status indicators
  if (!isShaking && !isAngry && !isTouchAngry) {
    // Transitory reaction icon popups for the first 400ms of state
    if (millis() - moodChangeStartTime < 400) {
      if (currentMood == MOOD_SURPRISED) {
        display.drawBitmap(60, 2, bmp_exclamation, 8, 12, SH110X_WHITE);
      } else if (currentMood == MOOD_SUSPICIOUS) {
        display.drawBitmap(102, 2, bmp_question, 8, 12, SH110X_WHITE);
      }
    }

    // Blush marks under both eyes on Love, Excited, or Petting
    if (currentMood == MOOD_LOVE || currentMood == MOOD_EXCITED || isPetting) {
      for (int i = 0; i < 3; i++) {
        display.drawLine((int)leftEye.x + 8 + i * 4, (int)leftEye.y + (int)leftEye.h + 4, (int)leftEye.x + 10 + i * 4, (int)leftEye.y + (int)leftEye.h + 1, SH110X_WHITE);
        display.drawLine((int)rightEye.x + 8 + i * 4, (int)rightEye.y + (int)rightEye.h + 4, (int)rightEye.x + 10 + i * 4, (int)rightEye.y + (int)rightEye.h + 1, SH110X_WHITE);
      }
    }

    if (isMoodSelecting) {
      // Mood selector HUD banner
      display.fillRect(18, 0, 92, 12, SH110X_WHITE);
      display.drawRoundRect(16, 0, 96, 13, 3, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
      display.setFont(NULL);
      String txt = "< " + String(MOOD_NAMES[selectedMoodIndex]) + " >";
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 2);
      display.print(txt);
      display.setTextColor(SH110X_WHITE);
    } else if (isPetting) {
      display.drawBitmap(56, 0, bmp_heart, 16, 16, SH110X_WHITE);
      display.drawBitmap(24, 2, bmp_heart, 16, 16, SH110X_WHITE);
      display.drawBitmap(88, 2, bmp_heart, 16, 16, SH110X_WHITE);
    } else if (currentMood == MOOD_LOVE) {
      display.drawBitmap(56, 0, bmp_heart, 16, 16, SH110X_WHITE);
    } else if (currentMood == MOOD_SLEEPY) {
      display.drawBitmap(110, 0, bmp_zzz, 16, 16, SH110X_WHITE);
    } else if (currentMood == MOOD_SAD) {
      display.drawBitmap(60, 56, bmp_tiny_drop, 8, 8, SH110X_WHITE);
    }
  }
}

void drawClockPage() {
  struct tm t;
  if (!getLocalTime(&t, 0)) {
    display.setFont(NULL);
    display.setCursor(30, 30);
    display.print("Syncing...");
    return;
  }
  String ampm = (t.tm_hour >= 12) ? "PM" : "AM";
  int h12 = t.tm_hour % 12;
  if (h12 == 0) h12 = 12;

  display.setTextColor(SH110X_WHITE);
  display.setFont(NULL);
  display.setCursor(114, 0);
  display.print(ampm);

  display.setFont(&FreeSansBold18pt7b);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", h12, t.tm_min);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 42);
  display.print(timeStr);

  display.setFont(&FreeSans9pt7b);
  char dateStr[20];
  strftime(dateStr, 20, "%a, %b %d", &t);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 60);
  display.print(dateStr);
}

// Rough US DST check: 2nd Sunday of March through 1st Sunday of November.
// Good enough for a display gadget; not calendar-perfect for every edge case.
bool isUSDaylightSaving(const struct tm* utc) {
  int month = utc->tm_mon + 1;
  if (month < 3 || month > 11) return false;
  if (month > 3 && month < 11) return true;
  int daysSinceSunday = utc->tm_wday;
  int precedingSunday = utc->tm_mday - daysSinceSunday;
  if (month == 3)  return precedingSunday >= 8;   // on/after 2nd Sunday
  return precedingSunday < 1;                     // before 1st Sunday (Nov)
}

// Rough UK DST (BST) check: last Sunday of March through last Sunday of October.
bool isUKDaylightSaving(const struct tm* utc) {
  int month = utc->tm_mon + 1;
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  int daysSinceSunday = utc->tm_wday;
  int precedingSunday = utc->tm_mday - daysSinceSunday;
  if (month == 3)  return precedingSunday >= 25;  // last Sunday of March
  return precedingSunday < 25;                    // before last Sunday of Oct
}

void drawWorldClockPage() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  time_t now; time(&now);
  struct tm utcTm;
  gmtime_r(&now, &utcTm);

  int nyOffset = isUSDaylightSaving(&utcTm) ? -4 : -5;
  int ldnOffset = isUKDaylightSaving(&utcTm) ? 1 : 0;

  time_t newyorkEpoch = now + (nyOffset * 3600);
  time_t londonEpoch  = now + (ldnOffset * 3600);
  struct tm* localtm  = &t;
  struct tm londontm, nytm;
  gmtime_r(&londonEpoch, &londontm);
  gmtime_r(&newyorkEpoch, &nytm);

  display.fillRect(0, 0, 128, 14, SH110X_WHITE);
  display.setFont(NULL);
  display.setTextColor(SH110X_BLACK);
  String title = "WORLD CLOCK";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 4);
  display.print(title);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(42, 16, 42, 63, SH110X_WHITE);
  display.drawLine(85, 16, 85, 63, SH110X_WHITE);
  display.setFont(NULL);
  display.setCursor(12, 18); display.print("JPR");
  display.setCursor(52, 18); display.print("LDN");
  display.setCursor(94, 18); display.print("NYC");

  char jStr[10]; sprintf(jStr, "%02d:%02d", localtm->tm_hour, localtm->tm_min);
  display.getTextBounds(jStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(21 - w/2, 48); display.print(jStr);

  char lStr[10]; sprintf(lStr, "%02d:%02d", londontm.tm_hour, londontm.tm_min);
  display.getTextBounds(lStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(64 - w/2, 48); display.print(lStr);

  char nStr[10]; sprintf(nStr, "%02d:%02d", nytm.tm_hour, nytm.tm_min);
  display.getTextBounds(nStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(106 - w/2, 48); display.print(nStr);
}

void drawWeatherPage() {
  if (WiFi.status() != WL_CONNECTED) {
    display.setFont(NULL); display.setCursor(0, 0); display.print("No WiFi");
    return;
  }

  String wMain = "Clouds", wDesc = "Sunny";
  float temp = 0.0, fLike = 0.0;
  int hum = 0;

  if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    wMain = weatherMain;
    wDesc = weatherDesc;
    temp  = temperature;
    fLike = feelsLike;
    hum   = humidity;
    xSemaphoreGive(weatherMutex);
  } else {
    wMain = weatherMain;
    wDesc = weatherDesc;
    temp  = temperature;
    fLike = feelsLike;
    hum   = humidity;
  }

  display.drawBitmap(96, 0, getBigIcon(wMain), 32, 32, SH110X_WHITE);
  display.setFont(&FreeSansBold9pt7b);
  String c = CITY; c.toUpperCase();
  display.setCursor(0, 14);
  if (c.length() > 9) c = c.substring(0, 8) + ".";
  display.print(c);

  display.setFont(&FreeSansBold18pt7b);
  int tempInt = (int)temp;
  display.setCursor(0, 48);
  display.print(tempInt);

  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(String(tempInt).c_str(), 0, 48, &x1, &y1, &w, &h);
  display.fillCircle(x1 + w + 5, 26, 4, SH110X_WHITE);

  display.setFont(NULL);
  display.drawBitmap(88, 32, bmp_tiny_drop, 8, 8, SH110X_WHITE);
  display.setCursor(100, 32); display.print(hum); display.print("%");
  display.setCursor(88, 45); display.print("~"); display.print((int)fLike);
  display.drawLine(0, 52, 128, 52, SH110X_WHITE);
  display.setCursor(0, 55);
  String shortDesc = wDesc;
  if (shortDesc.length() > 14) shortDesc = shortDesc.substring(0, 12) + "..";
  display.print(shortDesc);
}

void drawForecastPage() {
  display.fillRect(0, 0, 128, 14, SH110X_WHITE);
  display.setFont(NULL);
  display.setTextColor(SH110X_BLACK);
  String title = "3-DAY FORECAST";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 4);
  display.print(title);
  display.setTextColor(SH110X_WHITE);

  display.drawLine(42, 16, 42, 63, SH110X_WHITE);
  display.drawLine(85, 16, 85, 63, SH110X_WHITE);

  ForecastDay fcastCopy[3];
  if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (int i = 0; i < 3; i++) fcastCopy[i] = fcast[i];
    xSemaphoreGive(weatherMutex);
  } else {
    for (int i = 0; i < 3; i++) fcastCopy[i] = fcast[i];
  }

  for (int i = 0; i < 3; i++) {
    int xStart = i * 43, centerX = xStart + 21;
    display.setFont(NULL);
    String d = fcastCopy[i].dayName;
    if (d == "") d = "WAIT";
    display.setCursor(centerX - (d.length() * 3), 18);
    display.print(d);
    display.drawBitmap(centerX - 8, 26, getMiniIcon(fcastCopy[i].iconType), 16, 16, SH110X_WHITE);
    display.setFont(&FreeSansBold9pt7b);
    display.getTextBounds(String(fcastCopy[i].temp).c_str(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(centerX - w/2 - 2, 58);
    display.print(fcastCopy[i].temp);
    display.fillCircle(centerX + w/2 + 1, 50, 2, SH110X_WHITE);
  }
}

// Sticky-note reminder popup: text + time set via the Dashboard, pops up on the Face page
// at the scheduled time (see the reminderArmed check in loop()), auto-dismisses after
// REMINDER_POPUP_DURATION or on tap, same pattern as the weather popup.
void drawReminderPopup() {
  display.fillRoundRect(6, 14, 116, 38, 6, SH110X_WHITE);
  display.drawRoundRect(4, 12, 120, 42, 8, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setFont(NULL);
  display.setCursor(10, 18);
  display.print("REMINDER");
  display.drawLine(10, 27, 118, 27, SH110X_BLACK);

  String msg = reminderText;
  if (msg.length() > 19) msg = msg.substring(0, 17) + "..";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 40);
  display.print(msg);
  display.setTextColor(SH110X_WHITE);
}

void drawTimerPage() {
  if (!isTimerActive) {
    display.clearDisplay();
    display.drawRoundRect(4, 8, 120, 50, 6, SH110X_WHITE); // enlarged from the original (8,10,112,44) to fit the extra hint line below
    display.setFont(&FreeSansBold9pt7b);
    String title = isPresetSelecting
      ? ("< " + String(TIMER_PRESETS_MIN[selectedPresetIndex]) + "M >")
      : (String(TIMER_PRESETS_MIN[selectedPresetIndex]) + "M Timer");
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 28);
    display.print(title);
    display.setFont(NULL);
    display.setCursor(10, 38);
    display.print(isPresetSelecting ? "Hold: cycling" : "Dbl-tap: Start"); // was "Double-tap to Start" - that ran 14px past the border
    if (!isPresetSelecting) {
      display.setCursor(10, 48);
      display.print("Hold: change time");
    }
    return;
  }

  unsigned long now = millis();
  unsigned long remainingMs = (timerEndTime > now) ? (timerEndTime - now) : 0;
  float progress = (float)remainingMs / (float)timerDuration;
  if (progress < 0) progress = 0;
  if (progress > 1) progress = 1;

  int totalSec = remainingMs / 1000;
  int mins = totalSec / 60;
  int secs = totalSec % 60;

  // Circular Gauge (center at 34, 32)
  int cx = 34, cy = 32;
  display.drawCircle(cx, cy, 28, SH110X_WHITE);
  display.drawCircle(cx, cy, 23, SH110X_WHITE);

  // Draw radial progress track
  int maxDeg = (int)(progress * 360.0);
  for (int deg = 0; deg <= maxDeg; deg += 3) {
    float rad = (deg - 90) * 0.0174533; // deg * PI / 180
    float cosR = cos(rad);
    float sinR = sin(rad);
    for (int r = 24; r <= 27; r++) {
      display.drawPixel(cx + cosR * r, cy + sinR * r, SH110X_WHITE);
    }
  }

  // Seconds indicator dot around the circle
  float secRad = (secs * 6 - 90) * 0.0174533;
  display.fillCircle(cx + cos(secRad) * 19, cy + sin(secRad) * 19, 2, SH110X_WHITE);

  // Center icon: mini hourglass
  display.drawLine(cx - 3, cy - 4, cx + 3, cy - 4, SH110X_WHITE);
  display.drawLine(cx - 3, cy - 4, cx + 3, cy + 4, SH110X_WHITE);
  display.drawLine(cx + 3, cy - 4, cx - 3, cy + 4, SH110X_WHITE);
  display.drawLine(cx - 3, cy + 4, cx + 3, cy + 4, SH110X_WHITE);

  // Right Side Info Panel — shows WORK/BREAK during Pomodoro, otherwise the plain duration
  display.setFont(NULL);
  display.setCursor(72, 8);
  if (pomodoroActive) {
    display.print(pomodoroPhase == 0 ? "POMO: WORK" : "POMO: BREAK");
  } else {
    display.print(String(timerDuration / 60000) + "M TIMER");
  }
  display.drawLine(70, 18, 126, 18, SH110X_WHITE);

  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", mins, secs);
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(70 + (56 - w) / 2, 38);
  display.print(timeStr);

  display.setFont(NULL);
  if (isTimerCancelling) {
    // Deliberate cancel hold progress bar
    display.fillRect(70, 46, 56, 16, SH110X_BLACK);
    display.setCursor(72, 46);
    display.print("CANCEL?");
    unsigned long held = millis() - pressStartTime;
    int barW = map(constrain(held, 800, TIMER_CANCEL_HOLD_TIME), 800, TIMER_CANCEL_HOLD_TIME, 0, 50);
    display.drawRect(72, 56, 52, 6, SH110X_WHITE);
    display.fillRect(73, 57, barW, 4, SH110X_WHITE);
  } else {
    display.setCursor(74, 52);
    if ((now / 500) % 2 == 0) display.print("* RUNNING *");
    else                     display.print("  RUNNING  ");
  }
}

void startBoomAnimation() {
  isBoomActive = true;
  boomStartTime = millis();
  int cx = 64, cy = 32;
  for (int i = 0; i < 24; i++) {
    float ang = (i * 15) * 0.0174533;
    float spd = random(25, 65) / 10.0;
    boomParticles[i].x = cx;
    boomParticles[i].y = cy;
    boomParticles[i].vx = cos(ang) * spd;
    boomParticles[i].vy = sin(ang) * spd * 0.6;
  }
  leftEye.init(28, 18, 32, 32);
  rightEye.init(80, 18, 32, 32);
}

void drawBoomAnimation() {
  unsigned long elapsed = millis() - boomStartTime;
  int cx = 64, cy = 32;

  if (elapsed < 200) {
    // Phase 1: Rapid contraction & energy condensation (0-200ms)
    float t = (float)elapsed / 200.0;
    int r = 32 - (int)(t * 30);
    if (r < 2) r = 2;
    display.drawCircle(cx, cy, r, SH110X_WHITE);
    display.drawCircle(cx, cy, max(1, r / 2), SH110X_WHITE);
  } else if (elapsed < 600) {
    // Phase 2: Shockwave detonation / expanding blast circles (200-600ms)
    float t = (float)(elapsed - 200) / 400.0;
    int r = 4 + (int)(t * 84);
    display.fillCircle(cx, cy, r, SH110X_WHITE);
    if (r > 16) display.fillCircle(cx, cy, r - 14, SH110X_BLACK);
    if (r > 36) display.drawCircle(cx, cy, r - 32, SH110X_WHITE);
  } else if (elapsed < 1100) {
    // Phase 3: Radial explosion shrapnel and blast rays (600-1100ms)
    float t = (float)(elapsed - 600) / 500.0;
    int step = (int)(t * 16);
    for (int i = 0; i < 8; i++) {
      float a = (i * 45 + step * 4) * 0.0174533;
      display.drawLine(cx + cos(a)*10, cy + sin(a)*7, cx + cos(a)*(25 + step*2), cy + sin(a)*(15 + step), SH110X_WHITE);
    }
    for (int i = 0; i < 24; i++) {
      float px = boomParticles[i].x + boomParticles[i].vx * step * 1.5;
      float py = boomParticles[i].y + boomParticles[i].vy * step * 1.5;
      if (px >= 0 && px < 128 && py >= 0 && py < 64) {
        display.fillCircle((int)px, (int)py, (step < 7) ? 2 : 1, SH110X_WHITE);
      }
    }
  } else if (elapsed < 2000) {
    // Phase 4: Big "B O O M !" banner & burst rays (1100-2000ms)
    for (int i = 0; i < 16; i++) {
      float a = (i * 22.5) * 0.0174533;
      int r1 = (i % 2 == 0) ? 28 : 20;
      display.drawLine(cx + cos(a) * 16, cy + sin(a) * 12, cx + cos(a) * (r1 + 20), cy + sin(a) * (r1 + 10), SH110X_WHITE);
    }

    display.fillRoundRect(14, 14, 100, 36, 6, SH110X_WHITE);
    display.drawRoundRect(12, 12, 104, 40, 8, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setFont(&FreeSansBold9pt7b);
    String boomText = "B O O M !";
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(boomText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 38);
    display.print(boomText);
    display.setTextColor(SH110X_WHITE);
  } else if (elapsed < 3200) {
    // Phase 5: Excitement celebration reaction (2000-3200ms)
    setMood(MOOD_EXCITED, false);
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.drawBitmap(56, 0, bmp_heart, 16, 16, SH110X_WHITE);
  } else {
    // Completed
    isBoomActive = false;
    setMood(weatherMood);
    currentPage = 0;
    subPage = 0;
    if (pomodoroAdvancePending) {
      pomodoroAdvancePending = false;
      advancePomodoroPhase();
    }
  }
}

// Ends Pomodoro mode and restores the timer duration to the selected preset. Without the
// restore, the leftover 25/5 minute Pomodoro duration would silently be used for the next
// normal timer, while the idle Timer page still showed the preset.
void stopPomodoro() {
  pomodoroActive = false;
  pomodoroAdvancePending = false;
  timerDuration = (unsigned long)TIMER_PRESETS_MIN[selectedPresetIndex] * 60UL * 1000UL;
}

// Switches an active Pomodoro session between its Work and Break phases and restarts
// the underlying timer with that phase's duration. Called once the boom celebration for
// the just-finished phase has fully played out (see drawBoomAnimation()'s Completed branch).
void advancePomodoroPhase() {
  if (pomodoroPhase == 0) {
    // Work session just finished -> start a Break
    pomodoroPhase = 1;
    pomodoroCyclesCompleted++;
    timerDuration = POMODORO_BREAK_MS;
  } else {
    // Break just finished -> start the next Work session
    pomodoroPhase = 0;
    timerDuration = POMODORO_WORK_MS;
  }
  isTimerActive = true;
  timerStartTime = millis();
  timerEndTime = timerStartTime + timerDuration;
}

// ==================================================
// CAPTIVE PORTAL HANDLERS
// ==================================================
void drawPortalScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.drawRoundRect(2, 2, 124, 60, 4, SH110X_WHITE);
  display.fillRoundRect(4, 4, 120, 14, 2, SH110X_WHITE);

  display.setFont(NULL);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(20, 7);
  display.print("IRIS WIFI SETUP");

  display.setTextColor(SH110X_WHITE);
  display.setCursor(8, 20);
  display.print("AP: IRIS-Setup");

  display.setCursor(8, 31);
  display.print("Pass: ");
  display.print(AP_SETUP_PASSWORD);

  display.setCursor(8, 42);
  display.print("URL: 192.168.4.1");

  display.setCursor(8, 52);
  display.print("Open in browser"); // was "Configure in browser" - that was 120px wide and ran past the border/screen edge
  display.display();
}

// Renders RSSI as a simple bracketed bar, e.g. "[####]" (excellent) down to "[....]" (very
// weak). Deliberately plain ASCII rather than Unicode block glyphs: native <select> picker
// overlays on some older mobile browsers render box-drawing characters inconsistently, and
// this looks identical everywhere.
String signalBars(int rssi) {
  int rating; // 0-4
  if (rssi >= -50)      rating = 4;
  else if (rssi >= -60) rating = 3;
  else if (rssi >= -70) rating = 2;
  else if (rssi >= -80) rating = 1;
  else                  rating = 0;

  String bars = "[";
  for (int i = 0; i < 4; i++) bars += (i < rating) ? "#" : ".";
  bars += "]";
  return bars;
}

void handlePortalRoot() {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (s.length() > 0) {
      options += "<option value='" + s + "'>" + signalBars(WiFi.RSSI(i)) + " " + s + "</option>";
    }
  }

  String html = "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
    "<title>IRIS WiFi Setup</title>"
    "<style>"
    "*{-webkit-tap-highlight-color:transparent;}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#121212;color:#f0f0f0;margin:0;padding:20px;}"
    ".card{background:#1e1e1e;border-radius:12px;padding:20px;max-width:380px;margin:auto;box-shadow:0 4px 16px rgba(0,0,0,0.6);}"
    "h2{color:#4da6ff;text-align:center;margin:4px 0 14px;font-size:20px;}"
    "p{font-size:13px;color:#aaa;text-align:center;margin-bottom:16px;}"
    "label{display:block;font-size:12px;font-weight:600;margin:12px 0 4px;color:#ccc;}"
    "select,input{width:100%;box-sizing:border-box;padding:12px;border-radius:6px;border:1px solid #333;background:#292929;color:#fff;font-size:16px;}" // 16px prevents iOS auto-zoom on focus
    "select:focus,input:focus{border-color:#4da6ff;outline:none;}"
    ".pwrow{position:relative;}"
    ".pwrow input{padding-right:56px;}"
    ".pwtoggle{position:absolute;right:6px;top:6px;bottom:6px;width:44px;border:none;background:transparent;color:#4da6ff;font-size:12px;font-weight:600;cursor:pointer;}"
    "button.primary{width:100%;min-height:46px;padding:12px;margin-top:20px;border:none;border-radius:6px;background:#0070f3;color:#fff;font-size:15px;font-weight:bold;cursor:pointer;}"
    "button.primary:disabled{background:#2a4d75;cursor:default;}"
    "button.primary:not(:disabled):hover{background:#005bb5;}"
    "button.ghost{width:100%;min-height:40px;padding:10px;margin-top:10px;border:1px solid #444;border-radius:6px;background:transparent;color:#aaa;font-size:13px;cursor:pointer;}"
    "button.ghost:hover{border-color:#c0392b;color:#e74c3c;}"
    ".hint{font-size:11px;color:#666;margin-top:3px;}"
    "#status{text-align:center;font-size:13px;margin-top:12px;min-height:16px;}"
    "#status.ok{color:#2ecc71;}"
    "#status.err{color:#e74c3c;}"
    ".spinner{display:inline-block;width:14px;height:14px;border:2px solid #555;border-top-color:#4da6ff;border-radius:50%;animation:spin .7s linear infinite;vertical-align:middle;margin-right:6px;}"
    "@keyframes spin{to{transform:rotate(360deg);}}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>IRIS WiFi Setup</h2>"
    "<p>Select your WiFi network to connect IRIS</p>"
    "<label>Nearby WiFi Networks</label>"
    "<select id='networkSelect' onchange=\"if(this.value)document.getElementById('manual_ssid').value=this.value;\">"
    "<option value=''>-- Select WiFi Network --</option>"
    + options +
    "</select>"
    "<label>WiFi SSID</label>"
    "<input type='text' id='manual_ssid' value='" + wifiSsid + "' required placeholder='Enter SSID'>"
    "<label>WiFi Password</label>"
    "<div class='pwrow'>"
    "<input type='password' id='manual_pass' autocomplete='new-password' placeholder='Enter WiFi Password'>"
    "<button type='button' class='pwtoggle' onclick=\"var p=document.getElementById('manual_pass');var show=p.type==='password';p.type=show?'text':'password';this.textContent=show?'HIDE':'SHOW';\">SHOW</button>"
    "</div>"
    "<button type='button' class='primary' id='saveBtn' onclick='saveWifi()'>Save &amp; Connect</button>"
    "<div id='status'></div>"
    "<button type='button' class='ghost' onclick='forgetWifi()'>Forget Saved Network</button>"
    "</div>"
    "<script>"
    "function setStatus(msg,cls){var s=document.getElementById('status');s.innerHTML=msg;s.className=cls||'';}"
    "function saveWifi(){"
      "var ssid=document.getElementById('manual_ssid').value.trim();"
      "var pass=document.getElementById('manual_pass').value;"
      "if(!ssid){setStatus('Please enter or select a network first.','err');return;}"
      "var btn=document.getElementById('saveBtn');btn.disabled=true;"
      "setStatus('<span class=\"spinner\"></span>Saving and connecting...','');"
      "fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)})"
      ".then(function(r){if(!r.ok)throw new Error('save failed');return r.json();})"
      ".then(function(){setStatus('Saved! IRIS is restarting and connecting...','ok');})"
      ".catch(function(){btn.disabled=false;setStatus('Could not save — please try again.','err');});"
    "}"
    "function forgetWifi(){"
      "if(!confirm('Clear the saved WiFi network from IRIS?'))return;"
      "fetch('/forget',{method:'POST'})"
      ".then(function(r){if(!r.ok)throw new Error('forget failed');return r.json();})"
      ".then(function(){"
        "document.getElementById('manual_ssid').value='';"
        "document.getElementById('manual_pass').value='';"
        "document.getElementById('networkSelect').value='';"
        "setStatus('Saved network cleared.','ok');"
      "})"
      ".catch(function(){setStatus('Could not clear the saved network.','err');});"
    "}"
    "</script>"
    "</body></html>";

  server.send(200, "text/html", html);
}

void handlePortalSave() {
  String s = server.arg("ssid");
  String p = server.arg("pass");
  s.trim();
  p.trim();

  if (s.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID is required\"}");
    return;
  }

  preferences.begin("iris-wifi", false);
  preferences.putString("ssid", s);
  preferences.putString("pass", p);
  preferences.end();

  wifiSsid = s;
  wifiPassword = p;

  server.send(200, "application/json", "{\"ok\":true}");
  delay(1000); // give the response time to actually leave before we tear down the AP
  ESP.restart();
}

// Clears the saved network without restarting — used by the "Forget Saved Network" button
// on this same portal page, so a wrong pre-filled SSID/password can be wiped before saving
// a new one, without needing to wait for anything.
void handlePortalForget() {
  preferences.begin("iris-wifi", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();
  wifiSsid = "";
  wifiPassword = "";
  server.send(200, "application/json", "{\"ok\":true}");
}

// Named function instead of a lambda for onNotFound() — keeps every route handler on this
// server consistently a plain named function, matching the rest of the file.
void handlePortalNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void startCaptivePortal() {
  inPortalMode = true;
  Serial.println("\n[WiFi] ==================================");
  Serial.println("[WiFi] Starting IRIS Captive Portal...");
  Serial.println("[WiFi] ==================================");

  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);

  // Set lower TX power to prevent voltage brownout reset on ESP32-C3 Super Mini LDO
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_max_tx_power(WIFI_POWER_8_5dBm);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool apSuccess = WiFi.softAP("IRIS-Setup", AP_SETUP_PASSWORD, 1, 0, 4);

  if (apSuccess) {
    Serial.println("[WiFi] SoftAP 'IRIS-Setup' successfully started!");
    Serial.print("[WiFi] AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[WiFi ERROR] Failed to start SoftAP 'IRIS-Setup'!");
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/save", HTTP_POST, handlePortalSave);
  server.on("/forget", HTTP_POST, handlePortalForget);
  server.on("/generate_204", handlePortalRoot);
  server.on("/hotspot-detect.html", handlePortalRoot);
  server.on("/canonical.html", handlePortalRoot);
  server.on("/connecttest.txt", handlePortalRoot);
  server.on("/ncsi.txt", handlePortalRoot);
  server.onNotFound(handlePortalNotFound);
  server.begin();
  Serial.println("[WiFi] Captive Portal Web Server started at http://192.168.4.1");
  drawPortalScreen();
}

void handleCaptivePortal() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Throttle screen redraw so it does not starve Wi-Fi / DNS processing
  static unsigned long lastPortalDraw = 0;
  if (millis() - lastPortalDraw > 1000) {
    lastPortalDraw = millis();
    drawPortalScreen();
  }
}

// ==================================================
// ON-DEVICE DASHBOARD (runs during normal STA operation, not just setup)
// ==================================================
const char* moodIntToName(int m) {
  switch (m) {
    case MOOD_NORMAL:     return "Normal";
    case MOOD_HAPPY:      return "Happy";
    case MOOD_SURPRISED:  return "Surprised";
    case MOOD_SLEEPY:     return "Sleepy";
    case MOOD_ANGRY:      return "Angry";
    case MOOD_SAD:        return "Sad";
    case MOOD_EXCITED:    return "Excited";
    case MOOD_LOVE:       return "Love";
    case MOOD_SUSPICIOUS: return "Suspicious";
    case MOOD_DIZZY:      return "Dizzy";
    case MOOD_GLOOMY:     return "Gloomy";
    case MOOD_RECOVERING: return "Recovering";
    default:              return "Unknown";
  }
}

// Only the same curated set of moods the physical long-press mood picker offers
// (SELECTABLE_MOODS) can be set from the Dashboard — states like Dizzy/Angry/Recovering
// are reactions to real events and don't make sense to force from a web button.
int moodNameToSelectableInt(const String& nameIn) {
  String n = nameIn;
  n.toLowerCase();
  for (int i = 0; i < NUM_SELECTABLE_MOODS; i++) {
    String candidate = String(MOOD_NAMES[i]);
    candidate.toLowerCase();
    if (n == candidate) return SELECTABLE_MOODS[i];
  }
  return -1;
}

void handleDashboardRoot() {
  String html = "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
    "<title>IRIS Dashboard</title>"
    "<style>"
    "*{-webkit-tap-highlight-color:transparent;box-sizing:border-box;}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#121212;color:#f0f0f0;margin:0;padding:16px;}"
    ".card{background:#1e1e1e;border-radius:12px;padding:18px;max-width:420px;margin:0 auto 14px;box-shadow:0 4px 16px rgba(0,0,0,0.6);}"
    "h1{color:#4da6ff;text-align:center;font-size:20px;margin:4px 0 16px;}"
    "h3{font-size:13px;color:#888;text-transform:uppercase;letter-spacing:.05em;margin:0 0 10px;}"
    ".stat-row{display:flex;justify-content:space-between;font-size:14px;padding:6px 0;border-bottom:1px solid #2a2a2a;}"
    ".stat-row:last-child{border-bottom:none;}"
    ".stat-row b{color:#fff;}"
    ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;}"
    "button{min-height:42px;padding:8px;border:none;border-radius:6px;background:#2a2a2a;color:#eee;font-size:13px;cursor:pointer;}"
    "button:hover{background:#333;}"
    "button.active{background:#0070f3;color:#fff;}"
    "button.wide{grid-column:1/-1;background:#0070f3;color:#fff;font-weight:bold;}"
    "button.wide:hover{background:#005bb5;}"
    "button.danger{grid-column:1/-1;background:transparent;border:1px solid #5a2a2a;color:#e88;margin-top:4px;}"
    "button.danger:hover{border-color:#e74c3c;color:#fff;background:#3a1a1a;}"
    "input{width:100%;padding:10px;border-radius:6px;border:1px solid #333;background:#292929;color:#fff;font-size:16px;margin-bottom:8px;}"
    "label{display:block;font-size:12px;color:#ccc;margin:8px 0 4px;}"
    "#toast{text-align:center;font-size:13px;min-height:16px;margin-top:8px;}"
    "#toast.ok{color:#2ecc71;} #toast.err{color:#e74c3c;}"
    "</style></head><body>"
    "<h1>IRIS Dashboard</h1>"

    "<div class='card'><h3>Status</h3>"
    "<div class='stat-row'><span>Mood</span><b id='s-mood'>-</b></div>"
    "<div class='stat-row'><span>Weather</span><b id='s-weather'>-</b></div>"
    "<div class='stat-row'><span>Timer</span><b id='s-timer'>-</b></div>"
    "<div class='stat-row'><span>Time</span><b id='s-time'>-</b></div>"
    "<div class='stat-row'><span>WiFi</span><b id='s-wifi'>-</b></div>"
    "</div>"

    "<div class='card'><h3>Set Mood</h3><div class='grid' id='moodGrid'></div></div>"

    "<div class='card'><h3>Timer</h3><div class='grid'>"
    "<button onclick=\"timerAction('start',5)\">5 min</button>"
    "<button onclick=\"timerAction('start',15)\">15 min</button>"
    "<button onclick=\"timerAction('start',30)\">30 min</button>"
    "<button onclick=\"timerAction('start',60)\">60 min</button>"
    "<button onclick=\"timerAction('stop')\">Stop</button>"
    "<button onclick=\"timerAction('pomodoro_start')\">Pomodoro</button>"
    "</div></div>"

    "<div class='card'><h3>Sticky-Note Reminder</h3>"
    "<label>Reminder text</label><input id='remText' maxlength='40' placeholder=\"e.g. Stand up and stretch\">"
    "<label>Time</label><input id='remTime' type='time'>"
    "<div class='grid'>"
    "<button class='wide' onclick='setReminder()'>Set Reminder</button>"
    "<button class='danger' onclick='clearReminder()'>Clear Reminder</button>"
    "</div></div>"

    "<div class='card'><h3>WiFi</h3>"
    "<div class='grid'><button class='danger' onclick='forgetWifi()'>Forget WiFi &amp; Reconfigure</button></div>"
    "</div>"

    "<div id='toast'></div>"

    "<script>"
    "var MOODS=" + String("['Happy','Love','Sleepy','Excited','Gloomy','Sad','Normal']") + ";"
    "var grid=document.getElementById('moodGrid');"
    "MOODS.forEach(function(m){"
      "var b=document.createElement('button');b.textContent=m;"
      "b.onclick=function(){setMood(m,b);};grid.appendChild(b);"
    "});"
    "function toast(msg,cls){var t=document.getElementById('toast');t.textContent=msg;t.className=cls||'';}"
    "function refresh(){"
      "fetch('/api/state').then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('s-mood').textContent=d.mood;"
        "document.getElementById('s-weather').textContent=d.weather+' '+Math.round(d.tempC)+'\\u00B0C';"
        "document.getElementById('s-timer').textContent=d.timerActive?"
          "(d.pomodoroActive?('POMO '+d.pomodoroPhase.toUpperCase()+' '):'')+"
          "Math.floor(d.timerRemainingSec/60)+':'+String(d.timerRemainingSec%60).padStart(2,'0'):'Idle';"
        "document.getElementById('s-time').textContent=d.time;"
        "document.getElementById('s-wifi').textContent=d.ssid+' ('+d.rssi+' dBm)';"
        "Array.from(grid.children).forEach(function(btn){btn.classList.toggle('active',btn.textContent===d.mood);});"
      "}).catch(function(){});"
    "}"
    "function setMood(m,btn){"
      "fetch('/api/mood',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'mood='+encodeURIComponent(m)})"
      ".then(function(r){if(!r.ok)throw 0;toast('Mood set to '+m,'ok');refresh();})"
      ".catch(function(){toast('Could not set mood','err');});"
    "}"
    "function timerAction(action,minutes){"
      "var body='action='+encodeURIComponent(action);"
      "if(minutes)body+='&minutes='+minutes;"
      "fetch('/api/timer',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
      ".then(function(r){if(!r.ok)throw 0;toast('Timer updated','ok');refresh();})"
      ".catch(function(){toast('Could not update timer','err');});"
    "}"
    "function setReminder(){"
      "var text=document.getElementById('remText').value.trim();"
      "var time=document.getElementById('remTime').value;"
      "if(!text||!time){toast('Enter both text and a time','err');return;}"
      "fetch('/api/reminder',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'text='+encodeURIComponent(text)+'&time='+encodeURIComponent(time)})"
      ".then(function(r){if(!r.ok)throw 0;toast('Reminder set for '+time,'ok');})"
      ".catch(function(){toast('Could not set reminder','err');});"
    "}"
    "function clearReminder(){"
      "fetch('/api/reminder',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'clear=1'})"
      ".then(function(r){if(!r.ok)throw 0;toast('Reminder cleared','ok');})"
      ".catch(function(){toast('Could not clear reminder','err');});"
    "}"
    "function forgetWifi(){"
      "if(!confirm('IRIS will forget this WiFi network and restart into Setup mode. Continue?'))return;"
      "fetch('/api/forget-wifi',{method:'POST'})"
      ".then(function(){toast('Forgetting WiFi and restarting...','ok');})"
      ".catch(function(){toast('Could not reach IRIS — it may already be restarting.','err');});"
    "}"
    "refresh();setInterval(refresh,3000);"
    "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleApiState() {
  JSONVar obj;
  obj["mood"] = String(moodIntToName(currentMood));

  float tempCopy; String mainCopy, descCopy;
  if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    tempCopy = temperature; mainCopy = weatherMain; descCopy = weatherDesc;
    xSemaphoreGive(weatherMutex);
  } else {
    tempCopy = temperature; mainCopy = weatherMain; descCopy = weatherDesc;
  }
  obj["tempC"] = tempCopy;
  obj["weather"] = mainCopy;
  obj["weatherDesc"] = descCopy;

  obj["timerActive"] = isTimerActive;
  obj["pomodoroActive"] = pomodoroActive;
  obj["pomodoroPhase"] = String(pomodoroPhase == 0 ? "work" : "break");
  unsigned long remain = (isTimerActive && timerEndTime > millis()) ? (timerEndTime - millis()) : 0;
  obj["timerRemainingSec"] = (int)(remain / 1000);
  obj["timerPresetMin"] = TIMER_PRESETS_MIN[selectedPresetIndex];

  obj["reminderArmed"] = reminderArmed;
  if (reminderArmed) {
    char buf[6];
    sprintf(buf, "%02d:%02d", reminderHour, reminderMinute);
    obj["reminderTime"] = String(buf);
    obj["reminderText"] = reminderText;
  }

  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[6];
    sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    obj["time"] = String(buf);
  } else {
    obj["time"] = String("--:--");
  }

  obj["ssid"] = wifiSsid;
  obj["rssi"] = WiFi.RSSI();

  server.send(200, "application/json", JSON.stringify(obj));
}

void handleApiSetMood() {
  int moodInt = moodNameToSelectableInt(server.arg("mood"));
  if (moodInt < 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown mood\"}");
    return;
  }
  moodManualOverride = true;
  setMood(moodInt);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiTimer() {
  String action = server.arg("action");
  unsigned long now = millis();

  if (action == "start") {
    stopPomodoro(); // also resets any leftover Pomodoro duration before applying the requested one
    if (server.hasArg("minutes")) {
      int mins = server.arg("minutes").toInt();
      if (mins > 0 && mins <= 180) timerDuration = (unsigned long)mins * 60UL * 1000UL;
    }
    isTimerActive = true;
    timerStartTime = now;
    timerEndTime = now + timerDuration;
  } else if (action == "stop") {
    isTimerActive = false;
    stopPomodoro();
    currentPage = 0;
    subPage = 0;
    startBoomAnimation();
  } else if (action == "pomodoro_start") {
    pomodoroActive = true;
    pomodoroPhase = 0;
    pomodoroCyclesCompleted = 0;
    timerDuration = POMODORO_WORK_MS;
    isTimerActive = true;
    timerStartTime = now;
    timerEndTime = now + timerDuration;
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown action\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiReminder() {
  if (server.hasArg("clear")) {
    reminderArmed = false;
    reminderText = "";
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  String text = server.arg("text");
  String timeStr = server.arg("time"); // "HH:MM" from an <input type="time">
  text.trim();

  int colonIdx = timeStr.indexOf(':');
  if (text.length() == 0 || colonIdx < 1) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"text and time (HH:MM) are required\"}");
    return;
  }
  int hh = timeStr.substring(0, colonIdx).toInt();
  int mm = timeStr.substring(colonIdx + 1).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid time\"}");
    return;
  }

  if (text.length() > 40) text = text.substring(0, 40); // it's a sticky note, not an essay
  reminderText = text;
  reminderHour = hh;
  reminderMinute = mm;
  reminderArmed = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

// Clears saved WiFi and restarts straight into Setup mode — the same proven, fully-tested
// path handlePortalSave() already uses (send response, brief delay, ESP.restart()), rather
// than trying to hot-swap AP/STA mode live from inside this request handler's own call stack.
void handleApiForgetWifi() {
  preferences.begin("iris-wifi", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();

  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

// Named function instead of a lambda for onNotFound() — keeps every route handler on this
// server consistently a plain named function, matching the rest of the file.
void handleDashboardNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startDashboardServer() {
  if (dashboardStarted) return;
  dashboardStarted = true;

  server.on("/", HTTP_GET, handleDashboardRoot);
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/api/mood", HTTP_POST, handleApiSetMood);
  server.on("/api/timer", HTTP_POST, handleApiTimer);
  server.on("/api/reminder", HTTP_POST, handleApiReminder);
  server.on("/api/forget-wifi", HTTP_POST, handleApiForgetWifi);
  server.onNotFound(handleDashboardNotFound);
  server.begin();

  Serial.println("[Web] ==================================");
  Serial.print("[Web] Dashboard: http://");
  Serial.println(WiFi.localIP());
  if (MDNS.begin("iris")) {
    Serial.println("[Web] Also reachable at: http://iris.local");
  } else {
    Serial.println("[Web] mDNS failed to start — use the IP address above instead.");
  }
  Serial.println("[Web] ==================================");
}

void playBootAnimation() {
  display.setTextColor(SH110X_WHITE);
  int cx = 64, cy = 32;
  for (int r = 0; r < 80; r += 4) {
    display.clearDisplay(); display.fillCircle(cx, cy, r, SH110X_WHITE); display.display(); delay(8);
  }
  for (int r = 0; r < 80; r += 4) {
    display.clearDisplay();
    display.fillCircle(cx, cy, 80, SH110X_WHITE);
    display.fillCircle(cx, cy, r, SH110X_BLACK);
    display.display(); delay(8);
  }
  display.setFont(&FreeSansBold9pt7b);
  String bootText = "I R I S";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(bootText, 0, 0, &x1, &y1, &w, &h);
  display.clearDisplay();
  display.setCursor((SCREEN_WIDTH - w) / 2, 36);
  display.print(bootText);
  display.display();
  delay(2000);
}

void playWakeUpAnimation() {
  // Start with eyes closed / sleepy
  leftEye.init(28, 18, 32, 2);
  rightEye.init(80, 18, 32, 2);
  setMood(MOOD_SLEEPY, false);

  // 1. Sleeping with Zzz (700ms)
  unsigned long start = millis();
  while (millis() - start < 700) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.drawBitmap(110, 0, bmp_zzz, 16, 16, SH110X_WHITE);
    display.display();
    delay(20);
  }

  // 2. Groggy first peek: eyes open halfway to height 14 (500ms)
  leftEye.targetH = rightEye.targetH = 14;
  start = millis();
  while (millis() - start < 500) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 3. Drowsy blink back to 4 (300ms)
  leftEye.targetH = rightEye.targetH = 4;
  start = millis();
  while (millis() - start < 300) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 4. Wide awake! Eyes open fully to 32 and switch to Happy (400ms)
  leftEye.targetH = rightEye.targetH = 32;
  setMood(MOOD_HAPPY, false);
  start = millis();
  while (millis() - start < 400) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 5. Look left (350ms)
  leftEye.targetPupilX = rightEye.targetPupilX = -8;
  start = millis();
  while (millis() - start < 350) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 6. Look right (350ms)
  leftEye.targetPupilX = rightEye.targetPupilX = 8;
  start = millis();
  while (millis() - start < 350) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 7. Center pupils (300ms)
  leftEye.targetPupilX = rightEye.targetPupilX = 0;
  start = millis();
  while (millis() - start < 300) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }
}

void showConnectingWifiScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.drawRoundRect(8, 8, 112, 48, 6, SH110X_WHITE);
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t w, h;
  String text = "Connecting";
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 28);
  display.print(text);

  display.setFont(NULL);
  String sub = "WiFi...";
  display.getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 40);
  display.print(sub);

  display.display();
  delay(1000); // exactly 1 second on screen
}

// Plays a short "confused / searching" face animation instead of a static text box
// when the saved network can't be found (or reconnection keeps failing) — keeps IRIS's
// face-driven personality even while it's telling you something's wrong. Reuses the
// same MOOD_SUSPICIOUS look (furrowed side-eye + question mark) already used for pokes.
void playConfusedSearchingAnimation() {
  setMood(MOOD_SUSPICIOUS);
  unsigned long start = millis();
  const unsigned long duration = 2000;

  while (millis() - start < duration) {
    unsigned long elapsed = millis() - start;
    display.clearDisplay();

    // Restless side-to-side searching gaze, quicker than IRIS's normal idle glances
    float sweep = sin(elapsed * 0.006) * 10.0;
    leftEye.targetPupilX = rightEye.targetPupilX = sweep;
    leftEye.targetPupilY = rightEye.targetPupilY = sin(elapsed * 0.004) * 3.0;

    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    // No drawMouth() here on purpose: the suspicious-mood mouth line sits at y~52-56,
    // right where the caption below needs to be — the eyes + bobbing "?" already read
    // as "confused" without it, so dropping it avoids the two overlapping.

    // Bobbing question marks on both sides instead of the usual one-shot 400ms flash
    int bob = (int)(sin(elapsed * 0.01) * 2);
    display.drawBitmap(18, 2 - bob, bmp_question, 8, 12, SH110X_WHITE);
    display.drawBitmap(102, 2 + bob, bmp_question, 8, 12, SH110X_WHITE);

    // Small unobtrusive caption — no boxed text screen. y=53 sits below the eyes
    // (bottom edge ~50) and its ~8px height ends at 61, safely inside the 64px screen.
    display.setFont(NULL);
    display.setTextColor(SH110X_WHITE);
    String caption = (elapsed < duration / 2) ? "Can't find it..." : "Opening Setup AP...";
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(caption, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 53);
    display.print(caption);

    display.display();
    delay(20);
  }

  // Settle pupils back to center before the portal screen takes over
  leftEye.targetPupilX = rightEye.targetPupilX = 0;
  leftEye.targetPupilY = rightEye.targetPupilY = 0;
}

void startWifiConnection() {
  if (wifiSsid.length() == 0) {
    Serial.println("[WiFi] No WiFi SSID configured. Starting Captive Portal...");
    startCaptivePortal();
    return;
  }

  // Quick async scan first: if the saved SSID isn't visible at all, it's genuinely
  // out of range (or off) — no point waiting through a full 15s connection timeout,
  // let alone several of them, to find that out.
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_max_tx_power(WIFI_POWER_8_5dBm);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false); // async = true, don't include hidden networks
  wifiScanStartTime = millis();
  wifiState = WIFI_STATE_SCANNING;
  Serial.print("[WiFi] Scanning for '");
  Serial.print(wifiSsid);
  Serial.println("' before connecting...");
}

// Actually joins the saved network. Only called once the scan has confirmed the SSID
// is nearby (or the scan itself timed out/failed, in which case we try anyway).
void beginWifiConnectionAttempt() {
  WiFi.disconnect();
  delay(50);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  wifiConnectStartTime = millis();
  wifiState = WIFI_STATE_CONNECTING;
  Serial.print("[WiFi] Connecting to '");
  Serial.print(wifiSsid);
  Serial.println("'...");
}

void onWifiConnectedCelebration() {
  // 1. Show excited face for 1000ms
  setMood(MOOD_EXCITED);
  leftEye.targetH = rightEye.targetH = 32;
  leftEye.targetW = rightEye.targetW = 34;
  leftEye.targetPupilX = rightEye.targetPupilX = 0;
  leftEye.targetPupilY = rightEye.targetPupilY = -2;

  unsigned long start = millis();
  while (millis() - start < 1000) {
    display.clearDisplay();
    updateEyePhysics();
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    drawMouth();
    display.display();
    delay(20);
  }

  // 2. Show "WiFi Connected!" for half a second (500ms)
  display.clearDisplay();
  display.drawRoundRect(8, 8, 112, 48, 6, SH110X_WHITE);
  display.fillRoundRect(10, 10, 108, 44, 4, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t w, h;
  String text = "WiFi Connected!";
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 36);
  display.print(text);
  display.display();
  delay(500); // 0.5s

  display.setTextColor(SH110X_WHITE);

  // 3. Reset eyes and return to normal happy face
  leftEye.targetW = rightEye.targetW = 32;
  leftEye.targetPupilY = rightEye.targetPupilY = 0;
  setMood(MOOD_HAPPY);

  // 4. Sync NTP time
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", TIMEZONE, 1);
  tzset();

  // 5. Wake up background FreeRTOS weather task immediately
  if (weatherTaskHandle != NULL) {
    xTaskNotifyGive(weatherTaskHandle);
  }

  // 6. Start the on-device Dashboard (idempotent — safe to call again on later reconnects)
  bool isFirstDashboardStart = !dashboardStarted;
  startDashboardServer();

  // 7. On the very first connection only, show where to find the Dashboard. Skipped on
  // later background reconnects (e.g. after a brief WiFi drop) so it doesn't keep
  // interrupting normal use every time the connection flaps.
  if (isFirstDashboardStart) {
    display.clearDisplay();
    display.drawRoundRect(4, 8, 120, 48, 6, SH110X_WHITE);
    display.setFont(NULL);
    display.setCursor(10, 14);
    display.print("Dashboard:");
    display.setCursor(10, 26);
    display.print(WiFi.localIP().toString());
    display.setCursor(10, 40);
    display.print("or iris.local");
    display.display();
    delay(3000);
  }
}

void updateWifiBackground() {
  unsigned long now = millis();
  wl_status_t status = WiFi.status();

  if (wifiState == WIFI_STATE_SCANNING) {
    int16_t scanResult = WiFi.scanComplete();

    if (scanResult == WIFI_SCAN_RUNNING) {
      if (now - wifiScanStartTime > WIFI_SCAN_TIMEOUT) {
        // Scan is taking too long — give it the benefit of the doubt and just try connecting.
        Serial.println("[WiFi] Scan timed out. Attempting to connect anyway...");
        WiFi.scanDelete();
        beginWifiConnectionAttempt();
      }
      return; // still scanning — check again next loop, don't fall through below
    }

    if (scanResult == WIFI_SCAN_FAILED) {
      Serial.println("[WiFi] Scan failed. Attempting to connect anyway...");
      beginWifiConnectionAttempt();
      return;
    }

    bool found = false;
    for (int i = 0; i < scanResult; i++) {
      if (WiFi.SSID(i) == wifiSsid) { found = true; break; }
    }
    WiFi.scanDelete();

    if (found) {
      Serial.println("[WiFi] Saved network is in range. Connecting...");
      beginWifiConnectionAttempt();
    } else {
      // Genuinely out of range — skip the retry counter entirely and go straight
      // to the Setup AP instead of burning through connection timeouts for nothing.
      Serial.println("[WiFi] Saved network not found nearby. Skipping straight to Setup AP...");
      wifiFailedAttempts = 0;
      playConfusedSearchingAnimation();
      startCaptivePortal();
    }
    return;
  }

  if (wifiState == WIFI_STATE_CONNECTING) {
    if (status == WL_CONNECTED) {
      wifiState = WIFI_STATE_CONNECTED;
      wifiFailedAttempts = 0; // saved credentials work — clear the failure count
      onWifiConnectedCelebration();
    } else if (now - wifiConnectStartTime > WIFI_TIMEOUT) {
      wifiState = WIFI_STATE_FAILED;
      lastWifiRetryTime = now;
      wifiFailedAttempts++;
      Serial.print("[WiFi] Connection attempt failed or timed out. (");
      Serial.print(wifiFailedAttempts);
      Serial.print("/");
      Serial.print(WIFI_MAX_ATTEMPTS_BEFORE_PORTAL);
      Serial.println(")");

      if (wifiFailedAttempts >= WIFI_MAX_ATTEMPTS_BEFORE_PORTAL) {
        // Saved network is unreachable (likely out of range) after repeated tries —
        // reopen the Setup AP automatically so a different network can be configured.
        Serial.println("[WiFi] Saved network unreachable. Reopening IRIS-Setup so a new WiFi can be configured...");
        wifiFailedAttempts = 0;
        playConfusedSearchingAnimation();
        startCaptivePortal();
        return;
      }
    }
  } else if (wifiState == WIFI_STATE_CONNECTED) {
    if (status != WL_CONNECTED) {
      wifiState = WIFI_STATE_FAILED;
      lastWifiRetryTime = now;
      Serial.println("[WiFi] Disconnected. Will retry connecting in background...");
    }
  } else if (wifiState == WIFI_STATE_FAILED || wifiState == WIFI_STATE_DISCONNECTED) {
    if (now - lastWifiRetryTime >= WIFI_RETRY_INTERVAL) {
      Serial.println("[WiFi] Retrying saved WiFi in background...");
      startWifiConnection();
    }
  }
}

// ==================================================
// SETUP
// ==================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n====================================");
  Serial.println("IRIS ESP32-C3 Super Mini Booting...");
  Serial.println("====================================");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast I2C mode for smooth 60fps rendering
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  if (!display.begin(0x3C, true)) {
    Serial.println("[Display] Note: SH1106 not found on 0x3C. If connected, check SDA(6) & SCL(7).");
  } else {
    Serial.println("[Display] OLED initialized.");
    display.setTextColor(SH110X_WHITE);
    display.clearDisplay();
  }

  // Load saved WiFi configuration from NVS
  preferences.begin("iris-wifi", false);
  wifiSsid     = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("pass", "");
  preferences.end();

  Serial.print("[Config] Stored SSID: '");
  Serial.print(wifiSsid);
  Serial.println("'");

  // If no SSID configured or if user touches/holds TOUCH_PIN at boot, enter Captive Portal mode
  if (wifiSsid.length() == 0 || digitalRead(TOUCH_PIN) == HIGH) {
    startCaptivePortal();
    return;
  }

  mpu6050.begin();
  calibrateMPU6050();

  leftEye.init(28, 18, 32, 32);
  rightEye.init(80, 18, 32, 32);

  // Initialize FreeRTOS mutex and background weather task
  weatherMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(
    weatherTaskCode,
    "WeatherTask",
    8192,
    NULL,
    1,
    &weatherTaskHandle,
    0
  );

  // 1. Splash circle animation and "I R I S" name
  playBootAnimation();

  // 2. Waking up face animation
  playWakeUpAnimation();

  // 3. "Connecting to WiFi..." screen for 1 second
  showConnectingWifiScreen();

  // 4. Start background WiFi connection non-blockingly
  startWifiConnection();

  // 5. Switch to normal happy face immediately
  currentPage = 0;
  subPage = 0;
  setMood(MOOD_HAPPY, false);
}

// ==================================================
// RENDERING & PAGE TRANSITIONS
// ==================================================

// Draws whatever should currently be on screen into the display's buffer.
// Does NOT call clearDisplay() or display() itself — callers own that, so this same
// dispatch can be reused both for an instant render and for capturing a transition frame.
void renderCurrentFrame() {
  if (isBoomActive) {
    drawBoomAnimation();
  } else if (isReminderPopupActive) {
    drawReminderPopup();
  } else if (isMinutePopupActive) {
    drawClockPage();
  } else if (currentPage == 0) {
    drawEmoPage();
  } else if (currentPage == 1) {
    if (subPage == 1) drawWorldClockPage();
    else drawClockPage();
  } else if (currentPage == 2) {
    if (subPage == 2) drawForecastPage();
    else drawWeatherPage();
  } else if (currentPage == 3) {
    drawTimerPage();
  }
}

// 1-bit-per-pixel frame snapshot, in our OWN row-major/MSB-first layout — deliberately NOT
// tied to whatever internal buffer format the display driver happens to use, since we only
// ever touch it through the public getPixel()/drawPixel() API. 128*64/8 = 1024 bytes.
#define FRAME_BUF_BYTES ((SCREEN_WIDTH * SCREEN_HEIGHT) / 8)

void captureFrame(uint8_t* buf) {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      int bitIndex = y * SCREEN_WIDTH + x;
      uint8_t mask = 0x80 >> (bitIndex % 8);
      if (display.getPixel(x, y)) buf[bitIndex / 8] |= mask;
      else                        buf[bitIndex / 8] &= ~mask;
    }
  }
}

bool getFrameBit(const uint8_t* buf, int x, int y) {
  int bitIndex = y * SCREEN_WIDTH + x;
  return (buf[bitIndex / 8] & (0x80 >> (bitIndex % 8))) != 0;
}

// Quick left-to-right wipe: the new page sweeps in from the left edge over ~6 steps
// (~150-200ms total, dominated by the I2C push time of each display.display() call)
// instead of the old instant clear-and-redraw cut.
void playPageTransition() {
  static uint8_t oldFrame[FRAME_BUF_BYTES];
  static uint8_t newFrame[FRAME_BUF_BYTES];

  // 1. Whatever is already on screen from the previous frame IS the "old" page — capture it
  //    before drawing anything new over it.
  captureFrame(oldFrame);

  // 2. Render the new page into the buffer (off-screen from the user's perspective, since we
  //    haven't called display.display() yet) and capture that as the "new" page.
  display.clearDisplay();
  renderCurrentFrame();
  captureFrame(newFrame);

  // 3. Wipe: reveal progressively more of the new frame from the left, showing the old
  //    frame's remainder on the right, until the whole screen is the new page.
  const int STEPS = 6;
  for (int step = 1; step <= STEPS; step++) {
    int revealX = (SCREEN_WIDTH * step) / STEPS;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
      for (int x = 0; x < SCREEN_WIDTH; x++) {
        bool useNew = x < revealX;
        bool on = useNew ? getFrameBit(newFrame, x, y) : getFrameBit(oldFrame, x, y);
        display.drawPixel(x, y, on ? SH110X_WHITE : SH110X_BLACK);
      }
    }
    display.display();
  }
}

// ==================================================
// LOOP
// ==================================================
void loop() {
  if (inPortalMode) {
    handleCaptivePortal();
    delay(10);
    return;
  }

  handleTouch();
  updateMotion();
  updateWifiBackground();

  // If the saved network was just found unreachable, updateWifiBackground() will have
  // reopened the Setup AP (inPortalMode == true). Skip the rest of this frame so the
  // portal screen isn't immediately overdrawn by the face/clock/weather rendering below.
  if (inPortalMode) return;

  // Service the on-device Dashboard (only running once WiFi has connected at least once)
  if (dashboardStarted) {
    server.handleClient();
  }

  if (millis() - lastContrastCheckTime > CONTRAST_CHECK_INTERVAL) {
    lastContrastCheckTime = millis();
    updateDisplayContrast();
  }

  // Handle Timer auto-completion -> non-blocking BOOM! (also covers each Pomodoro phase ending)
  if (isTimerActive && millis() >= timerEndTime) {
    isTimerActive = false;
    currentPage = 0;
    subPage = 0;
    startBoomAnimation();
    if (pomodoroActive) {
      // Don't advance the phase immediately — let the boom celebration play out fully first.
      // The actual phase switch happens in drawBoomAnimation()'s "Completed" branch below.
      pomodoroAdvancePending = true;
    }
  }

  // Handle 3-second time display on every minute change (non-blocking 0ms)
  // Guarded against interrupting active interactions, emotions, or navigation
  struct tm t;
  if (getLocalTime(&t, 0)) {
    if (lastObservedMinute != -1 && t.tm_min != lastObservedMinute) {
      if (!isTimerActive && !isWeatherPopupActive && !isShaking && !isAngry && 
          !isRecovering && !isSuspicious && !isTouchAngry && !isBoomActive && 
          !isPetting && !isMoodSelecting && (millis() - lastUserInteractionTime > 3000)) {
        isMinutePopupActive = true;
        minutePopupEndTime = millis() + MINUTE_POPUP_DURATION;
      }
    }
    lastObservedMinute = t.tm_min;
  }

  // Handle minute change popup expiration
  if (isMinutePopupActive && millis() >= minutePopupEndTime) {
    isMinutePopupActive = false;
  }

  // Fire the sticky-note reminder at its scheduled time (set via the Dashboard). Fires once
  // (reminderArmed is cleared immediately) so it can't re-trigger repeatedly within the same minute.
  if (reminderArmed) {
    struct tm rt;
    if (getLocalTime(&rt, 0) && rt.tm_hour == reminderHour && rt.tm_min == reminderMinute) {
      if (!isBoomActive && !isShaking && !isAngry && !isRecovering) {
        reminderArmed = false;
        isReminderPopupActive = true;
        reminderPopupEndTime = millis() + REMINDER_POPUP_DURATION;
        if (currentPage != 0 || subPage != 0) pageTransitionPending = true;
        currentPage = 0;
        subPage = 0;
      }
    }
  }

  // Handle reminder popup expiration
  if (isReminderPopupActive && millis() >= reminderPopupEndTime) {
    isReminderPopupActive = false;
  }

  // Handle 20-second weather popup expiration
  if (isWeatherPopupActive && millis() >= weatherPopupEndTime) {
    isWeatherPopupActive = false;
    currentPage = 0;
    subPage = 0;
  }

  // Handle newly arrived background weather data
  // Only auto-pop if the user has been idle on the Face page (>10s) and not actively interacting
  if (weatherDataReady) {
    weatherDataReady = false;
    if (currentPage == 0 && !isTimerActive && !isBoomActive && 
        !isShaking && !isAngry && !isRecovering && !isSuspicious && 
        !isTouchAngry && !isPetting && !isMoodSelecting && 
        (millis() - lastUserInteractionTime > 10000)) {
      isWeatherPopupActive = true;
      weatherPopupEndTime = millis() + WEATHER_POPUP_DURATION;
      currentPage = 2;
      subPage = 0;
    }
  }

  // Weather-change reaction: only plays if IRIS is idle on the Face page and not already
  // in the middle of some other exclusive animation/interaction. If those conditions aren't
  // met right now, the reaction is simply dropped rather than queued for later — by the time
  // things free up, the moment (and relevance) of that particular reading has usually passed.
  if (pendingWeatherReaction != WEATHER_REACTION_NONE) {
    int reaction = pendingWeatherReaction;
    pendingWeatherReaction = WEATHER_REACTION_NONE;
    if (currentPage == 0 && !isBoomActive && !isShaking && !isAngry && !isRecovering &&
        !isSuspicious && !isTouchAngry && !isPetting && !isMoodSelecting && !isTimerCancelling) {
      playWeatherChangeReaction(reaction);
    }
  }

  if (pageTransitionPending && !isBoomActive && !isMinutePopupActive) {
    pageTransitionPending = false;
    playPageTransition();
  } else {
    pageTransitionPending = false; // clear defensively even if a transition got skipped this frame
    display.clearDisplay();
    renderCurrentFrame();
    display.display();
  }
  delay(15);
}
