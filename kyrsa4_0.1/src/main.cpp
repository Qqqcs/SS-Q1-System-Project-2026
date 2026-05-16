#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <Preferences.h>

#if __has_include("../include/secrets.h")
  #include "../include/secrets.h"
#elif __has_include("secrets.h")
  #include "secrets.h"
#endif

// ================= CONFIG =================
#ifndef USE_WOKWI
#define USE_WOKWI 0
#endif

#ifndef USE_ST7789
  #if USE_WOKWI
    #define USE_ST7789 0
  #else
    #define USE_ST7789 1
  #endif
#endif

#if USE_ST7789
  #include <Adafruit_ST7789.h>
#else
  #include <Adafruit_ILI9341.h>
#endif

// ================= WIFI DATA =================
// Real Wi-Fi credentials live in include/secrets.h.
#ifndef REAL_WIFI_SSID
#define REAL_WIFI_SSID ""
#endif

#ifndef REAL_WIFI_PASS
#define REAL_WIFI_PASS ""
#endif

#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN ""
#endif

#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID ""
#endif

#ifndef WIFI_SSID
  #if USE_WOKWI
    #define WIFI_SSID "Wokwi-GUEST"
  #else
    #define WIFI_SSID REAL_WIFI_SSID
  #endif
#endif

#ifndef WIFI_PASS
  #if USE_WOKWI
    #define WIFI_PASS ""
  #else
    #define WIFI_PASS REAL_WIFI_PASS
  #endif
#endif

#ifndef TZ_INFO
#define TZ_INFO "MSK-3"
#endif

#ifndef TZ_LABEL
#define TZ_LABEL "МСК"
#endif

// ================= PINS =================
static const uint8_t TFT_CS = 4;
static const uint8_t TFT_DC = 16;
static const uint8_t TFT_RST = 15;
static const uint8_t TFT_SCK = 18;
static const uint8_t TFT_MOSI = 17;

#if USE_WOKWI
  static const uint8_t TEMP_PIN = 2;
  static const uint8_t REED_DO_PIN = 8;
#else
  static const uint8_t TEMP_PIN = 5;
  static const uint8_t REED_DO_PIN = 12;
#endif

static const uint8_t ENC_CLK = 7;
static const uint8_t ENC_DT = 6;
static const uint8_t BTN_NEXT = 9;
static const uint8_t BTN_BACK = 10;
static const uint8_t SOUND_DO_PIN = 11;
static const uint8_t WATER_PIN = 1;
static const uint8_t HUMIDITY_PIN = 3;
static const uint8_t GAS_DO_PIN = 13;
static const uint8_t MOTION_DO_PIN = 21;
static const uint8_t BUZZER_PIN = 14;

// 10k NTC thermistor, beta coefficient used by the Wokwi NTC sensor.
static const float THERMISTOR_BETA = 3950.0f;

#if USE_WOKWI
  static const uint8_t SOUND_PIN_MODE = INPUT_PULLUP;
  static const uint8_t SOUND_ACTIVE_LEVEL = LOW;
#else
  // По успешному тесту звука: DO changed: 1 active=YES, DO changed: 0 active=NO.
  static const uint8_t SOUND_PIN_MODE = INPUT_PULLUP;
  static const uint8_t SOUND_ACTIVE_LEVEL = HIGH;
#endif

// Герконовый модуль: обычно active LOW. Если будет наоборот, поменяй LOW на HIGH.
static const uint8_t REED_PIN_MODE = INPUT_PULLUP;
static const uint8_t REED_ACTIVE_LEVEL = LOW;

static const uint8_t GAS_PIN_MODE = INPUT_PULLUP;
static const uint8_t GAS_ACTIVE_LEVEL = LOW;

static const uint8_t MOTION_PIN_MODE = INPUT;
static const uint8_t MOTION_ACTIVE_LEVEL = HIGH;

// Датчик воды: + -> 3.3V, - -> GND, S -> GPIO1.
// Чем больше значение analogRead, тем больше воды на датчике.
static const int WATER_THRESHOLD = 1200;
static const unsigned long GAS_BOOT_IGNORE_TIME = 2000;
static const unsigned long GAS_HOLD_TIME = 1200;
static const unsigned long GAS_RETRIGGER_DELAY = 1000;
static const unsigned long MOTION_BOOT_IGNORE_TIME = 2000;
static const unsigned long MOTION_HOLD_TIME = 1500;
static const unsigned long MOTION_RETRIGGER_DELAY = 1000;

// ================= DISPLAY / UI =================
static const int16_t TFT_W = 240;
static const int16_t TFT_H = 320;
static const uint8_t DISPLAY_ROTATION = USE_ST7789 ? 2 : 0;

static const int16_t SCREEN_W = 240;
static const int16_t SCREEN_H = 320;
static const int16_t HEADER_H = 30;
static const int16_t NOTIFY_Y = 212;

#if USE_ST7789
static const uint16_t COLOR_BG = ST77XX_BLACK;
static const uint16_t COLOR_TEXT = ST77XX_WHITE;
static const uint16_t COLOR_ON = ST77XX_GREEN;
static const uint16_t COLOR_OFF = ST77XX_RED;
static const uint16_t COLOR_WARN = ST77XX_YELLOW;
#else
static const uint16_t COLOR_BG = ILI9341_BLACK;
static const uint16_t COLOR_TEXT = ILI9341_WHITE;
static const uint16_t COLOR_ON = ILI9341_GREEN;
static const uint16_t COLOR_OFF = ILI9341_RED;
static const uint16_t COLOR_WARN = ILI9341_YELLOW;
#endif

static const uint16_t COLOR_PANEL = 0x2104;
static const uint16_t COLOR_ROW = 0x2104;
static const uint16_t COLOR_LINE = 0x4208;
static const uint16_t COLOR_MUTED = 0x9CF3;
static const uint16_t COLOR_ACCENT = 0x05FF;

#if USE_ST7789
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
#else
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
#endif

U8G2_FOR_ADAFRUIT_GFX u8g2;
Preferences prefs;
WebServer wifiPortalServer(80);

// ================= TIMINGS =================
static const unsigned long UI_INTERVAL = 35;
static const unsigned long BUTTON_DEBOUNCE = 120;
static const unsigned long ENCODER_DEBOUNCE = 35;
#if USE_WOKWI
  static const unsigned long TEMP_SENSOR_INTERVAL = 1000;
#else
  static const unsigned long TEMP_SENSOR_INTERVAL = 2500;
#endif
static const unsigned long TEMP_NOTIFY_INTERVAL = 60000;
static const unsigned long SOUND_HOLD_TIME = 700;
static const unsigned long SOUND_RETRIGGER_DELAY = 250;
static const unsigned long SOUND_BOOT_IGNORE_TIME = 2000;
static const unsigned long REED_BOOT_IGNORE_TIME = 2000;
static const unsigned long REED_HOLD_TIME = 900;
static const unsigned long REED_RETRIGGER_DELAY = 500;
static const unsigned long WATER_RETRIGGER_DELAY = 1000;
static const unsigned long BUZZER_DURATION = 1000;
static const unsigned long WIFI_RETRY_INTERVAL = 15000;
static const unsigned long WIFI_CONNECT_TIMEOUT = 9000;
static const unsigned long WIFI_KEYBOARD_CONNECT_TIMEOUT = 30000;
static const unsigned long MENU_TIMEOUT = 30000;
static const unsigned long NOTIFICATION_CLEAR_HOLD = 5000;
static const unsigned long PIN_RESET_HOLD_TIME = 10000;
static const unsigned long WIFI_KEYBOARD_HOLD_TIME = 2000;

// ================= STATE =================
enum AppScreen : uint8_t {
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_SENSORS,
  SCREEN_LOG,
  SCREEN_NETWORK,
  SCREEN_WIFI_LIST,
  SCREEN_WIFI_PASSWORD,
  SCREEN_SETTINGS,
  SCREEN_PIN_SETTINGS,
  SCREEN_GUARD_SETTINGS,
  SCREEN_ALERT_SETTINGS,
  SCREEN_TELEGRAM_SETTINGS,
  SCREEN_TELEGRAM_COMMANDS,
  SCREEN_PASSWORD,
  SCREEN_SET_PIN,
  SCREEN_SYSTEM,
  SCREEN_STATUS,
  SCREEN_CHARACTERISTICS,
  SCREEN_DESCRIPTION
};

enum WifiViewState : uint8_t {
  WIFI_VIEW_OFF,
  WIFI_VIEW_CONNECTING,
  WIFI_VIEW_ONLINE
};

AppScreen screen = SCREEN_MAIN;
WifiViewState wifiViewState = WIFI_VIEW_OFF;

bool soundSensorEnabled = true;
bool reedSensorEnabled = true;
bool waterSensorEnabled = true;
bool tempSensorEnabled = true;
bool humiditySensorEnabled = true;
bool gasSensorEnabled = true;
bool motionSensorEnabled = true;
bool buzzerEnabled = true;
bool buzzerActive = false;
bool soundDetected = false;
bool reedDetected = false;
bool waterDetected = false;
bool gasDetected = false;
bool motionDetected = false;
bool mainDirty = true;
bool menuDirty = true;
bool clockSynced = false;
bool mainFullDirty = true;
bool topBarDirty = true;
bool clockDirty = true;
bool sensorStripDirty = true;
bool notificationsDirty = true;

// ================= GUARD / ALARM =================
bool guardEnabled = false;
bool guardPending = false;
bool guardActive = false;
bool alarmActive = false;
char alarmSource[18] = "";
uint8_t guardDelayIndex = 0;
unsigned long guardArmStartedAt = 0;

const char* guardDelayLabels[] = {
  "Сразу",
  "5 сек",
  "10 сек",
  "20 сек",
  "30 сек",
  "1 мин",
  "2 мин",
  "5 мин"
};

const unsigned long guardDelayValues[] = {
  0,
  5000,
  10000,
  20000,
  30000,
  60000,
  120000,
  300000
};

static const uint8_t GUARD_DELAY_COUNT = sizeof(guardDelayValues) / sizeof(guardDelayValues[0]);

// ================= ALERT SOUND SETTINGS =================
uint8_t alertBeepCountIndex = 0;
uint8_t alertBeepDurationIndex = 1;
uint8_t alertBeepsLeft = 0;
bool alertPauseActive = false;
unsigned long alertPauseStartedAt = 0;

const char* alertBeepCountLabels[] = {
  "1 писк",
  "2 писка",
  "3 писка",
  "До выкл"
};

const uint8_t alertBeepCountValues[] = {
  1,
  2,
  3,
  255
};

const char* alertBeepDurationLabels[] = {
  "Короткий",
  "Средний",
  "Длинный"
};

const unsigned long alertBeepDurationValues[] = {
  300,
  700,
  1200
};

static const uint8_t ALERT_COUNT_OPTION_COUNT = sizeof(alertBeepCountValues) / sizeof(alertBeepCountValues[0]);
static const uint8_t ALERT_DURATION_OPTION_COUNT = sizeof(alertBeepDurationValues) / sizeof(alertBeepDurationValues[0]);

float temperature = NAN;
float humidity = NAN;
time_t currentTime = 0;

uint8_t rootIndex = 0;
uint8_t sensorIndex = 0;
uint8_t networkIndex = 0;
uint8_t wifiListIndex = 0;
uint8_t wifiListScroll = 0;
uint8_t wifiPasswordCursor = 0;
uint8_t wifiPasswordLength = 0;
uint8_t settingsIndex = 0;
uint8_t pinSettingsIndex = 0;
uint8_t guardSettingsIndex = 0;
uint8_t alertSettingsIndex = 0;
uint8_t telegramSettingsIndex = 0;
uint8_t systemIndex = 0;
uint16_t logScroll = 0;
uint8_t pinCursor = 0;
uint8_t pinInputLength = 0;

unsigned long lastUiUpdate = 0;
unsigned long lastStatusRefresh = 0;
unsigned long lastButtonTime = 0;
unsigned long lastEncoderTime = 0;
unsigned long lastTempSensorRead = 0;
unsigned long lastTempNotify = 0;
unsigned long lastSoundSeen = 0;
unsigned long lastSoundEvent = 0;
unsigned long lastReedSeen = 0;
unsigned long lastReedEvent = 0;
unsigned long lastWaterEvent = 0;
unsigned long lastGasSeen = 0;
unsigned long lastGasEvent = 0;
unsigned long lastMotionSeen = 0;
unsigned long lastMotionEvent = 0;
unsigned long buzzerStartedAt = 0;
unsigned long buzzerDurationMs = BUZZER_DURATION;
unsigned long lastClockTick = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastInputTime = 0;
unsigned long nextDownAt = 0;
unsigned long backDownAt = 0;
unsigned long pinResetComboStartedAt = 0;

bool lastNextState = HIGH;
bool lastBackState = HIGH;
bool nextLongHandled = false;
bool backLongHandled = false;
bool pinResetComboHandled = false;
int lastEncClk = HIGH;
long lastMinuteKey = -1;
uint8_t tempFailureCount = 0;

bool lastSoundRawActive = false;
bool lastReedRawActive = false;
bool lastWaterWet = false;
bool lastGasRawActive = false;
bool lastMotionRawActive = false;
uint32_t soundClickCount = 0;
uint32_t reedEventCount = 0;
uint32_t waterEventCount = 0;
uint32_t gasEventCount = 0;
uint32_t motionEventCount = 0;

// ================= TELEGRAM SETTINGS =================
bool telegramEnabled = false;

// ================= WIFI SETUP PORTAL =================
char storedWifiSsid[33] = "";
char storedWifiPass[65] = "";
bool wifiSetupMode = false;
bool wifiPortalReconnectPending = false;
bool wifiKeyboardConnectPending = false;
unsigned long wifiKeyboardStableAt = 0;
unsigned long wifiPortalSavedAt = 0;
unsigned long wifiKeyboardConnectStartedAt = 0;
char wifiKeyboardStatus[32] = "";
static const char* WIFI_SETUP_AP_SSID = "ESP32-MONITOR-SETUP";
static const char* WIFI_SETUP_AP_PASS = "12345678";

char selectedWifiSsid[33] = "";
char wifiPasswordInput[65] = "";
int wifiNetworkCount = -1;

static const uint8_t MAX_WIFI_MENU_NETWORKS = 12;
char wifiMenuSsid[MAX_WIFI_MENU_NETWORKS][33];
int wifiMenuRssi[MAX_WIFI_MENU_NETWORKS];
int32_t wifiMenuChannel[MAX_WIFI_MENU_NETWORKS];
uint8_t wifiMenuBssid[MAX_WIFI_MENU_NETWORKS][6];
uint8_t wifiMenuCount = 0;

uint8_t wifiKeyboardPage = 0;

const char WIFI_KEYS_LOWER[] = "1234567890qwertyuiopasdfghjklzxcvbnm";
const char WIFI_KEYS_UPPER[] = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM";
const char WIFI_KEYS_SYMBOLS[] = "-_@.!?#$%&*+";

// ================= PASSWORD SETTINGS =================
bool passwordEnabled = false;
uint8_t pinLength = 4;
uint8_t pendingPinLength = 4;
bool pinChangeRequiresNewPin = false;
char savedPin[7] = "1234";
char enteredPin[7] = "";
unsigned long lastUnlockTime = 0;
uint8_t passwordTimeoutIndex = 0;

const char* passwordTimeoutLabels[] = {
  "Всегда",
  "1 минута",
  "2 минуты",
  "5 минут",
  "10 минут"
};

const unsigned long passwordTimeoutValues[] = {
  0,
  60000,
  120000,
  300000,
  600000
};

struct LogEntry {
  char time[10];
  char text[72];
};

struct Notification {
  char time[6];
  char text[64];
};

static const uint16_t LOG_CAPACITY = 700;
static const uint8_t NOTIFY_CAPACITY = 4;
LogEntry logEntries[LOG_CAPACITY];
Notification notifications[NOTIFY_CAPACITY];
uint16_t logCount = 0;
uint8_t notifyCount = 0;

struct SensorItem {
  const char* name;
  bool* enabled;
};

SensorItem sensorItems[] = {
  {"Звук", &soundSensorEnabled},
  {"Геркон", &reedSensorEnabled},
  {"Вода", &waterSensorEnabled},
  {"Темп", &tempSensorEnabled},
  {"Влажн", &humiditySensorEnabled},
  {"Газ", &gasSensorEnabled},
  {"Движ", &motionSensorEnabled},
  {"Сирена", &buzzerEnabled}
};

static const uint8_t SENSOR_COUNT = sizeof(sensorItems) / sizeof(sensorItems[0]);
const char* rootItems[] = {"Датчики", "Журнал", "Сеть", "Настройки", "Система"};
const char* systemItems[] = {"Состояние", "Характеристики", "Описание"};
static const uint8_t SYSTEM_COUNT = sizeof(systemItems) / sizeof(systemItems[0]);
static const uint8_t ROOT_COUNT = sizeof(rootItems) / sizeof(rootItems[0]);

// ================= PROTOTYPES =================
void drawBootScreen();
void drawMainScreen();
void drawMenuScreen();
void drawRootMenu();
void drawSensorsMenu();
void drawLogMenu();
void drawNetworkMenu();
void drawWifiListMenu();
void drawWifiPasswordMenu();
void drawSettingsMenu();
void drawPinSettingsMenu();
void drawGuardSettingsMenu();
void drawAlertSettingsMenu();
void drawTelegramSettingsMenu();
void drawTelegramCommandsMenu();
void drawPasswordScreen();
void drawSetPinScreen();
void drawSystemMenu();
void drawStatusMenu();
void drawCharacteristicsMenu();
void drawDescriptionMenu();
void drawTopBar();
void drawWifiBars(int16_t x, int16_t y);
void drawClockBlock();
void drawSensorStrip();
void drawNotifications();
void drawAlarmFrame();
void drawPageTitle(const char* title);
void drawMenuRow(uint8_t index, const char* label, bool selected);
void drawToggle(int16_t x, int16_t y, bool enabled);
void drawStatusLetter(const char* letter, int16_t x, int16_t y, uint16_t statusColor);
void drawUtf8(const uint8_t* font, int16_t x, int16_t y, uint16_t color, const char* text);
void drawUtf8Centered(const uint8_t* font, int16_t y, uint16_t color, const char* text);
uint16_t getEventColor(const char* text);

void updateUi();
void markAllDirty();
void markMainDirty(bool fullRedraw = false);
void handleButtons();
void handleEncoder();
void handleShortButtonPress(bool nextButton);
void handleSound();
void handleReed();
void handleWater();
void handleGas();
void handleMotion();
void handleBuzzer();
void startAlarmBuzzer();
void handleSensorAlert(const char* source);
void updateGuard();
void toggleGuard();
void triggerAlarm(const char* source);
void resetAlarm();
bool telegramConfigured();
String urlEncode(const String& value);
bool sendTelegramMessage(const String& text);
void sendTelegramStatus();
void checkTelegramCommands();
void handleTelegramCommand(const String& command);
const char* activeWifiSsid();
const char* activeWifiPass();
void startWiFiSetupPortal();
void handleWiFiSetupPortal();
void stopWiFiSetupPortal();
void saveWiFiCredentials(const String& ssid, const String& pass);
void clearWiFiCredentials();
const char* currentWifiKeys();
uint8_t currentWifiKeyCount();
char currentWifiKey();
void scanWifiNetworksForMenu();
void openWifiPasswordScreen(uint8_t networkIndex);
void acceptWifiPasswordChar();
void deleteWifiPasswordCharOrBack();
void connectSelectedWifiFromKeyboard();
void readTemperatureSensor();
void updateClock();
void updateWiFi();
void updateMenuTimeout();

void addLog(const char* text);
void addNotification(const char* text);
void formatTime(char* buffer, size_t size, bool withSeconds);
void formatDate(char* buffer, size_t size);
void initClockFromCompileTime();
void startWiFi(bool manual);
bool wifiConfigured();

bool passwordRequired();
void resetPinInput();
void emergencyResetPinAndOpenMenu();
void acceptPinDigit();
void acceptNewPinDigit();
uint8_t getPinEditLength();
void forceNewPinIfRequired();
void deletePinDigitOrExit();
void deleteNewPinDigitOrExit();
void openMenu();
void closeMenu();
void menuBack();
void menuSelect();
void menuMove(int8_t direction);
void noteInput();
void startBuzzer(unsigned long durationMs = BUZZER_DURATION, uint16_t freq = 2300);
void stopBuzzer();
void clearNotifications();
void loadSettings();
void saveSettings();
float readThermistorCelsius();
float readHumidityPercent();

// ================= SETTINGS =================
void loadSettings() {
  prefs.begin("monitor", true);

  String wifiSsid = prefs.getString("wifi_ssid", "");
  String wifiPass = prefs.getString("wifi_pass", "");
  wifiSsid.toCharArray(storedWifiSsid, sizeof(storedWifiSsid));
  wifiPass.toCharArray(storedWifiPass, sizeof(storedWifiPass));

  soundSensorEnabled = prefs.getBool("snd_en", soundSensorEnabled);
  reedSensorEnabled = prefs.getBool("reed_en", reedSensorEnabled);
  waterSensorEnabled = prefs.getBool("water_en", waterSensorEnabled);
  tempSensorEnabled = prefs.getBool("temp_en", tempSensorEnabled);
  humiditySensorEnabled = prefs.getBool("hum_en", humiditySensorEnabled);
  gasSensorEnabled = prefs.getBool("gas_en", gasSensorEnabled);
  motionSensorEnabled = prefs.getBool("mot_en", motionSensorEnabled);
  buzzerEnabled = prefs.getBool("buzz_en", buzzerEnabled);

  passwordEnabled = prefs.getBool("pwd_en", passwordEnabled);
  pinLength = prefs.getUChar("pin_len", pinLength);
  if (pinLength != 4 && pinLength != 6) pinLength = 4;
  pendingPinLength = pinLength;
  pinChangeRequiresNewPin = false;

  String saved = prefs.getString("pin", savedPin);
  saved.toCharArray(savedPin, sizeof(savedPin));
  savedPin[pinLength] = '\0';

  passwordTimeoutIndex = prefs.getUChar("pwd_to", passwordTimeoutIndex);
  if (passwordTimeoutIndex >= 5) passwordTimeoutIndex = 0;

  telegramEnabled = false;

  alertBeepCountIndex = prefs.getUChar("al_cnt", alertBeepCountIndex);
  if (alertBeepCountIndex >= ALERT_COUNT_OPTION_COUNT) alertBeepCountIndex = 0;

  alertBeepDurationIndex = prefs.getUChar("al_dur", alertBeepDurationIndex);
  if (alertBeepDurationIndex >= ALERT_DURATION_OPTION_COUNT) alertBeepDurationIndex = 1;

  guardEnabled = prefs.getBool("guard_en", guardEnabled);
  guardDelayIndex = prefs.getUChar("guard_dl", guardDelayIndex);
  if (guardDelayIndex >= GUARD_DELAY_COUNT) guardDelayIndex = 0;

  prefs.end();

  if (guardEnabled) {
    guardArmStartedAt = millis();
    guardPending = guardDelayValues[guardDelayIndex] > 0;
    guardActive = !guardPending;
  } else {
    guardPending = false;
    guardActive = false;
  }
}

void saveWiFiCredentials(const String& ssid, const String& pass) {
  prefs.begin("monitor", false);
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  prefs.end();

  ssid.toCharArray(storedWifiSsid, sizeof(storedWifiSsid));
  pass.toCharArray(storedWifiPass, sizeof(storedWifiPass));
}

void clearWiFiCredentials() {
  prefs.begin("monitor", false);
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pass");
  prefs.end();

  storedWifiSsid[0] = '\0';
  storedWifiPass[0] = '\0';

  stopWiFiSetupPortal();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_STA);

  wifiViewState = WIFI_VIEW_OFF;
  clockSynced = false;
  lastWifiAttempt = millis();

  addLog("Wi-Fi настройки сброшены");
  addNotification("Wi-Fi setup mode");
  markAllDirty();
}

const char* currentWifiKeys() {
  if (wifiKeyboardPage == 1) return WIFI_KEYS_UPPER;
  if (wifiKeyboardPage == 2) return WIFI_KEYS_SYMBOLS;
  return WIFI_KEYS_LOWER;
}

uint8_t currentWifiKeyCount() {
  return strlen(currentWifiKeys());
}

char currentWifiKey() {
  const char* keys = currentWifiKeys();
  uint8_t count = strlen(keys);
  if (count == 0) return '\0';
  if (wifiPasswordCursor >= count) wifiPasswordCursor = 0;
  return keys[wifiPasswordCursor];
}

void scanWifiNetworksForMenu() {
  stopWiFiSetupPortal();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);

  addLog("Wi-Fi scan");
  addNotification("Скан Wi-Fi...");

  wifiMenuCount = 0;
  wifiNetworkCount = WiFi.scanNetworks(false, false);
  if (wifiNetworkCount < 0) wifiNetworkCount = 0;

  for (int i = 0; i < wifiNetworkCount && wifiMenuCount < MAX_WIFI_MENU_NETWORKS; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();

    if (ssid.length() == 0) continue;

    bool duplicate = false;
    for (uint8_t j = 0; j < wifiMenuCount; j++) {
      if (ssid == String(wifiMenuSsid[j])) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    ssid.toCharArray(wifiMenuSsid[wifiMenuCount], sizeof(wifiMenuSsid[wifiMenuCount]));
    wifiMenuRssi[wifiMenuCount] = WiFi.RSSI(i);
    wifiMenuChannel[wifiMenuCount] = WiFi.channel(i);

    uint8_t* bssid = WiFi.BSSID(i);
    if (bssid != nullptr) memcpy(wifiMenuBssid[wifiMenuCount], bssid, 6);
    else memset(wifiMenuBssid[wifiMenuCount], 0, 6);

    wifiMenuCount++;
  }

  wifiListIndex = 0;
  wifiListScroll = 0;
}

void openWifiPasswordScreen(uint8_t networkIndex) {
  if (wifiMenuCount == 0 || networkIndex >= wifiMenuCount) return;

  strncpy(selectedWifiSsid, wifiMenuSsid[networkIndex], sizeof(selectedWifiSsid));
  selectedWifiSsid[sizeof(selectedWifiSsid) - 1] = '\0';

  wifiPasswordLength = 0;
  wifiPasswordCursor = 0;
  wifiKeyboardPage = 0;
  wifiPasswordInput[0] = '\0';
  wifiKeyboardStatus[0] = '\0';

  screen = SCREEN_WIFI_PASSWORD;
}

void acceptWifiPasswordChar() {
  if (wifiPasswordLength >= sizeof(wifiPasswordInput) - 1) return;

  char key = currentWifiKey();
  if (key == '\0') return;

  wifiKeyboardStatus[0] = '\0';
  wifiPasswordInput[wifiPasswordLength] = key;
  wifiPasswordLength++;
  wifiPasswordInput[wifiPasswordLength] = '\0';
}

void deleteWifiPasswordCharOrBack() {
  if (wifiPasswordLength > 0) {
    wifiKeyboardStatus[0] = '\0';
    wifiPasswordLength--;
    wifiPasswordInput[wifiPasswordLength] = '\0';
    menuDirty = true;
    return;
  }

  screen = SCREEN_WIFI_LIST;
  menuDirty = true;
}

void connectSelectedWifiFromKeyboard() {
  if (strlen(selectedWifiSsid) == 0) return;

  stopWiFiSetupPortal();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(300);
  WiFi.mode(WIFI_STA);
  delay(300);

  Serial.println();
  Serial.println("Wi-Fi keyboard connect attempt");
  Serial.print("SSID: ");
  Serial.println(selectedWifiSsid);
  Serial.print("Password length: ");
  Serial.println(strlen(wifiPasswordInput));

  WiFi.begin(selectedWifiSsid, wifiPasswordInput);

  wifiKeyboardConnectPending = true;
  wifiKeyboardConnectStartedAt = millis();
  wifiKeyboardStableAt = 0;
  wifiViewState = WIFI_VIEW_CONNECTING;
  lastWifiAttempt = millis();

  strncpy(wifiKeyboardStatus, "Подключение...", sizeof(wifiKeyboardStatus) - 1);
  wifiKeyboardStatus[sizeof(wifiKeyboardStatus) - 1] = '\0';

  addLog("Wi-Fi проверка");
  addNotification("Wi-Fi подключение");
  menuDirty = true;
  markMainDirty(false);
}

void saveSettings() {
  prefs.begin("monitor", false);

  prefs.putBool("snd_en", soundSensorEnabled);
  prefs.putBool("reed_en", reedSensorEnabled);
  prefs.putBool("water_en", waterSensorEnabled);
  prefs.putBool("temp_en", tempSensorEnabled);
  prefs.putBool("hum_en", humiditySensorEnabled);
  prefs.putBool("gas_en", gasSensorEnabled);
  prefs.putBool("mot_en", motionSensorEnabled);
  prefs.putBool("buzz_en", buzzerEnabled);

  prefs.putBool("pwd_en", passwordEnabled);
  prefs.putUChar("pin_len", pinLength);
  prefs.putString("pin", savedPin);
  prefs.putUChar("pwd_to", passwordTimeoutIndex);
  prefs.putBool("tg_en", false);
  prefs.putUChar("al_cnt", alertBeepCountIndex);
  prefs.putUChar("al_dur", alertBeepDurationIndex);
  prefs.putBool("guard_en", guardEnabled);
  prefs.putUChar("guard_dl", guardDelayIndex);

  prefs.end();
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(SOUND_DO_PIN, SOUND_PIN_MODE);
  pinMode(WATER_PIN, INPUT);
  pinMode(REED_DO_PIN, REED_PIN_MODE);
  pinMode(TEMP_PIN, INPUT);
  pinMode(HUMIDITY_PIN, INPUT);
  pinMode(GAS_DO_PIN, GAS_PIN_MODE);
  pinMode(MOTION_DO_PIN, MOTION_PIN_MODE);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
#if USE_ST7789
  tft.init(TFT_W, TFT_H);
  tft.setSPISpeed(40000000);
#else
  tft.begin(40000000);
#endif
  tft.setRotation(DISPLAY_ROTATION);
  tft.setTextWrap(false);
  tft.invertDisplay(false);
  tft.fillScreen(COLOR_BG);

  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);

  drawBootScreen();

  initClockFromCompileTime();
  loadSettings();

  lastSoundRawActive = digitalRead(SOUND_DO_PIN) == SOUND_ACTIVE_LEVEL;
  lastReedRawActive = digitalRead(REED_DO_PIN) == REED_ACTIVE_LEVEL;
  lastWaterWet = analogRead(WATER_PIN) >= WATER_THRESHOLD;
  lastGasRawActive = digitalRead(GAS_DO_PIN) == GAS_ACTIVE_LEVEL;
  lastMotionRawActive = digitalRead(MOTION_DO_PIN) == MOTION_ACTIVE_LEVEL;

  addLog("Система запущена");
  addNotification("Система готова");

  if (tempSensorEnabled || humiditySensorEnabled) {
    lastTempSensorRead = millis() - TEMP_SENSOR_INTERVAL;
    readTemperatureSensor();
  }

  drawMainScreen();
  mainFullDirty = false;
  topBarDirty = false;
  clockDirty = false;
  sensorStripDirty = false;
  notificationsDirty = false;
  mainDirty = false;

  if (wifiConfigured()) {
    startWiFi(false);
  } else {
    addLog("Wi-Fi не настроен");
    addNotification("Wi-Fi setup mode");
    startWiFiSetupPortal();
  }

  Serial.println();
  Serial.println("ESP32-S3 monitor started");
  Serial.print("Display: ");
  Serial.print(tft.width());
  Serial.print(" x ");
  Serial.println(tft.height());
  Serial.print("Display driver: ");
  Serial.println(USE_ST7789 ? "ST7789" : "ILI9341");
  Serial.print("Sound active level: ");
  Serial.println(SOUND_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.print("Reed active level: ");
  Serial.println(REED_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.print("Water threshold: ");
  Serial.println(WATER_THRESHOLD);
}

void loop() {
  handleButtons();
  handleEncoder();
  handleSound();
  handleReed();
  handleWater();
  handleGas();
  handleMotion();
  handleBuzzer();
  handleWiFiSetupPortal();
  readTemperatureSensor();
  updateClock();
  updateWiFi();
  updateGuard();
  updateMenuTimeout();
  updateUi();
}

// ================= INPUT =================
void handleButtons() {
  bool nextState = digitalRead(BTN_NEXT);
  bool backState = digitalRead(BTN_BACK);
  unsigned long now = millis();

  if ((screen == SCREEN_PASSWORD || screen == SCREEN_SET_PIN) && nextState == LOW && backState == LOW) {
    if (pinResetComboStartedAt == 0) {
      pinResetComboStartedAt = now;
      pinResetComboHandled = false;
    }

    if (!pinResetComboHandled && now - pinResetComboStartedAt >= PIN_RESET_HOLD_TIME) {
      emergencyResetPinAndOpenMenu();

      nextLongHandled = true;
      backLongHandled = true;
      pinResetComboHandled = true;
      lastButtonTime = now;
      return;
    }
  } else {
    pinResetComboStartedAt = 0;
    pinResetComboHandled = false;
  }

  if (nextState == LOW && nextDownAt > 0 && !nextLongHandled) {
    unsigned long holdTime = (screen == SCREEN_WIFI_PASSWORD) ? WIFI_KEYBOARD_HOLD_TIME : NOTIFICATION_CLEAR_HOLD;

    if (now - nextDownAt >= holdTime) {
      if (screen == SCREEN_MAIN) clearNotifications();
      else if (screen == SCREEN_WIFI_PASSWORD) connectSelectedWifiFromKeyboard();
      nextLongHandled = true;
      menuDirty = true;
    }
  }

  if (backState == LOW && backDownAt > 0 && !backLongHandled) {
    unsigned long holdTime = (screen == SCREEN_WIFI_PASSWORD) ? WIFI_KEYBOARD_HOLD_TIME : NOTIFICATION_CLEAR_HOLD;

    if (now - backDownAt >= holdTime) {
      if (screen == SCREEN_MAIN) {
        clearNotifications();
      } else if (screen == SCREEN_WIFI_PASSWORD) {
        wifiKeyboardPage = (wifiKeyboardPage + 1) % 3;
        wifiPasswordCursor = 0;
        menuDirty = true;
      }
      backLongHandled = true;
    }
  }

  if (now - lastButtonTime > BUTTON_DEBOUNCE && nextState != lastNextState) {
    if (nextState == LOW) {
      nextDownAt = now;
      nextLongHandled = false;
    } else {
      if (!nextLongHandled && nextDownAt > 0) handleShortButtonPress(true);
      nextDownAt = 0;
    }
    lastNextState = nextState;
    lastButtonTime = now;
  }

  if (now - lastButtonTime > BUTTON_DEBOUNCE && backState != lastBackState) {
    if (backState == LOW) {
      backDownAt = now;
      backLongHandled = false;
    } else {
      if (!backLongHandled && backDownAt > 0) handleShortButtonPress(false);
      backDownAt = 0;
    }
    lastBackState = backState;
    lastButtonTime = now;
  }
}

void handleShortButtonPress(bool nextButton) {
  noteInput();

  if (!nextButton && alarmActive) {
    resetAlarm();
    return;
  }

  if (screen == SCREEN_MAIN) openMenu();
  else if (nextButton) menuSelect();
  else menuBack();
}

void handleEncoder() {
  int clkState = digitalRead(ENC_CLK);
  unsigned long now = millis();

  if (clkState != lastEncClk && clkState == LOW && now - lastEncoderTime > ENCODER_DEBOUNCE) {
    int dtState = digitalRead(ENC_DT);
    int8_t direction = (dtState != clkState) ? -1 : 1;

    noteInput();
    if (screen == SCREEN_MAIN) openMenu();
    else menuMove(direction);

    lastEncoderTime = now;
  }

  lastEncClk = clkState;
}

void noteInput() {
  lastInputTime = millis();
}

// ================= SENSORS =================
void handleSound() {
  if (!soundSensorEnabled) {
    if (soundDetected) {
      soundDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    lastSoundRawActive = digitalRead(SOUND_DO_PIN) == SOUND_ACTIVE_LEVEL;
    return;
  }

  unsigned long now = millis();

  if (now < SOUND_BOOT_IGNORE_TIME) {
    soundDetected = false;
    lastSoundRawActive = digitalRead(SOUND_DO_PIN) == SOUND_ACTIVE_LEVEL;
    return;
  }

  int rawValue = digitalRead(SOUND_DO_PIN);
  bool rawActive = rawValue == SOUND_ACTIVE_LEVEL;
  bool clickEvent = rawActive && !lastSoundRawActive;

  if (rawActive != lastSoundRawActive) {
    Serial.print("DO changed: ");
    Serial.print(rawValue);
    Serial.print(" active=");
    Serial.println(rawActive ? "YES" : "NO");
  }

  if (clickEvent && (lastSoundEvent == 0 || now - lastSoundEvent >= SOUND_RETRIGGER_DELAY)) {
    lastSoundEvent = now;
    lastSoundSeen = now;
    soundClickCount++;
    soundDetected = true;

    addLog("Сработал датчик звука");
    addNotification("Сработал датчик звука");
    handleSensorAlert("Звук");

    Serial.print("SOUND EVENT #");
    Serial.print(soundClickCount);
    Serial.print(" DO=");
    Serial.println(rawValue);
  }

  bool previousDetected = soundDetected;
  soundDetected = lastSoundSeen > 0 && now - lastSoundSeen < SOUND_HOLD_TIME;

  if (previousDetected != soundDetected) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  lastSoundRawActive = rawActive;
}

void handleReed() {
  if (!reedSensorEnabled) {
    if (reedDetected) {
      reedDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    lastReedRawActive = digitalRead(REED_DO_PIN) == REED_ACTIVE_LEVEL;
    return;
  }

  unsigned long now = millis();

  if (now < REED_BOOT_IGNORE_TIME) {
    reedDetected = false;
    lastReedRawActive = digitalRead(REED_DO_PIN) == REED_ACTIVE_LEVEL;
    return;
  }

  int rawValue = digitalRead(REED_DO_PIN);
  bool rawActive = rawValue == REED_ACTIVE_LEVEL;
  bool reedEvent = rawActive && !lastReedRawActive;

  if (rawActive != lastReedRawActive) {
    Serial.print("REED changed: ");
    Serial.print(rawValue);
    Serial.print(" active=");
    Serial.println(rawActive ? "YES" : "NO");
  }

  if (reedEvent && (lastReedEvent == 0 || now - lastReedEvent >= REED_RETRIGGER_DELAY)) {
    lastReedEvent = now;
    lastReedSeen = now;
    reedEventCount++;
    reedDetected = true;

    addLog("Сработал геркон");
    addNotification("Сработал геркон");
    handleSensorAlert("Геркон");

    Serial.print("REED EVENT #");
    Serial.print(reedEventCount);
    Serial.print(" DO=");
    Serial.println(rawValue);
  }

  bool previousDetected = reedDetected;
  reedDetected = lastReedSeen > 0 && now - lastReedSeen < REED_HOLD_TIME;

  if (previousDetected != reedDetected) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  lastReedRawActive = rawActive;
}

void handleWater() {
  if (!waterSensorEnabled) {
    if (waterDetected) {
      waterDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    lastWaterWet = analogRead(WATER_PIN) >= WATER_THRESHOLD;
    return;
  }

  unsigned long now = millis();
  int waterRaw = analogRead(WATER_PIN);
  bool wetNow = waterRaw >= WATER_THRESHOLD;
  bool waterEvent = wetNow && !lastWaterWet;

  if (waterEvent && (lastWaterEvent == 0 || now - lastWaterEvent >= WATER_RETRIGGER_DELAY)) {
    lastWaterEvent = now;
    waterEventCount++;

    addLog("Обнаружена вода");
    addNotification("Обнаружена вода");
    handleSensorAlert("Вода");

    Serial.print("WATER EVENT #");
    Serial.print(waterEventCount);
    Serial.print(" raw=");
    Serial.println(waterRaw);
  }

  bool previousDetected = waterDetected;
  waterDetected = wetNow;

  if (previousDetected != waterDetected) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  lastWaterWet = wetNow;
}

void handleGas() {
  if (!gasSensorEnabled) {
    if (gasDetected) {
      gasDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    lastGasRawActive = digitalRead(GAS_DO_PIN) == GAS_ACTIVE_LEVEL;
    return;
  }

  unsigned long now = millis();

  if (now < GAS_BOOT_IGNORE_TIME) {
    gasDetected = false;
    lastGasRawActive = digitalRead(GAS_DO_PIN) == GAS_ACTIVE_LEVEL;
    return;
  }

  int rawValue = digitalRead(GAS_DO_PIN);
  bool rawActive = rawValue == GAS_ACTIVE_LEVEL;
  bool gasEvent = rawActive && !lastGasRawActive;

  if (gasEvent && (lastGasEvent == 0 || now - lastGasEvent >= GAS_RETRIGGER_DELAY)) {
    lastGasEvent = now;
    lastGasSeen = now;
    gasEventCount++;
    gasDetected = true;

    addLog("Обнаружен газ");
    addNotification("Обнаружен газ");
    handleSensorAlert("Газ");

    Serial.print("GAS EVENT #");
    Serial.print(gasEventCount);
    Serial.print(" DO=");
    Serial.println(rawValue);
  }

  bool previousDetected = gasDetected;
  gasDetected = lastGasSeen > 0 && now - lastGasSeen < GAS_HOLD_TIME;

  if (previousDetected != gasDetected) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  lastGasRawActive = rawActive;
}

void handleMotion() {
  if (!motionSensorEnabled) {
    if (motionDetected) {
      motionDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    lastMotionRawActive = digitalRead(MOTION_DO_PIN) == MOTION_ACTIVE_LEVEL;
    return;
  }

  unsigned long now = millis();

  if (now < MOTION_BOOT_IGNORE_TIME) {
    motionDetected = false;
    lastMotionRawActive = digitalRead(MOTION_DO_PIN) == MOTION_ACTIVE_LEVEL;
    return;
  }

  int rawValue = digitalRead(MOTION_DO_PIN);
  bool rawActive = rawValue == MOTION_ACTIVE_LEVEL;
  bool motionEvent = rawActive && !lastMotionRawActive;

  if (motionEvent && (lastMotionEvent == 0 || now - lastMotionEvent >= MOTION_RETRIGGER_DELAY)) {
    lastMotionEvent = now;
    lastMotionSeen = now;
    motionEventCount++;
    motionDetected = true;

    addLog("Обнаружено движение");
    addNotification("Обнаружено движение");
    handleSensorAlert("Движение");

    Serial.print("MOTION EVENT #");
    Serial.print(motionEventCount);
    Serial.print(" DO=");
    Serial.println(rawValue);
  }

  bool previousDetected = motionDetected;
  motionDetected = lastMotionSeen > 0 && now - lastMotionSeen < MOTION_HOLD_TIME;

  if (previousDetected != motionDetected) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  lastMotionRawActive = rawActive;
}

float readThermistorCelsius() {
  const uint8_t samples = 8;
  uint32_t sum = 0;

  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(TEMP_PIN);
    delay(2);
  }

  float raw = (float)sum / samples;
  if (raw <= 2.0f || raw >= 4093.0f) return NAN;

  float celsius = 1.0f / (log(1.0f / (4095.0f / raw - 1.0f)) / THERMISTOR_BETA + 1.0f / 298.15f) - 273.15f;
  if (celsius < -60.0f || celsius > 150.0f) return NAN;
  return celsius;
}

float readHumidityPercent() {
  const uint8_t samples = 8;
  uint32_t sum = 0;

  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(HUMIDITY_PIN);
    delay(2);
  }

  float raw = (float)sum / samples;
  if (raw < 0.0f || raw > 4095.0f) return NAN;
  return constrain(raw * 100.0f / 4095.0f, 0.0f, 100.0f);
}

void readTemperatureSensor() {
  if (!tempSensorEnabled && !humiditySensorEnabled) return;

  unsigned long now = millis();
  if (now - lastTempSensorRead < TEMP_SENSOR_INTERVAL) return;
  lastTempSensorRead = now;

  float newTemperature = tempSensorEnabled ? readThermistorCelsius() : NAN;
  float newHumidity = humiditySensorEnabled ? readHumidityPercent() : NAN;
  bool tempFailed = tempSensorEnabled && isnan(newTemperature);
  bool humidityFailed = humiditySensorEnabled && isnan(newHumidity);

  if (tempFailed || humidityFailed) {
    bool hadReading = !isnan(temperature);

    if (tempFailureCount < 255) tempFailureCount++;
    Serial.print("CLIMATE read failed, count = ");
    Serial.println(tempFailureCount);

    if (tempFailureCount == 5) addLog("Климат: нет сигнала");
    if (tempFailureCount >= 3 && hadReading) temperature = NAN;
    if (tempFailureCount >= 3 && !isnan(humidity)) humidity = NAN;

    sensorStripDirty = true;
    mainDirty = true;
    return;
  }

  tempFailureCount = 0;

  bool changed = false;
  if (tempSensorEnabled) {
    if (isnan(temperature) || fabs(newTemperature - temperature) >= 0.1f) changed = true;
    temperature = newTemperature;
  } else if (!isnan(temperature)) {
    temperature = NAN;
    changed = true;
  }

  if (humiditySensorEnabled) {
    if (isnan(humidity) || fabs(newHumidity - humidity) >= 0.5f) changed = true;
    humidity = newHumidity;
  } else if (!isnan(humidity)) {
    humidity = NAN;
    changed = true;
  }

  if (changed) {
    Serial.print("CLIMATE update: T=");
    if (isnan(temperature)) Serial.print("--");
    else Serial.print(temperature, 1);
    Serial.print("C H=");
    if (isnan(humidity)) Serial.print("--");
    else Serial.print(humidity, 0);
    Serial.println("%");
    sensorStripDirty = true;
    mainDirty = true;
  }

  if ((tempSensorEnabled || humiditySensorEnabled) && (lastTempNotify == 0 || now - lastTempNotify >= TEMP_NOTIFY_INTERVAL)) {
    char message[64];
    if (!isnan(temperature) && !isnan(humidity)) snprintf(message, sizeof(message), "T %.1fC  H %.0f%%", temperature, humidity);
    else if (!isnan(temperature)) snprintf(message, sizeof(message), "T %.1fC", temperature);
    else if (!isnan(humidity)) snprintf(message, sizeof(message), "H %.0f%%", humidity);
    else return;
    addNotification(message);
    addLog(message);
    lastTempNotify = now;
  }
}

// ================= BUZZER =================
void startBuzzer(unsigned long durationMs, uint16_t freq) {
  if (!buzzerEnabled) return;

  buzzerActive = true;
  buzzerStartedAt = millis();
  buzzerDurationMs = durationMs;
  digitalWrite(BUZZER_PIN, HIGH);
  sensorStripDirty = true;
  mainDirty = true;
  Serial.print("BUZZER ON freq=");
  Serial.println(freq);
}

void stopBuzzer() {
  if (!buzzerActive) return;

  buzzerActive = false;
  digitalWrite(BUZZER_PIN, LOW);
  noTone(BUZZER_PIN);
  sensorStripDirty = true;
  mainDirty = true;
  Serial.println("BUZZER OFF");
}

void handleBuzzer() {
  unsigned long now = millis();

  if (buzzerActive && buzzerDurationMs > 0 && now - buzzerStartedAt >= buzzerDurationMs) {
    stopBuzzer();

    if (alarmActive && alertBeepsLeft > 0) {
      alertPauseActive = true;
      alertPauseStartedAt = now;
    }
  }

  if (alarmActive && alertPauseActive && now - alertPauseStartedAt >= 250) {
    alertPauseActive = false;
    if (alertBeepsLeft > 0) {
      alertBeepsLeft--;
      startBuzzer(alertBeepDurationValues[alertBeepDurationIndex]);
    }
  }
}

void startAlarmBuzzer() {
  alertPauseActive = false;

  if (alertBeepCountValues[alertBeepCountIndex] == 255) {
    alertBeepsLeft = 0;
    startBuzzer(0);
    return;
  }

  alertBeepsLeft = alertBeepCountValues[alertBeepCountIndex] - 1;
  startBuzzer(alertBeepDurationValues[alertBeepDurationIndex]);
}

void handleSensorAlert(const char* source) {
  if (guardActive) {
    triggerAlarm(source);
    return;
  }

  startBuzzer(BUZZER_DURATION);
}

void toggleGuard() {
  guardEnabled = !guardEnabled;

  if (!guardEnabled) {
    guardPending = false;
    guardActive = false;
    resetAlarm();
    addLog("Охрана выключена");
    addNotification("Охрана выключена");
    sendTelegramMessage("Охрана выключена");
    saveSettings();
    markAllDirty();
    return;
  }

  guardArmStartedAt = millis();
  guardPending = guardDelayValues[guardDelayIndex] > 0;
  guardActive = !guardPending;

  if (guardActive) {
    addLog("Охрана включена");
    addNotification("Охрана включена");
    sendTelegramMessage("Охрана включена");
  } else {
    char msg[40];
    snprintf(msg, sizeof(msg), "Охрана: %s", guardDelayLabels[guardDelayIndex]);
    addLog(msg);
    addNotification(msg);
  }

  saveSettings();
  markAllDirty();
}

void updateGuard() {
  if (!guardEnabled || !guardPending) return;

  if (millis() - guardArmStartedAt >= guardDelayValues[guardDelayIndex]) {
    guardPending = false;
    guardActive = true;
    addLog("Охрана включена");
    addNotification("Охрана включена");
    markAllDirty();
  }
}

void triggerAlarm(const char* source) {
  if (!guardActive) return;

  alarmActive = true;
  strncpy(alarmSource, source, sizeof(alarmSource) - 1);
  alarmSource[sizeof(alarmSource) - 1] = '\0';

  char msg[48];
  snprintf(msg, sizeof(msg), "ТРЕВОГА: %s", source);
  addLog(msg);
  addNotification(msg);
  if (strcmp(source, "Звук") == 0) sendTelegramMessage("ТРЕВОГА: Сработал датчик звука!");
  else if (strcmp(source, "Геркон") == 0) sendTelegramMessage("ТРЕВОГА: Сработал геркон!");
  else if (strcmp(source, "Вода") == 0) sendTelegramMessage("ТРЕВОГА: Сработал датчик воды!");
  else sendTelegramMessage(String("ТРЕВОГА: ") + String(source));

  startAlarmBuzzer();
  markAllDirty();
}

void resetAlarm() {
  if (!alarmActive && !buzzerActive) return;

  alarmActive = false;
  alarmSource[0] = '\0';
  stopBuzzer();
  addLog("Тревога сброшена");
  addNotification("Тревога сброшена");
  markAllDirty();
}

// ================= TIME / WIFI =================
void initClockFromCompileTime() {
  setenv("TZ", TZ_INFO, 1);
  tzset();

  struct tm timeInfo;
  memset(&timeInfo, 0, sizeof(timeInfo));

  int day = 1, year = 2026, hour = 0, minute = 0, second = 0;
  char monthStr[4] = {0};

  sscanf(__DATE__, "%3s %d %d", monthStr, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  if (strcmp(monthStr, "Jan") == 0) timeInfo.tm_mon = 0;
  else if (strcmp(monthStr, "Feb") == 0) timeInfo.tm_mon = 1;
  else if (strcmp(monthStr, "Mar") == 0) timeInfo.tm_mon = 2;
  else if (strcmp(monthStr, "Apr") == 0) timeInfo.tm_mon = 3;
  else if (strcmp(monthStr, "May") == 0) timeInfo.tm_mon = 4;
  else if (strcmp(monthStr, "Jun") == 0) timeInfo.tm_mon = 5;
  else if (strcmp(monthStr, "Jul") == 0) timeInfo.tm_mon = 6;
  else if (strcmp(monthStr, "Aug") == 0) timeInfo.tm_mon = 7;
  else if (strcmp(monthStr, "Sep") == 0) timeInfo.tm_mon = 8;
  else if (strcmp(monthStr, "Oct") == 0) timeInfo.tm_mon = 9;
  else if (strcmp(monthStr, "Nov") == 0) timeInfo.tm_mon = 10;
  else if (strcmp(monthStr, "Dec") == 0) timeInfo.tm_mon = 11;

  timeInfo.tm_mday = day;
  timeInfo.tm_year = year - 1900;
  timeInfo.tm_hour = hour;
  timeInfo.tm_min = minute;
  timeInfo.tm_sec = second;

  currentTime = mktime(&timeInfo);
  lastClockTick = millis();
}

void updateClock() {
  unsigned long now = millis();
  while (now - lastClockTick >= 1000) {
    currentTime++;
    lastClockTick += 1000;
  }

  if (wifiViewState == WIFI_VIEW_ONLINE) {
    time_t ntpTime = time(nullptr);
    if (ntpTime > 1700000000) {
      currentTime = ntpTime;
      clockSynced = true;
    }
  }

  long minuteKey = currentTime / 60;
  if (minuteKey != lastMinuteKey) {
    lastMinuteKey = minuteKey;
    topBarDirty = true;
    clockDirty = true;
    mainDirty = true;
  }
}

const char* activeWifiSsid() {
  if (strlen(storedWifiSsid) > 0) return storedWifiSsid;
  return WIFI_SSID;
}

const char* activeWifiPass() {
  if (strlen(storedWifiSsid) > 0) return storedWifiPass;
  return WIFI_PASS;
}

bool wifiConfigured() {
  return strlen(activeWifiSsid()) > 0;
}

void startWiFi(bool manual) {
  if (wifiSetupMode) stopWiFiSetupPortal();

  if (!wifiConfigured()) {
    wifiViewState = WIFI_VIEW_OFF;
    if (manual) {
      addNotification("Wi-Fi setup mode");
      addLog("Wi-Fi setup mode");
    }
    startWiFiSetupPortal();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

#if USE_WOKWI
  WiFi.begin(activeWifiSsid(), activeWifiPass(), 6);
#else
  WiFi.begin(activeWifiSsid(), activeWifiPass());
#endif

  wifiViewState = WIFI_VIEW_CONNECTING;
  lastWifiAttempt = millis();
  topBarDirty = true;
  mainDirty = true;
  menuDirty = true;

  if (manual) {
    addNotification("Поиск Wi-Fi");
    addLog("Запущен поиск Wi-Fi");
  }
}

void updateWiFi() {
  if (wifiKeyboardConnectPending) return;
  if (!wifiConfigured()) return;

  unsigned long now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && wifiViewState != WIFI_VIEW_ONLINE) {
    wifiViewState = WIFI_VIEW_ONLINE;
    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
    addNotification("Wi-Fi подключен");
    addLog("Wi-Fi подключен");
    markAllDirty();
    return;
  }

  if (!connected && wifiViewState == WIFI_VIEW_ONLINE) {
    wifiViewState = WIFI_VIEW_OFF;
    clockSynced = false;
    addNotification("Wi-Fi отключен");
    addLog("Wi-Fi отключен");
    markAllDirty();
  }

  if (wifiViewState == WIFI_VIEW_CONNECTING && now - lastWifiAttempt > WIFI_CONNECT_TIMEOUT) {
    wifiViewState = WIFI_VIEW_OFF;
    addLog("Wi-Fi timeout");
    startWiFiSetupPortal();
    markAllDirty();
  }

  if (wifiViewState == WIFI_VIEW_OFF && now - lastWifiAttempt > WIFI_RETRY_INTERVAL) startWiFi(false);
}

// ================= WIFI SETUP PORTAL =================
void startWiFiSetupPortal() {
  if (wifiSetupMode) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_PASS);

  wifiSetupMode = true;
  wifiPortalReconnectPending = false;
  wifiViewState = WIFI_VIEW_OFF;

  wifiPortalServer.on("/", HTTP_GET, []() {
    int networks = WiFi.scanNetworks();

    String page = "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>ESP32 Wi-Fi Setup</title>";
    page += "<style>body{font-family:Arial;background:#111;color:#eee;padding:20px;}";
    page += "select,input,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:1px solid #555;background:#222;color:#fff;}";
    page += "button{background:#0aa;color:#fff;font-weight:bold;} .card{max-width:420px;margin:auto;border:1px solid #444;border-radius:16px;padding:18px;}";
    page += "small{color:#aaa;}</style></head><body><div class='card'>";
    page += "<h2>ESP32 Wi-Fi Setup</h2>";
    page += "<p><small>Выбери сеть, введи пароль и нажми Save.</small></p>";
    page += "<form method='POST' action='/save'>";
    page += "<label>SSID</label><select name='ssid'>";

    for (int i = 0; i < networks; i++) {
      page += "<option value='";
      page += WiFi.SSID(i);
      page += "'>";
      page += WiFi.SSID(i);
      page += " (";
      page += String(WiFi.RSSI(i));
      page += " dBm)";
      page += "</option>";
    }

    page += "</select>";
    page += "<label>Пароль</label><input name='pass' type='password' placeholder='Wi-Fi password'>";
    page += "<button type='submit'>Save</button>";
    page += "</form>";
    page += "<p><small>Setup AP: ";
    page += WIFI_SETUP_AP_SSID;
    page += " / ";
    page += WIFI_SETUP_AP_PASS;
    page += "</small></p>";
    page += "</div></body></html>";

    wifiPortalServer.send(200, "text/html", page);
  });

  wifiPortalServer.on("/save", HTTP_POST, []() {
    String ssid = wifiPortalServer.arg("ssid");
    String pass = wifiPortalServer.arg("pass");

    if (ssid.length() == 0) {
      wifiPortalServer.send(400, "text/plain", "SSID is empty");
      return;
    }

    saveWiFiCredentials(ssid, pass);

    String page = "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>body{font-family:Arial;background:#111;color:#eee;padding:20px;} .card{max-width:420px;margin:auto;border:1px solid #444;border-radius:16px;padding:18px;}</style>";
    page += "</head><body><div class='card'><h2>Saved</h2>";
    page += "<p>Wi-Fi сохранён. ESP32 попробует подключиться через секунду.</p>";
    page += "<p>SSID: ";
    page += ssid;
    page += "</p></div></body></html>";

    wifiPortalServer.send(200, "text/html", page);

    wifiPortalReconnectPending = true;
    wifiPortalSavedAt = millis();
    addLog("Wi-Fi сохранён");
    addNotification("Wi-Fi сохранён");
  });

  wifiPortalServer.begin();

  addLog("Wi-Fi setup portal");
  addNotification("AP: ESP32-MONITOR");
  markAllDirty();
}

void stopWiFiSetupPortal() {
  if (!wifiSetupMode) return;

  wifiPortalServer.stop();
  WiFi.softAPdisconnect(true);
  wifiSetupMode = false;
}

void handleWiFiSetupPortal() {
  if (wifiSetupMode) {
    wifiPortalServer.handleClient();

    if (wifiPortalReconnectPending && millis() - wifiPortalSavedAt > 1200) {
      wifiPortalReconnectPending = false;
      stopWiFiSetupPortal();
      startWiFi(true);
    }
  }

  if (wifiKeyboardConnectPending) {
    wl_status_t status = WiFi.status();
    unsigned long now = millis();

    if (status == WL_CONNECTED && WiFi.localIP().toString() != "0.0.0.0") {
      if (wifiKeyboardStableAt == 0) {
        wifiKeyboardStableAt = now;
        strncpy(wifiKeyboardStatus, "Проверка сети...", sizeof(wifiKeyboardStatus) - 1);
        wifiKeyboardStatus[sizeof(wifiKeyboardStatus) - 1] = '\0';
        menuDirty = true;
      }

      if (now - wifiKeyboardStableAt >= 1500) {
        wifiKeyboardConnectPending = false;
        wifiKeyboardStableAt = 0;
        wifiViewState = WIFI_VIEW_ONLINE;
        clockSynced = false;

        saveWiFiCredentials(String(selectedWifiSsid), String(wifiPasswordInput));
        strncpy(wifiKeyboardStatus, "Подключено", sizeof(wifiKeyboardStatus) - 1);
        wifiKeyboardStatus[sizeof(wifiKeyboardStatus) - 1] = '\0';

        WiFi.setAutoReconnect(true);
        addLog("Wi-Fi сохранён");
        addNotification("Wi-Fi подключен");
        configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

        screen = SCREEN_NETWORK;
        markAllDirty();
        return;
      }
    } else {
      wifiKeyboardStableAt = 0;
    }

    bool hardFail = (status == WL_CONNECT_FAILED && now - wifiKeyboardConnectStartedAt > 5000);
    bool timeout = now - wifiKeyboardConnectStartedAt > WIFI_KEYBOARD_CONNECT_TIMEOUT;

    if (hardFail || timeout) {
      wifiKeyboardConnectPending = false;
      wifiKeyboardStableAt = 0;
      wifiViewState = WIFI_VIEW_OFF;

      int statusCode = (int)status;
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_STA);

      snprintf(wifiKeyboardStatus, sizeof(wifiKeyboardStatus), "Ошибка Wi-Fi: %d", statusCode);

      char logMsg[40];
      snprintf(logMsg, sizeof(logMsg), "Wi-Fi error code %d", statusCode);
      addLog(logMsg);
      addNotification("Wi-Fi ошибка");

      Serial.print("Wi-Fi connect failed, status = ");
      Serial.println(statusCode);

      menuDirty = true;
      markMainDirty(false);
    }
  }
}

// ================= TELEGRAM =================
bool telegramConfigured() {
  return false;
}

String urlEncode(const String& value) {
  String encoded = "";
  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];

    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

bool sendTelegramMessage(const String& text) {
  (void)text;
  return false;
}

void sendTelegramStatus() {
  addLog("Telegram: заглушка");
  addNotification("Telegram отключён");
}

void handleTelegramCommand(const String& command) {
  (void)command;
}

void checkTelegramCommands() {
}

// ================= LOGS =================
void addLog(const char* text) {
  if (logCount >= LOG_CAPACITY) {
    memmove(&logEntries[0], &logEntries[1], sizeof(LogEntry) * (LOG_CAPACITY - 1));
    logCount = LOG_CAPACITY - 1;
  }

  formatTime(logEntries[logCount].time, sizeof(logEntries[logCount].time), true);
  strncpy(logEntries[logCount].text, text, sizeof(logEntries[logCount].text) - 1);
  logEntries[logCount].text[sizeof(logEntries[logCount].text) - 1] = '\0';
  logCount++;
  menuDirty = true;
}

void addNotification(const char* text) {
  if (notifyCount >= NOTIFY_CAPACITY) {
    memmove(&notifications[0], &notifications[1], sizeof(Notification) * (NOTIFY_CAPACITY - 1));
    notifyCount = NOTIFY_CAPACITY - 1;
  }

  formatTime(notifications[notifyCount].time, sizeof(notifications[notifyCount].time), false);
  strncpy(notifications[notifyCount].text, text, sizeof(notifications[notifyCount].text) - 1);
  notifications[notifyCount].text[sizeof(notifications[notifyCount].text) - 1] = '\0';
  notifyCount++;
  notificationsDirty = true;
  mainDirty = true;
}

void clearNotifications() {
  if (notifyCount == 0) return;
  notifyCount = 0;
  addLog("Уведомления очищены");
  notificationsDirty = true;
  mainDirty = true;
}

void formatTime(char* buffer, size_t size, bool withSeconds) {
  struct tm timeInfo;
  localtime_r(&currentTime, &timeInfo);
  if (withSeconds) snprintf(buffer, size, "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  else snprintf(buffer, size, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
}

void formatDate(char* buffer, size_t size) {
  struct tm timeInfo;
  localtime_r(&currentTime, &timeInfo);
  snprintf(buffer, size, "%02d.%02d.%04d", timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_year + 1900);
}

// ================= MENU LOGIC =================
bool passwordRequired() {
  if (!passwordEnabled) return false;

  unsigned long timeout = passwordTimeoutValues[passwordTimeoutIndex];
  if (timeout == 0) return true;

  return millis() - lastUnlockTime > timeout;
}

void resetPinInput() {
  pinCursor = 0;
  pinInputLength = 0;
  enteredPin[0] = '\0';
}

void emergencyResetPinAndOpenMenu() {
  if (pinLength == 6 || pendingPinLength == 6) {
    pinLength = 6;
    pendingPinLength = 6;
    strncpy(savedPin, "123456", sizeof(savedPin));
    savedPin[6] = '\0';
  } else {
    pinLength = 4;
    pendingPinLength = 4;
    strncpy(savedPin, "1234", sizeof(savedPin));
    savedPin[4] = '\0';
  }

  pinChangeRequiresNewPin = false;
  resetPinInput();

  lastUnlockTime = millis();
  screen = SCREEN_MENU;
  rootIndex = 0;
  sensorIndex = 0;
  networkIndex = 0;
  settingsIndex = 0;
  menuDirty = true;

  saveSettings();
  addLog("PIN аварийно сброшен");
  addNotification(pinLength == 6 ? "PIN: 123456" : "PIN: 1234");
}

uint8_t getPinEditLength() {
  return pinChangeRequiresNewPin ? pendingPinLength : pinLength;
}

void forceNewPinIfRequired() {
  if (!pinChangeRequiresNewPin) return;

  resetPinInput();
  screen = SCREEN_SET_PIN;
  menuDirty = true;
  addNotification("Задай новый PIN");
}

void openMenu() {
  if (passwordRequired()) {
    resetPinInput();
    screen = SCREEN_PASSWORD;
    noteInput();
    menuDirty = true;
    return;
  }

  screen = SCREEN_MENU;
  rootIndex = 0;
  sensorIndex = 0;
  settingsIndex = 0;
  pinSettingsIndex = 0;
  guardSettingsIndex = 0;
  alertSettingsIndex = 0;
  telegramSettingsIndex = 0;
  systemIndex = 0;
  logScroll = 0;
  noteInput();
  menuDirty = true;
}

void closeMenu() {
  screen = SCREEN_MAIN;
  markAllDirty();
}

void deletePinDigitOrExit() {
  if (pinInputLength > 0) {
    pinInputLength--;
    enteredPin[pinInputLength] = '\0';
    menuDirty = true;
    return;
  }

  closeMenu();
}

void deleteNewPinDigitOrExit() {
  if (pinInputLength > 0) {
    pinInputLength--;
    enteredPin[pinInputLength] = '\0';
    menuDirty = true;
    return;
  }

  if (pinChangeRequiresNewPin) {
    addNotification("Нужен новый PIN");
    menuDirty = true;
    return;
  }

  screen = SCREEN_PIN_SETTINGS;
  menuDirty = true;
}

void menuBack() {
  if (screen == SCREEN_PASSWORD) {
    deletePinDigitOrExit();
    return;
  }

  if (screen == SCREEN_SET_PIN) {
    deleteNewPinDigitOrExit();
    return;
  }

  if (screen == SCREEN_WIFI_PASSWORD) {
    deleteWifiPasswordCharOrBack();
    return;
  }

  if (screen == SCREEN_WIFI_LIST) {
    screen = SCREEN_NETWORK;
    menuDirty = true;
    return;
  }

  if (pinChangeRequiresNewPin && (screen == SCREEN_PIN_SETTINGS || screen == SCREEN_SETTINGS || screen == SCREEN_MENU)) {
    forceNewPinIfRequired();
    return;
  }

  if (screen == SCREEN_MENU) {
    closeMenu();
    return;
  }

  if (screen == SCREEN_STATUS || screen == SCREEN_CHARACTERISTICS || screen == SCREEN_DESCRIPTION) {
    screen = SCREEN_SYSTEM;
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_PIN_SETTINGS || screen == SCREEN_GUARD_SETTINGS || screen == SCREEN_ALERT_SETTINGS || screen == SCREEN_TELEGRAM_SETTINGS || screen == SCREEN_TELEGRAM_COMMANDS) {
    screen = SCREEN_SETTINGS;
    menuDirty = true;
    return;
  }

  screen = SCREEN_MENU;
  menuDirty = true;
}

void acceptPinDigit() {
  if (pinInputLength >= pinLength) return;

  enteredPin[pinInputLength] = '0' + pinCursor;
  pinInputLength++;
  enteredPin[pinInputLength] = '\0';

  if (pinInputLength >= pinLength) {
    enteredPin[pinLength] = '\0';
    savedPin[pinLength] = '\0';

    Serial.print("PIN entered: ");
    Serial.println(enteredPin);
    Serial.print("PIN saved: ");
    Serial.println(savedPin);
    Serial.print("PIN length: ");
    Serial.println(pinLength);

    if (strcmp(enteredPin, savedPin) == 0) {
      lastUnlockTime = millis();
      pendingPinLength = pinLength;
      pinChangeRequiresNewPin = false;
      screen = SCREEN_MENU;
      rootIndex = 0;
      addLog("Вход по PIN выполнен");
    } else {
      addLog("Неверный PIN");
      addNotification("Неверный PIN");
      sendTelegramMessage("Неверный PIN на устройстве");
      resetPinInput();
    }
  }

  menuDirty = true;
}

void acceptNewPinDigit() {
  uint8_t targetPinLength = getPinEditLength();
  if (targetPinLength != 4 && targetPinLength != 6) targetPinLength = 4;
  if (pinInputLength >= targetPinLength) return;

  enteredPin[pinInputLength] = '0' + pinCursor;
  pinInputLength++;
  enteredPin[pinInputLength] = '\0';

  if (pinInputLength >= targetPinLength) {
    enteredPin[targetPinLength] = '\0';

    memset(savedPin, 0, sizeof(savedPin));
    strncpy(savedPin, enteredPin, targetPinLength);
    savedPin[targetPinLength] = '\0';

    pinLength = targetPinLength;
    pendingPinLength = pinLength;
    pinChangeRequiresNewPin = false;

    Serial.print("NEW PIN saved: ");
    Serial.println(savedPin);
    Serial.print("NEW PIN length: ");
    Serial.println(pinLength);

    resetPinInput();
    addLog("PIN изменён");
    addNotification("PIN изменён");
    saveSettings();
    screen = SCREEN_PIN_SETTINGS;
  }

  menuDirty = true;
}

void menuSelect() {
  if (screen == SCREEN_PASSWORD) {
    acceptPinDigit();
    return;
  }

  if (screen == SCREEN_SET_PIN) {
    acceptNewPinDigit();
    return;
  }

  if (screen == SCREEN_WIFI_PASSWORD) {
    acceptWifiPasswordChar();
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_WIFI_LIST) {
    openWifiPasswordScreen(wifiListIndex);
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_MENU) {
    if (rootIndex == 0) screen = SCREEN_SENSORS;
    else if (rootIndex == 1) screen = SCREEN_LOG;
    else if (rootIndex == 2) screen = SCREEN_NETWORK;
    else if (rootIndex == 3) screen = SCREEN_SETTINGS;
    else if (rootIndex == 4) screen = SCREEN_SYSTEM;
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_SENSORS) {
    bool* enabled = sensorItems[sensorIndex].enabled;
    *enabled = !*enabled;

    if (&buzzerEnabled == enabled && !buzzerEnabled) stopBuzzer();
    if (&tempSensorEnabled == enabled) {
      tempFailureCount = 0;
      temperature = NAN;

      if (tempSensorEnabled) lastTempSensorRead = millis() - TEMP_SENSOR_INTERVAL;
    }
    if (&humiditySensorEnabled == enabled) {
      tempFailureCount = 0;
      humidity = NAN;

      if (humiditySensorEnabled) lastTempSensorRead = millis() - TEMP_SENSOR_INTERVAL;
    }
    if (&reedSensorEnabled == enabled && !reedSensorEnabled) reedDetected = false;
    if (&waterSensorEnabled == enabled && !waterSensorEnabled) waterDetected = false;
    if (&gasSensorEnabled == enabled && !gasSensorEnabled) gasDetected = false;
    if (&motionSensorEnabled == enabled && !motionSensorEnabled) motionDetected = false;

    char message[80];
    snprintf(message, sizeof(message), "%s: %s", sensorItems[sensorIndex].name, *enabled ? "вкл" : "выкл");
    addNotification(message);
    addLog(message);
    saveSettings();
    markAllDirty();
    return;
  }

  if (screen == SCREEN_NETWORK) {
    if (networkIndex == 0) {
      startWiFi(true);
    } else if (networkIndex == 1) {
      scanWifiNetworksForMenu();
      screen = SCREEN_WIFI_LIST;
    } else if (networkIndex == 2) {
      startWiFiSetupPortal();
    } else if (networkIndex == 3) {
      clearWiFiCredentials();
      startWiFiSetupPortal();
    }
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_SYSTEM) {
    if (systemIndex == 0) screen = SCREEN_STATUS;
    else if (systemIndex == 1) screen = SCREEN_CHARACTERISTICS;
    else if (systemIndex == 2) screen = SCREEN_DESCRIPTION;
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_SETTINGS) {
    if (settingsIndex == 0) screen = SCREEN_PIN_SETTINGS;
    else if (settingsIndex == 1) screen = SCREEN_GUARD_SETTINGS;
    else if (settingsIndex == 2) screen = SCREEN_ALERT_SETTINGS;
    else if (settingsIndex == 3) screen = SCREEN_TELEGRAM_SETTINGS;
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_PIN_SETTINGS) {
    if (pinSettingsIndex == 0) {
      passwordEnabled = !passwordEnabled;
      if (!passwordEnabled) {
        lastUnlockTime = 0;
        resetPinInput();
      }
      addLog(passwordEnabled ? "PIN защита включена" : "PIN защита выключена");
      saveSettings();
    } else if (pinSettingsIndex == 1) {
      resetPinInput();
      screen = SCREEN_SET_PIN;
    } else if (pinSettingsIndex == 2) {
      uint8_t currentLength = pinChangeRequiresNewPin ? pendingPinLength : pinLength;
      pendingPinLength = (currentLength == 4) ? 6 : 4;
      pinChangeRequiresNewPin = true;
      resetPinInput();

      addLog(pendingPinLength == 4 ? "PIN: выбрано 4 цифры" : "PIN: выбрано 6 цифр");
      addNotification("Задай новый PIN");
    } else if (pinSettingsIndex == 3) {
      passwordTimeoutIndex = (passwordTimeoutIndex + 1) % 5;
      addLog("Изменён таймаут PIN");
      saveSettings();
    }
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_GUARD_SETTINGS) {
    if (guardSettingsIndex == 0) {
      toggleGuard();
    } else if (guardSettingsIndex == 1) {
      guardDelayIndex = (guardDelayIndex + 1) % GUARD_DELAY_COUNT;
      addLog("Изменена задержка охраны");
      saveSettings();
    }
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_ALERT_SETTINGS) {
    if (alertSettingsIndex == 0) {
      alertBeepCountIndex = (alertBeepCountIndex + 1) % ALERT_COUNT_OPTION_COUNT;
      addLog("Изменён режим писка");
      saveSettings();
    } else if (alertSettingsIndex == 1) {
      alertBeepDurationIndex = (alertBeepDurationIndex + 1) % ALERT_DURATION_OPTION_COUNT;
      addLog("Изменена длина писка");
      saveSettings();
    }
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_TELEGRAM_SETTINGS) {
    if (telegramSettingsIndex == 0) {
      telegramEnabled = false;
      addLog("Telegram: заглушка");
      addNotification("Telegram отключён");
    } else if (telegramSettingsIndex == 1) {
      addLog("Telegram test: заглушка");
      addNotification("Telegram отключён");
    } else if (telegramSettingsIndex == 2) {
      sendTelegramStatus();
    } else if (telegramSettingsIndex == 3) {
      screen = SCREEN_TELEGRAM_COMMANDS;
    }
    menuDirty = true;
  }
}

void menuMove(int8_t direction) {
  if (screen == SCREEN_PASSWORD || screen == SCREEN_SET_PIN) {
    pinCursor = direction > 0 ? (pinCursor + 1) % 10 : (pinCursor + 9) % 10;
  }
  else if (screen == SCREEN_WIFI_PASSWORD) {
    uint8_t keyCount = currentWifiKeyCount();
    if (keyCount > 0) {
      wifiPasswordCursor = direction > 0
        ? (wifiPasswordCursor + 1) % keyCount
        : (wifiPasswordCursor + keyCount - 1) % keyCount;
    }
  }
  else if (screen == SCREEN_WIFI_LIST) {
    if (wifiMenuCount > 0) {
      wifiListIndex = direction > 0
        ? (wifiListIndex + 1) % wifiMenuCount
        : (wifiListIndex + wifiMenuCount - 1) % wifiMenuCount;

      const uint8_t visibleRows = 6;
      if (wifiListIndex < wifiListScroll) wifiListScroll = wifiListIndex;
      else if (wifiListIndex >= wifiListScroll + visibleRows) wifiListScroll = wifiListIndex - visibleRows + 1;
    }
  }
  else if (screen == SCREEN_MENU) rootIndex = direction > 0 ? (rootIndex + 1) % ROOT_COUNT : (rootIndex + ROOT_COUNT - 1) % ROOT_COUNT;
  else if (screen == SCREEN_SENSORS) sensorIndex = direction > 0 ? (sensorIndex + 1) % SENSOR_COUNT : (sensorIndex + SENSOR_COUNT - 1) % SENSOR_COUNT;
  else if (screen == SCREEN_NETWORK) networkIndex = direction > 0 ? (networkIndex + 1) % 4 : (networkIndex + 3) % 4;
  else if (screen == SCREEN_SETTINGS) settingsIndex = direction > 0 ? (settingsIndex + 1) % 4 : (settingsIndex + 3) % 4;
  else if (screen == SCREEN_PIN_SETTINGS) pinSettingsIndex = direction > 0 ? (pinSettingsIndex + 1) % 4 : (pinSettingsIndex + 3) % 4;
  else if (screen == SCREEN_GUARD_SETTINGS) guardSettingsIndex = direction > 0 ? (guardSettingsIndex + 1) % 2 : (guardSettingsIndex + 1) % 2;
  else if (screen == SCREEN_ALERT_SETTINGS) alertSettingsIndex = direction > 0 ? (alertSettingsIndex + 1) % 2 : (alertSettingsIndex + 1) % 2;
  else if (screen == SCREEN_TELEGRAM_SETTINGS) telegramSettingsIndex = direction > 0 ? (telegramSettingsIndex + 1) % 4 : (telegramSettingsIndex + 3) % 4;
  else if (screen == SCREEN_SYSTEM) systemIndex = direction > 0 ? (systemIndex + 1) % SYSTEM_COUNT : (systemIndex + SYSTEM_COUNT - 1) % SYSTEM_COUNT;
  else if (screen == SCREEN_LOG) {
    const uint8_t pageSize = 7;
    uint16_t totalPages = logCount == 0 ? 1 : ((logCount + pageSize - 1) / pageSize);
    uint16_t maxPage = totalPages > 0 ? totalPages - 1 : 0;

    if (direction > 0 && logScroll < maxPage) logScroll++;
    else if (direction < 0 && logScroll > 0) logScroll--;
  }
  menuDirty = true;
}

void updateMenuTimeout() {
  if (screen == SCREEN_MAIN) return;

  if (millis() - lastInputTime > MENU_TIMEOUT) {
    if (pinChangeRequiresNewPin) {
      forceNewPinIfRequired();
      lastInputTime = millis();
      return;
    }

    closeMenu();
  }
}

// ================= DRAW HELPERS =================
void drawUtf8(const uint8_t* font, int16_t x, int16_t y, uint16_t color, const char* text) {
  u8g2.setFont(font);
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y);
  u8g2.print(text);
}

void drawUtf8Centered(const uint8_t* font, int16_t y, uint16_t color, const char* text) {
  u8g2.setFont(font);
  u8g2.setForegroundColor(color);
  int16_t width = u8g2.getUTF8Width(text);
  u8g2.setCursor((SCREEN_W - width) / 2, y);
  u8g2.print(text);
}

uint16_t getEventColor(const char* text) {
  if (strstr(text, "ТРЕВОГА") != NULL ||
      strstr(text, "Сработал") != NULL ||
      strstr(text, "Обнаружена вода") != NULL ||
      strstr(text, "Обнаружен газ") != NULL ||
      strstr(text, "Обнаружено движение") != NULL ||
      strstr(text, "Неверный PIN") != NULL) {
    return COLOR_OFF;
  }

  if (strstr(text, "Wi-Fi") != NULL ||
      strstr(text, "Система") != NULL ||
      strstr(text, "Охрана включена") != NULL ||
      strstr(text, "Охрана выключена") != NULL ||
      strstr(text, "PIN изменён") != NULL ||
      strstr(text, "Вход") != NULL) {
    return COLOR_ON;
  }

  if (strstr(text, "T ") != NULL || strstr(text, "Температура") != NULL) return COLOR_ACCENT;

  if (strstr(text, "нет") != NULL ||
      strstr(text, "ожид") != NULL ||
      strstr(text, "ошибка") != NULL ||
      strstr(text, "выключ") != NULL) {
    return COLOR_WARN;
  }

  return COLOR_MUTED;
}

void drawPageTitle(const char* title) {
  tft.fillScreen(COLOR_BG);

  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 12, COLOR_LINE);
  tft.drawRoundRect(10, 10, SCREEN_W - 20, 30, 8, COLOR_LINE);
  tft.fillCircle(22, 25, 2, COLOR_TEXT);
  tft.fillCircle(30, 25, 2, COLOR_MUTED);
  tft.fillCircle(38, 25, 2, COLOR_MUTED);

  drawUtf8(u8g2_font_9x15_t_cyrillic, 52, 29, COLOR_TEXT, title);

  for (int16_t x = SCREEN_W - 44; x < SCREEN_W - 16; x += 7) {
    for (int16_t yy = 19; yy < 33; yy += 7) {
      tft.fillCircle(x, yy, 1, COLOR_MUTED);
    }
  }
}

void drawMenuRow(uint8_t index, const char* label, bool selected) {
  int16_t y = 72 + index * 46;
  int16_t x = 14;
  int16_t w = SCREEN_W - 28;
  int16_t h = 34;

  tft.fillRoundRect(x, y - 23, w, h, 8, selected ? COLOR_ROW : COLOR_BG);
  tft.drawRoundRect(x, y - 23, w, h, 8, selected ? COLOR_TEXT : COLOR_LINE);

  if (selected) {
    tft.fillCircle(x + 14, y - 6, 3, COLOR_TEXT);
    tft.drawFastVLine(x + 28, y - 18, 24, COLOR_MUTED);
  } else {
    tft.drawCircle(x + 14, y - 6, 3, COLOR_MUTED);
  }

  drawUtf8(u8g2_font_9x15_t_cyrillic, x + 40, y, selected ? COLOR_TEXT : COLOR_MUTED, label);
}

void drawToggle(int16_t x, int16_t y, bool enabled) {
  const char* label = enabled ? "ON" : "OFF";
  const uint8_t* font = u8g2_font_6x12_t_cyrillic;
  uint16_t borderColor = enabled ? COLOR_TEXT : COLOR_MUTED;
  uint16_t dotColor = enabled ? COLOR_TEXT : COLOR_OFF;

  tft.fillRoundRect(x, y - 21, 56, 25, 8, COLOR_BG);
  tft.drawRoundRect(x, y - 21, 56, 25, 8, borderColor);
  tft.fillCircle(enabled ? x + 43 : x + 13, y - 9, 6, dotColor);

  u8g2.setFont(font);
  int16_t width = u8g2.getUTF8Width(label);
  drawUtf8(font, x + (56 - width) / 2, y + 16, borderColor, label);
}

void drawStatusLetter(const char* letter, int16_t x, int16_t y, uint16_t statusColor) {
  static const int16_t boxW = 33;
  static const int16_t boxH = 22;
  static const int16_t r = 6;

  tft.drawRoundRect(x, y - 12, boxW, boxH, r, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, x + 3, y + 3, COLOR_TEXT, letter);

  tft.fillCircle(x + 24, y - 1, 3, statusColor);
  tft.drawCircle(x + 24, y - 1, 4, COLOR_TEXT);
}

void drawBootScreen() {
  tft.fillScreen(COLOR_BG);

  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 14, COLOR_LINE);
  tft.drawRoundRect(12, 12, SCREEN_W - 24, 34, 10, COLOR_LINE);

  drawUtf8Centered(u8g2_font_9x15_t_cyrillic, 35, COLOR_TEXT, "SYSTEM START");

  int16_t x = 22;
  int16_t y = 76;
  int16_t w = SCREEN_W - 44;

  const char* labels[] = {"DISPLAY", "TEMP", "SOUND", "REED", "WATER", "WIFI", "READY"};
  const char* statuses[] = {"OK", "INIT", "OK", "OK", "OK", wifiConfigured() ? "WAIT" : "OFF", "OK"};

  for (uint8_t i = 0; i < 7; i++) {
    int16_t rowY = y + i * 28;

    tft.drawRoundRect(x, rowY - 16, w, 22, 6, COLOR_LINE);
    tft.fillCircle(x + 12, rowY - 5, 2, COLOR_MUTED);

    drawUtf8(u8g2_font_6x12_t_cyrillic, x + 24, rowY, COLOR_MUTED, labels[i]);

    uint16_t statusColor = COLOR_ON;
    if (strcmp(statuses[i], "WAIT") == 0) statusColor = COLOR_WARN;
    if (strcmp(statuses[i], "OFF") == 0) statusColor = COLOR_MUTED;

    drawUtf8(u8g2_font_6x12_t_cyrillic, x + 145, rowY, statusColor, statuses[i]);

    int16_t barX = x + 24;
    int16_t barY = rowY + 10;
    int16_t barW = w - 48;

    tft.drawRoundRect(barX, barY, barW, 5, 2, COLOR_LINE);
    tft.fillRoundRect(barX + 1, barY + 1, map(i + 1, 0, 7, 0, barW - 2), 3, 1, statusColor);

    delay(180);
  }

  delay(350);
}

// ================= DRAW MAIN =================
void drawTopBar() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_BG);

  tft.drawRoundRect(5, 5, SCREEN_W - 10, 24, 8, COLOR_LINE);
  drawWifiBars(18, 7);

  char dateText[12];
  formatDate(dateText, sizeof(dateText));
  drawUtf8(u8g2_font_6x12_t_cyrillic, 154, 20, COLOR_TEXT, dateText);
}

void drawWifiBars(int16_t x, int16_t y) {
  tft.fillRect(x - 2, y - 2, 38, 22, COLOR_BG);

  uint8_t level = 0;

  if (wifiViewState == WIFI_VIEW_ONLINE) {
    long rssi = WiFi.RSSI();
    level = rssi > -60 ? 3 : (rssi > -75 ? 2 : 1);
  } else if (wifiViewState == WIFI_VIEW_CONNECTING) {
    level = (millis() / 400) % 4;
  }

  for (uint8_t i = 0; i < 3; i++) {
    int16_t barX = x + i * 9;
    int16_t barH = 5 + i * 5;
    int16_t barY = y + 16 - barH;
    uint16_t barColor = i < level ? COLOR_TEXT : COLOR_LINE;
    tft.fillRoundRect(barX, barY, 6, barH, 2, barColor);
  }
}

void drawClockBlock() {
  char timeText[6];
  formatTime(timeText, sizeof(timeText), false);

  tft.fillRect(0, HEADER_H, SCREEN_W, 108, COLOR_BG);
  tft.drawRoundRect(10, HEADER_H + 8, SCREEN_W - 20, 94, 14, COLOR_LINE);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(6);
  tft.setCursor(30, 62);
  tft.print(timeText);

  drawUtf8Centered(u8g2_font_7x13_t_cyrillic, 126, COLOR_MUTED, TZ_LABEL);
}

void drawSensorStrip() {
  char tempText[32];
  char humidityText[24];

  if (!tempSensorEnabled) snprintf(tempText, sizeof(tempText), "TEMP OFF");
  else if (isnan(temperature)) snprintf(tempText, sizeof(tempText), "TEMP ERR");
  else snprintf(tempText, sizeof(tempText), "%.1f C", temperature);

  if (!humiditySensorEnabled) snprintf(humidityText, sizeof(humidityText), "HUM OFF");
  else if (isnan(humidity)) snprintf(humidityText, sizeof(humidityText), "HUM ERR");
  else snprintf(humidityText, sizeof(humidityText), "%.0f %%", humidity);

  tft.fillRect(0, 138, SCREEN_W, NOTIFY_Y - 138, COLOR_BG);
  tft.drawRoundRect(4, 142, SCREEN_W - 8, 64, 12, COLOR_LINE);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(36, 151);
  tft.print(tempText);
  int16_t humidityX = SCREEN_W - 36 - (int16_t)strlen(humidityText) * 12;
  if (humidityX < 124) humidityX = 124;
  tft.setCursor(humidityX, 151);
  tft.print(humidityText);

  tft.drawFastHLine(16, 174, SCREEN_W - 32, COLOR_LINE);

  uint16_t soundColor = !soundSensorEnabled ? COLOR_WARN : (soundDetected ? COLOR_OFF : COLOR_ON);
  uint16_t reedColor = !reedSensorEnabled ? COLOR_WARN : (reedDetected ? COLOR_OFF : COLOR_ON);
  uint16_t waterColor = !waterSensorEnabled ? COLOR_WARN : (waterDetected ? COLOR_OFF : COLOR_ON);
  uint16_t gasColor = !gasSensorEnabled ? COLOR_WARN : (gasDetected ? COLOR_OFF : COLOR_ON);
  uint16_t motionColor = !motionSensorEnabled ? COLOR_WARN : (motionDetected ? COLOR_OFF : COLOR_ON);
  uint16_t buzzerColor = !buzzerEnabled ? COLOR_WARN : (buzzerActive ? COLOR_OFF : COLOR_ON);

  drawStatusLetter("Зв", 6, 190, soundColor);
  drawStatusLetter("Гк", 45, 190, reedColor);
  drawStatusLetter("Вд", 84, 190, waterColor);
  drawStatusLetter("Гз", 123, 190, gasColor);
  drawStatusLetter("Дв", 162, 190, motionColor);
  drawStatusLetter("С", 201, 190, buzzerColor);
}

void drawNotifications() {
  tft.fillRect(0, NOTIFY_Y, SCREEN_W, SCREEN_H - NOTIFY_Y, COLOR_BG);

  tft.drawRoundRect(10, NOTIFY_Y + 4, SCREEN_W - 20, SCREEN_H - NOTIFY_Y - 12, 12, COLOR_LINE);
  tft.fillRoundRect(14, NOTIFY_Y + 8, SCREEN_W - 28, 24, 8, COLOR_PANEL);

  drawUtf8(u8g2_font_9x15_t_cyrillic, 24, NOTIFY_Y + 27, COLOR_TEXT, "Уведомления");

  for (int16_t x = SCREEN_W - 54; x < SCREEN_W - 22; x += 7) {
    for (int16_t yy = NOTIFY_Y + 16; yy < NOTIFY_Y + 30; yy += 7) {
      tft.fillCircle(x, yy, 1, COLOR_MUTED);
    }
  }

  if (notifyCount == 0) {
    drawUtf8(u8g2_font_6x12_t_cyrillic, 24, NOTIFY_Y + 56, COLOR_MUTED, "Пока тихо");
    return;
  }

  for (uint8_t i = 0; i < notifyCount && i < 3; i++) {
    uint8_t source = notifyCount - 1 - i;
    int16_t y = NOTIFY_Y + 52 + i * 18;
    uint16_t eventColor = getEventColor(notifications[source].text);

    tft.drawRoundRect(18, y - 13, SCREEN_W - 36, 16, 4, eventColor);
    tft.fillCircle(26, y - 5, 3, eventColor);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 36, y, COLOR_MUTED, notifications[source].time);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 74, y, COLOR_TEXT, notifications[source].text);
  }
}

void drawAlarmFrame() {
}

void drawMainScreen() {
  tft.fillScreen(COLOR_BG);
  tft.drawRoundRect(3, 3, SCREEN_W - 6, SCREEN_H - 6, 14, COLOR_LINE);

  drawTopBar();
  drawClockBlock();
  drawSensorStrip();
  drawNotifications();
  drawAlarmFrame();
}

// ================= DRAW MENUS =================
void drawRootMenu() {
  drawPageTitle("Меню");

  for (uint8_t i = 0; i < ROOT_COUNT; i++) drawMenuRow(i, rootItems[i], i == rootIndex);
}

void drawSensorsMenu() {
  drawPageTitle("Датчики");

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    int16_t y = 62 + i * 30;
    bool selected = i == sensorIndex;
    int16_t x = 14;
    int16_t w = SCREEN_W - 28;
    int16_t h = 24;

    tft.fillRoundRect(x, y - 17, w, h, 6, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(x, y - 17, w, h, 6, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) {
      tft.fillCircle(x + 12, y - 5, 3, COLOR_TEXT);
      tft.drawFastVLine(x + 25, y - 14, 18, COLOR_MUTED);
    } else {
      tft.drawCircle(x + 12, y - 5, 3, COLOR_MUTED);
    }

    drawUtf8(u8g2_font_6x12_t_cyrillic, x + 36, y, selected ? COLOR_TEXT : COLOR_MUTED, sensorItems[i].name);

    bool enabled = *sensorItems[i].enabled;
    int16_t tx = SCREEN_W - 62;
    tft.fillRoundRect(tx, y - 15, 44, 18, 5, COLOR_BG);
    tft.drawRoundRect(tx, y - 15, 44, 18, 5, enabled ? COLOR_ON : COLOR_OFF);
    drawUtf8(u8g2_font_6x12_t_cyrillic, tx + 10, y - 2, enabled ? COLOR_ON : COLOR_OFF, enabled ? "ON" : "OFF");
  }
}

void drawLogMenu() {
  drawPageTitle("Журнал");

  if (logCount == 0) {
    tft.drawRoundRect(14, 58, SCREEN_W - 28, 42, 8, COLOR_LINE);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 84, COLOR_MUTED, "Событий пока нет");
    return;
  }

  const uint8_t pageSize = 7;
  uint16_t totalPages = (logCount + pageSize - 1) / pageSize;
  if (logScroll >= totalPages) logScroll = totalPages - 1;

  uint8_t visible = logCount - logScroll * pageSize;
  if (visible > pageSize) visible = pageSize;

  int latest = (int)logCount - 1 - (int)logScroll * pageSize;

  char pageText[20];
  snprintf(pageText, sizeof(pageText), "%u/%u", (unsigned)(logScroll + 1), (unsigned)totalPages);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 190, 42, COLOR_MUTED, pageText);

  for (uint8_t i = 0; i < visible; i++) {
    int entryIndex = latest - i;
    if (entryIndex < 0) break;

    int16_t y = 62 + i * 30;
    uint16_t eventColor = getEventColor(logEntries[entryIndex].text);

    tft.drawRoundRect(12, y - 18, SCREEN_W - 24, 24, 6, eventColor);
    tft.fillCircle(22, y - 5, 2, eventColor);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 32, y, COLOR_MUTED, logEntries[entryIndex].time);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 92, y, COLOR_TEXT, logEntries[entryIndex].text);
  }
}

void drawNetworkMenu() {
  drawPageTitle("Сеть");

  const char* status = "не настроена";
  uint16_t statusColor = COLOR_OFF;

  if (wifiSetupMode) {
    status = "setup mode";
    statusColor = COLOR_WARN;
  } else if (wifiViewState == WIFI_VIEW_ONLINE) {
    status = "подключена";
    statusColor = COLOR_ON;
  } else if (wifiViewState == WIFI_VIEW_CONNECTING) {
    status = "поиск";
    statusColor = COLOR_WARN;
  } else if (wifiConfigured()) {
    status = "нет связи";
    statusColor = COLOR_WARN;
  }

  tft.drawRoundRect(14, 52, SCREEN_W - 28, 70, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 74, COLOR_MUTED, "Wi-Fi status");
  drawUtf8(u8g2_font_9x15_t_cyrillic, 28, 98, statusColor, status);

  if (wifiViewState == WIFI_VIEW_ONLINE) {
    char ipText[36];
    snprintf(ipText, sizeof(ipText), "IP %s", WiFi.localIP().toString().c_str());
    drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 116, COLOR_TEXT, ipText);
  } else if (wifiSetupMode) {
    drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 116, COLOR_TEXT, "AP: 192.168.4.1");
  } else {
    drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 116, COLOR_TEXT, activeWifiSsid());
  }

  const char* labels[4] = {"Подключиться", "Выбрать Wi-Fi", "Setup Wi-Fi", "Сброс Wi-Fi"};
  const char* values[4] = {"start", "scan", "portal", "reset"};

  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 148 + i * 36;
    bool selected = i == networkIndex;

    tft.fillRoundRect(14, y - 20, SCREEN_W - 28, 30, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 20, SCREEN_W - 28, 30, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) tft.fillCircle(28, y - 6, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 6, 3, COLOR_MUTED);

    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, selected ? COLOR_TEXT : COLOR_MUTED, labels[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 154, y, COLOR_TEXT, values[i]);
  }
}

void drawWifiListMenu() {
  drawPageTitle("Wi-Fi рядом");

  if (wifiMenuCount == 0) {
    tft.drawRoundRect(14, 58, SCREEN_W - 28, 50, 8, COLOR_LINE);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 88, COLOR_WARN, "Сети не найдены");
    return;
  }

  const uint8_t visibleRows = 6;
  uint8_t rows = wifiMenuCount < visibleRows ? wifiMenuCount : visibleRows;

  char countText[16];
  snprintf(countText, sizeof(countText), "%u", wifiMenuCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 194, 42, COLOR_MUTED, countText);

  for (uint8_t i = 0; i < rows; i++) {
    uint8_t idx = wifiListScroll + i;
    if (idx >= wifiMenuCount) break;

    int16_t y = 70 + i * 38;
    bool selected = idx == wifiListIndex;
    uint16_t rowColor = selected ? COLOR_TEXT : COLOR_LINE;

    tft.fillRoundRect(14, y - 22, SCREEN_W - 28, 30, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 22, SCREEN_W - 28, 30, 8, rowColor);

    if (selected) tft.fillCircle(28, y - 7, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 7, 3, COLOR_MUTED);

    char ssidShort[18];
    strncpy(ssidShort, wifiMenuSsid[idx], sizeof(ssidShort) - 1);
    ssidShort[sizeof(ssidShort) - 1] = '\0';

    char rssiText[16];
    snprintf(rssiText, sizeof(rssiText), "%d", wifiMenuRssi[idx]);

    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, selected ? COLOR_TEXT : COLOR_MUTED, ssidShort);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 172, y, COLOR_MUTED, rssiText);
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 22, 292, COLOR_MUTED, "Вперед - выбрать");
}

void drawWifiPasswordMenu() {
  drawPageTitle("Пароль Wi-Fi");

  tft.drawRoundRect(14, 50, SCREEN_W - 28, 58, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 72, COLOR_MUTED, "SSID");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 72, 72, COLOR_TEXT, selectedWifiSsid);

  char passView[22];
  if (wifiPasswordLength == 0) {
    snprintf(passView, sizeof(passView), "_");
  } else if (wifiPasswordLength <= 20) {
    strncpy(passView, wifiPasswordInput, sizeof(passView) - 1);
    passView[sizeof(passView) - 1] = '\0';
  } else {
    strncpy(passView, wifiPasswordInput + wifiPasswordLength - 20, sizeof(passView) - 1);
    passView[sizeof(passView) - 1] = '\0';
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 94, COLOR_TEXT, passView);

  const char* pageName = wifiKeyboardPage == 0 ? "abc" : (wifiKeyboardPage == 1 ? "ABC" : "симв");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 178, 94, COLOR_MUTED, pageName);

  const char* rowsLower[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char* rowsUpper[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const char* rowsSymbols[] = {"-_@.!?", "#$%&*+"};

  const char** rows = rowsLower;
  uint8_t rowCount = 4;
  if (wifiKeyboardPage == 1) {
    rows = rowsUpper;
    rowCount = 4;
  } else if (wifiKeyboardPage == 2) {
    rows = rowsSymbols;
    rowCount = 2;
  }

  uint8_t globalIndex = 0;
  const int16_t cellW = 21;
  const int16_t cellH = 24;
  const int16_t startY = 126;
  const int16_t rowStep = 31;

  for (uint8_t r = 0; r < rowCount; r++) {
    uint8_t rowLen = strlen(rows[r]);
    int16_t startX = (SCREEN_W - rowLen * cellW) / 2;
    int16_t y = startY + r * rowStep;

    for (uint8_t c = 0; c < rowLen; c++) {
      int16_t x = startX + c * cellW;
      bool selected = globalIndex == wifiPasswordCursor;

      if (selected) {
        tft.fillRoundRect(x - 1, y - 1, cellW, cellH + 2, 5, COLOR_ROW);
        tft.drawRoundRect(x - 1, y - 1, cellW, cellH + 2, 5, COLOR_TEXT);
      }

      char key[2];
      key[0] = rows[r][c];
      key[1] = '\0';
      drawUtf8(u8g2_font_6x12_t_cyrillic, x + 6, y + 16, selected ? COLOR_TEXT : COLOR_MUTED, key);

      globalIndex++;
    }
  }

  if (wifiKeyboardStatus[0] != '\0') {
    uint16_t statusColor = strstr(wifiKeyboardStatus, "Ошибка") != NULL ? COLOR_OFF : COLOR_ON;
    drawUtf8(u8g2_font_6x12_t_cyrillic, 18, 254, statusColor, wifiKeyboardStatus);
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 18, 274, COLOR_MUTED, "NEXT ввод, BACK удалить");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 18, 292, COLOR_MUTED, "hold NEXT save, hold BACK abc/ABC");
}

void drawSettingsMenu() {
  drawPageTitle("Настройки");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 54, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 78, COLOR_TEXT, "Разделы настроек");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 96, COLOR_MUTED, "PIN, охрана, сирена.");

  const char* items[4] = {"PIN", "Охрана", "Оповещение", "Telegram"};

  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 148 + i * 46;
    bool selected = i == settingsIndex;

    tft.fillRoundRect(14, y - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) tft.fillCircle(28, y - 6, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 6, 3, COLOR_MUTED);

    drawUtf8(u8g2_font_9x15_t_cyrillic, 52, y, selected ? COLOR_TEXT : COLOR_MUTED, items[i]);
  }
}

void drawPinSettingsMenu() {
  drawPageTitle("PIN");

  tft.drawRoundRect(14, 52, SCREEN_W - 28, 58, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 76, COLOR_TEXT, "PIN защищает меню.");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 94, COLOR_MUTED, "Без PIN нельзя войти.");

  const char* rowLabels[4] = {"Пароль", "PIN", "Длина", "Запрос"};
  char rowValues[4][18];
  snprintf(rowValues[0], sizeof(rowValues[0]), "%s", passwordEnabled ? "ON" : "OFF");
  snprintf(rowValues[1], sizeof(rowValues[1]), "%s", "Изменить");
  snprintf(rowValues[2], sizeof(rowValues[2]), pinChangeRequiresNewPin ? "%u цифр!" : "%u цифры", getPinEditLength());
  snprintf(rowValues[3], sizeof(rowValues[3]), "%s", passwordTimeoutLabels[passwordTimeoutIndex]);

  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 140 + i * 36;
    bool selected = i == pinSettingsIndex;

    tft.fillRoundRect(14, y - 20, SCREEN_W - 28, 30, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 20, SCREEN_W - 28, 30, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) tft.fillCircle(28, y - 6, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 6, 3, COLOR_MUTED);

    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, selected ? COLOR_TEXT : COLOR_MUTED, rowLabels[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 132, y, COLOR_TEXT, rowValues[i]);
  }
}

void drawGuardSettingsMenu() {
  drawPageTitle("Охрана");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 58, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 78, COLOR_TEXT, "Охрана включает");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 96, COLOR_MUTED, "тревоги датчиков.");

  int16_t y1 = 150;
  bool selected = guardSettingsIndex == 0;
  tft.fillRoundRect(14, y1 - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_ROW : COLOR_BG);
  tft.drawRoundRect(14, y1 - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_TEXT : COLOR_LINE);
  if (selected) tft.fillCircle(28, y1 - 6, 3, COLOR_TEXT);
  else tft.drawCircle(28, y1 - 6, 3, COLOR_MUTED);
  drawUtf8(u8g2_font_9x15_t_cyrillic, 42, y1, selected ? COLOR_TEXT : COLOR_MUTED, "Охрана");
  drawToggle(166, y1, guardEnabled);

  int16_t y2 = 202;
  selected = guardSettingsIndex == 1;
  tft.fillRoundRect(14, y2 - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_ROW : COLOR_BG);
  tft.drawRoundRect(14, y2 - 24, SCREEN_W - 28, 38, 8, selected ? COLOR_TEXT : COLOR_LINE);
  if (selected) tft.fillCircle(28, y2 - 6, 3, COLOR_TEXT);
  else tft.drawCircle(28, y2 - 6, 3, COLOR_MUTED);
  drawUtf8(u8g2_font_9x15_t_cyrillic, 42, y2, selected ? COLOR_TEXT : COLOR_MUTED, "Задержка");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 150, y2, COLOR_TEXT, guardDelayLabels[guardDelayIndex]);
}

void drawAlertSettingsMenu() {
  drawPageTitle("Оповещение");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 72, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 76, COLOR_TEXT, "Настройка сирены.");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 94, COLOR_MUTED, "BACK выключает");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 110, COLOR_MUTED, "активную тревогу.");

  const char* rowLabels[2] = {"Писки", "Длина"};
  const char* rowValues[2] = {alertBeepCountLabels[alertBeepCountIndex], alertBeepDurationLabels[alertBeepDurationIndex]};

  for (uint8_t i = 0; i < 2; i++) {
    int16_t y = 158 + i * 46;
    bool selected = i == alertSettingsIndex;

    tft.fillRoundRect(14, y - 22, SCREEN_W - 28, 32, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 22, SCREEN_W - 28, 32, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) tft.fillCircle(28, y - 6, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 6, 3, COLOR_MUTED);

    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, selected ? COLOR_TEXT : COLOR_MUTED, rowLabels[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 122, y, COLOR_TEXT, rowValues[i]);
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 22, 286, COLOR_MUTED, "Вперед - изменить");
}

void drawTelegramSettingsMenu() {
  drawPageTitle("Telegram");

  tft.drawRoundRect(14, 52, SCREEN_W - 28, 74, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 74, COLOR_TEXT, "Telegram отключён.");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 92, COLOR_WARN, "Это заглушка меню.");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 110, COLOR_MUTED, "Запросы в сеть не идут.");

  const char* rowLabels[4] = {"Отправка", "Тест", "Статус", "Команды"};
  const char* rowValues[4] = {"OFF", "заглушка", "заглушка", "открыть"};

  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 160 + i * 40;
    bool selected = i == telegramSettingsIndex;

    tft.fillRoundRect(14, y - 22, SCREEN_W - 28, 32, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(14, y - 22, SCREEN_W - 28, 32, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) tft.fillCircle(28, y - 7, 3, COLOR_TEXT);
    else tft.drawCircle(28, y - 7, 3, COLOR_MUTED);

    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, selected ? COLOR_TEXT : COLOR_MUTED, rowLabels[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 132, y, COLOR_TEXT, rowValues[i]);
  }
}

void drawTelegramCommandsMenu() {
  drawPageTitle("Команды");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 224, 10, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 78, COLOR_TEXT, "Команды Telegram:");
  tft.drawFastHLine(24, 94, SCREEN_W - 48, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 120, COLOR_TEXT, "/status");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 120, COLOR_MUTED, "состояние");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 146, COLOR_TEXT, "/guard_on");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 146, COLOR_MUTED, "охрана ON");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 172, COLOR_TEXT, "/guard_off");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 172, COLOR_MUTED, "охрана OFF");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 198, COLOR_TEXT, "/alarm_reset");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 128, 198, COLOR_MUTED, "сброс");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 224, COLOR_TEXT, "/help");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 224, COLOR_MUTED, "список команд");

  tft.drawFastHLine(24, 242, SCREEN_W - 48, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 262, COLOR_WARN, "Опрос команд отключён");
}

void drawSetPinScreen() {
  tft.fillScreen(COLOR_BG);
  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 12, COLOR_LINE);
  tft.drawRoundRect(10, 10, SCREEN_W - 20, 30, 8, COLOR_LINE);
  drawUtf8Centered(u8g2_font_9x15_t_cyrillic, 29, COLOR_TEXT, "Новый PIN");

  tft.drawRoundRect(28, 54, SCREEN_W - 56, 42, 10, COLOR_LINE);

  uint8_t targetPinLength = getPinEditLength();
  char pinMask[7];
  memset(pinMask, 0, sizeof(pinMask));
  for (uint8_t i = 0; i < targetPinLength; i++) pinMask[i] = i < pinInputLength ? '*' : '_';
  pinMask[targetPinLength] = '\0';
  drawUtf8Centered(u8g2_font_10x20_t_cyrillic, 83, COLOR_TEXT, pinMask);

  for (uint8_t digit = 0; digit < 10; digit++) {
    uint8_t row;
    uint8_t col;

    if (digit == 0) {
      row = 3;
      col = 1;
    } else {
      row = (digit - 1) / 3;
      col = (digit - 1) % 3;
    }

    int16_t x = 42 + col * 56;
    int16_t y = 116 + row * 42;
    bool selected = digit == pinCursor;

    tft.fillRoundRect(x, y, 42, 30, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(x, y, 42, 30, 8, selected ? COLOR_TEXT : COLOR_LINE);

    char d[2];
    snprintf(d, sizeof(d), "%u", digit);
    drawUtf8(u8g2_font_9x15_t_cyrillic, x + 16, y + 21, selected ? COLOR_TEXT : COLOR_MUTED, d);
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 20, 292, COLOR_MUTED, "Вперед - ввод, Назад - удалить");
}

void drawPasswordScreen() {
  tft.fillScreen(COLOR_BG);
  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 12, COLOR_LINE);
  tft.drawRoundRect(10, 10, SCREEN_W - 20, 30, 8, COLOR_LINE);
  drawUtf8Centered(u8g2_font_9x15_t_cyrillic, 29, COLOR_TEXT, "PIN");

  tft.drawRoundRect(28, 54, SCREEN_W - 56, 42, 10, COLOR_LINE);

  char pinMask[7];
  memset(pinMask, 0, sizeof(pinMask));
  for (uint8_t i = 0; i < pinLength; i++) pinMask[i] = i < pinInputLength ? '*' : '_';
  pinMask[pinLength] = '\0';
  drawUtf8Centered(u8g2_font_10x20_t_cyrillic, 83, COLOR_TEXT, pinMask);

  for (uint8_t digit = 0; digit < 10; digit++) {
    uint8_t row;
    uint8_t col;

    if (digit == 0) {
      row = 3;
      col = 1;
    } else {
      row = (digit - 1) / 3;
      col = (digit - 1) % 3;
    }

    int16_t x = 42 + col * 56;
    int16_t y = 116 + row * 42;
    bool selected = digit == pinCursor;

    tft.fillRoundRect(x, y, 42, 30, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(x, y, 42, 30, 8, selected ? COLOR_TEXT : COLOR_LINE);

    char d[2];
    snprintf(d, sizeof(d), "%u", digit);
    drawUtf8(u8g2_font_9x15_t_cyrillic, x + 16, y + 21, selected ? COLOR_TEXT : COLOR_MUTED, d);
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, 24, 296, COLOR_MUTED, "Вперед - ввод, Назад - удалить");
}

void drawSystemMenu() {
  drawPageTitle("Система");

  for (uint8_t i = 0; i < SYSTEM_COUNT; i++) drawMenuRow(i, systemItems[i], i == systemIndex);
}

void drawStatusMenu() {
  drawPageTitle("Состояние");

  tft.drawRoundRect(10, 52, SCREEN_W - 20, 236, 10, COLOR_LINE);

  const char* guardText = "OFF";
  uint16_t guardColor = COLOR_WARN;
  if (guardActive) {
    guardText = "ON";
    guardColor = COLOR_ON;
  } else if (guardPending) {
    guardText = "WAIT";
    guardColor = COLOR_WARN;
  }

  const char* alarmText = alarmActive ? alarmSource : "нет";
  uint16_t alarmColor = alarmActive ? COLOR_OFF : COLOR_ON;

  const char* wifiText = WiFi.status() == WL_CONNECTED ? "online" : "offline";
  uint16_t wifiColor = WiFi.status() == WL_CONNECTED ? COLOR_ON : COLOR_WARN;

  const char* tempText = (!tempSensorEnabled) ? "выкл" : (!isnan(temperature) ? "OK" : "нет сигнала");
  uint16_t tempColor = (!tempSensorEnabled) ? COLOR_WARN : (!isnan(temperature) ? COLOR_ON : COLOR_OFF);

  const char* humidityText = (!humiditySensorEnabled) ? "выкл" : (!isnan(humidity) ? "OK" : "нет сигнала");
  uint16_t humidityColor = (!humiditySensorEnabled) ? COLOR_WARN : (!isnan(humidity) ? COLOR_ON : COLOR_OFF);

  const char* soundText = (!soundSensorEnabled) ? "выкл" : (soundDetected ? "сработал" : "OK");
  uint16_t soundColor = (!soundSensorEnabled) ? COLOR_WARN : (soundDetected ? COLOR_OFF : COLOR_ON);

  const char* reedText = (!reedSensorEnabled) ? "выкл" : (reedDetected ? "сработал" : "OK");
  uint16_t reedColor = (!reedSensorEnabled) ? COLOR_WARN : (reedDetected ? COLOR_OFF : COLOR_ON);

  const char* waterText = (!waterSensorEnabled) ? "выкл" : (waterDetected ? "вода" : "сухо");
  uint16_t waterColor = (!waterSensorEnabled) ? COLOR_WARN : (waterDetected ? COLOR_OFF : COLOR_ON);

  const char* gasText = (!gasSensorEnabled) ? "выкл" : (gasDetected ? "газ" : "OK");
  uint16_t gasColor = (!gasSensorEnabled) ? COLOR_WARN : (gasDetected ? COLOR_OFF : COLOR_ON);

  const char* motionText = (!motionSensorEnabled) ? "выкл" : (motionDetected ? "движение" : "OK");
  uint16_t motionColor = (!motionSensorEnabled) ? COLOR_WARN : (motionDetected ? COLOR_OFF : COLOR_ON);

  const char* buzzerText = (!buzzerEnabled) ? "выкл" : (buzzerActive ? "звучит" : "готова");
  uint16_t buzzerColor = (!buzzerEnabled) ? COLOR_WARN : (buzzerActive ? COLOR_OFF : COLOR_ON);

  const char* labels[] = {"Охрана", "Тревога", "Wi-Fi", "Темп", "Влажн", "Звук", "Геркон", "Вода", "Газ", "Движ", "Сирена"};
  const char* values[] = {guardText, alarmText, wifiText, tempText, humidityText, soundText, reedText, waterText, gasText, motionText, buzzerText};
  uint16_t colors[] = {guardColor, alarmColor, wifiColor, tempColor, humidityColor, soundColor, reedColor, waterColor, gasColor, motionColor, buzzerColor};

  for (uint8_t i = 0; i < 11; i++) {
    int16_t y = 70 + i * 19;

    tft.drawRoundRect(18, y - 14, SCREEN_W - 36, 17, 5, colors[i]);
    tft.fillCircle(28, y - 6, 3, colors[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 42, y, COLOR_MUTED, labels[i]);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 126, y, COLOR_TEXT, values[i]);
  }
}

void drawCharacteristicsMenu() {
  drawPageTitle("Характеристики");

  char line[48];
  const int16_t x = 18;
  const int16_t w = SCREEN_W - 36;

  tft.drawRoundRect(x, 54, w, 224, 10, COLOR_LINE);

  snprintf(line, sizeof(line), "Плата: ESP32-S3");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 78, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Экран: %s %dx%d", USE_ST7789 ? "ST7789" : "ILI9341", tft.width(), tft.height());
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 100, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Датчик: NTC 10k");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 122, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Звук: %lu", (unsigned long)soundClickCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 144, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Геркон: %lu", (unsigned long)reedEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 166, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Вода: %lu", (unsigned long)waterEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 188, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Газ: %lu", (unsigned long)gasEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 210, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Движ: %lu", (unsigned long)motionEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 232, COLOR_TEXT, line);

  unsigned long uptime = millis() / 1000;
  snprintf(line, sizeof(line), "Uptime: %lum %lus", uptime / 60, uptime % 60);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 254, COLOR_TEXT, line);

  drawUtf8(u8g2_font_9x15_t_cyrillic, 32, 276,
           buzzerEnabled ? COLOR_ON : COLOR_OFF,
           buzzerEnabled ? "Сирена готова" : "Сирена выкл");
}

void drawDescriptionMenu() {
  drawPageTitle("Описание");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 236, 10, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 76, COLOR_TEXT, "Система мониторинга");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 92, COLOR_MUTED, "темп, влажность,");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 108, COLOR_MUTED, "звук, воду, газ, движение.");

  tft.drawFastHLine(24, 124, SCREEN_W - 48, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 146, COLOR_TEXT, "Зв - звук");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 164, COLOR_TEXT, "Гк - геркон");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 182, COLOR_TEXT, "Вд - вода");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 200, COLOR_TEXT, "Гз - газ");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 128, 146, COLOR_TEXT, "Дв - движение");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 128, 164, COLOR_TEXT, "С - сирена");

  tft.drawFastHLine(24, 214, SCREEN_W - 48, COLOR_LINE);

  tft.fillCircle(34, 232, 4, COLOR_ON);
  tft.drawCircle(34, 232, 5, COLOR_TEXT);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 50, 236, COLOR_TEXT, "работает / норма");

  tft.fillCircle(34, 252, 4, COLOR_OFF);
  tft.drawCircle(34, 252, 5, COLOR_TEXT);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 50, 256, COLOR_TEXT, "оповещение");

  tft.fillCircle(34, 272, 4, COLOR_WARN);
  tft.drawCircle(34, 272, 5, COLOR_TEXT);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 50, 276, COLOR_TEXT, "отключен");
}

void drawMenuScreen() {
  if (screen == SCREEN_MENU) drawRootMenu();
  else if (screen == SCREEN_SENSORS) drawSensorsMenu();
  else if (screen == SCREEN_LOG) drawLogMenu();
  else if (screen == SCREEN_NETWORK) drawNetworkMenu();
  else if (screen == SCREEN_WIFI_LIST) drawWifiListMenu();
  else if (screen == SCREEN_WIFI_PASSWORD) drawWifiPasswordMenu();
  else if (screen == SCREEN_SETTINGS) drawSettingsMenu();
  else if (screen == SCREEN_PIN_SETTINGS) drawPinSettingsMenu();
  else if (screen == SCREEN_GUARD_SETTINGS) drawGuardSettingsMenu();
  else if (screen == SCREEN_ALERT_SETTINGS) drawAlertSettingsMenu();
  else if (screen == SCREEN_TELEGRAM_SETTINGS) drawTelegramSettingsMenu();
  else if (screen == SCREEN_TELEGRAM_COMMANDS) drawTelegramCommandsMenu();
  else if (screen == SCREEN_PASSWORD) drawPasswordScreen();
  else if (screen == SCREEN_SET_PIN) drawSetPinScreen();
  else if (screen == SCREEN_SYSTEM) drawSystemMenu();
  else if (screen == SCREEN_STATUS) drawStatusMenu();
  else if (screen == SCREEN_CHARACTERISTICS) drawCharacteristicsMenu();
  else if (screen == SCREEN_DESCRIPTION) drawDescriptionMenu();
}

// ================= UI UPDATE =================
void markAllDirty() {
  markMainDirty(true);
  menuDirty = true;
}

void markMainDirty(bool fullRedraw) {
  if (fullRedraw) mainFullDirty = true;

  topBarDirty = true;
  clockDirty = true;
  sensorStripDirty = true;
  notificationsDirty = true;
  mainDirty = true;
}

void updateUi() {
  unsigned long now = millis();
  if (now - lastUiUpdate < UI_INTERVAL) return;
  lastUiUpdate = now;

  if (wifiViewState == WIFI_VIEW_CONNECTING) {
    topBarDirty = true;
    mainDirty = true;
    if (screen == SCREEN_NETWORK) menuDirty = true;
  }

  if (screen == SCREEN_MAIN) {
    if (mainDirty) {
      if (mainFullDirty) {
        drawMainScreen();
        mainFullDirty = false;
        topBarDirty = false;
        clockDirty = false;
        sensorStripDirty = false;
        notificationsDirty = false;
      } else {
        if (topBarDirty) {
          drawTopBar();
          topBarDirty = false;
        }
        if (clockDirty) {
          drawClockBlock();
          clockDirty = false;
        }
        if (sensorStripDirty) {
          drawSensorStrip();
          sensorStripDirty = false;
        }
        if (notificationsDirty) {
          drawNotifications();
          notificationsDirty = false;
        }
      }

      mainDirty = topBarDirty || clockDirty || sensorStripDirty || notificationsDirty || mainFullDirty;
    }
  } else if (screen == SCREEN_STATUS && now - lastStatusRefresh >= 500) {
    lastStatusRefresh = now;
    drawStatusMenu();
    drawAlarmFrame();
    menuDirty = false;
  } else if (menuDirty) {
    drawMenuScreen();
    drawAlarmFrame();
    menuDirty = false;
  }

  if (alarmActive) drawAlarmFrame();
}
