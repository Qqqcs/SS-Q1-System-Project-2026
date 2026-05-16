#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <Preferences.h>
#include "esp_heap_caps.h"

// ================= CONFIG =================
#ifndef USE_WOKWI
#define USE_WOKWI 0
#endif

// ================= WIFI DATA =================
#define REAL_WIFI_SSID ""  // NA, чтобы можно было настроить через функцию setup wifi
#define REAL_WIFI_PASS ""

// ================= TELEGRAM DATA =================
#define TELEGRAM_BOT_TOKEN "7890181496:AAEyj3kGWkmiqnBLqDW8brTNyvMbiVtf8FI"  //Бот SS-Q1
#define TELEGRAM_CHAT_ID "1092905120"

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

static const uint8_t DHT_PIN = 5;
static const uint8_t ENC_CLK = 7;
static const uint8_t ENC_DT = 6;
static const uint8_t BTN_NEXT = 9;
static const uint8_t BTN_BACK = 10;
static const uint8_t SOUND_DO_PIN = 11;
static const uint8_t WATER_PIN = 1;  // S датчика воды
static const uint8_t REED_DO_PIN = 12;
static const uint8_t REED_AO_PIN = 13;
static const uint8_t BUZZER_PIN = 14;

// RFID RC522. SCK/MOSI общие с TFT, MISO нужен только RFID.
static const uint8_t RFID_SS_PIN = 2;    // SDA / SS на модуле RC522
static const uint8_t RFID_RST_PIN = 21;  // RST на модуле RC522
static const uint8_t RFID_MISO_PIN = 8;  // MISO на модуле RC522

#if USE_WOKWI
static const uint8_t DHT_TYPE = DHT22;
#else
static const uint8_t DHT_TYPE = DHT11;
#endif

// По успешному тесту звука: DO changed: 1 active=YES, DO changed: 0 active=NO.
static const uint8_t SOUND_PIN_MODE = INPUT_PULLUP;
static const uint8_t SOUND_ACTIVE_LEVEL = HIGH;

// Герконовый модуль: обычно active LOW. Если будет наоборот, поменяй LOW на HIGH.
static const uint8_t REED_PIN_MODE = INPUT_PULLUP;
static const uint8_t REED_ACTIVE_LEVEL = LOW;

// Датчик воды: + -> 3.3V, - -> GND, S -> GPIO1.
// Чем больше значение analogRead, тем больше воды на датчике.
static const int WATER_THRESHOLD = 1200;


// ================= DISPLAY / UI =================
static const int16_t TFT_W = 240;
static const int16_t TFT_H = 320;
static const uint8_t DISPLAY_ROTATION = 2;

static const int16_t SCREEN_W = 240;
static const int16_t SCREEN_H = 320;
static const int16_t HEADER_H = 30;
static const int16_t NOTIFY_Y = 212;

static const uint16_t COLOR_BG = ST77XX_BLACK;
static const uint16_t COLOR_TEXT = ST77XX_WHITE;
static const uint16_t COLOR_PANEL = 0x2104;
static const uint16_t COLOR_ROW = 0x2104;
static const uint16_t COLOR_LINE = 0x4208;
static const uint16_t COLOR_MUTED = 0x9CF3;
static const uint16_t COLOR_ACCENT = 0x05FF;
static const uint16_t COLOR_ON = ST77XX_GREEN;
static const uint16_t COLOR_OFF = ST77XX_RED;
static const uint16_t COLOR_WARN = ST77XX_YELLOW;

class BufferedTFT : public Adafruit_GFX {
public:
  BufferedTFT(int16_t w, int16_t h)
    : Adafruit_GFX(w, h), _w(w), _h(h) {}

  bool begin(Adafruit_ST7789* display) {
    _display = display;

    size_t bytes = (size_t)_w * (size_t)_h * 2;
    _buffer = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (_buffer != nullptr) {
      memset(_buffer, 0, bytes);
      Serial.print("Framebuffer in PSRAM: ");
      Serial.print(bytes);
      Serial.println(" bytes");
      return true;
    }

    Serial.println("Framebuffer disabled: PSRAM not available");
    return false;
  }

  bool isBuffered() const {
    return _buffer != nullptr;
  }

  uint16_t* getBuffer() {
    return _buffer;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return;

    if (_buffer != nullptr) {
      _buffer[(int32_t)y * _w + x] = color;
    } else if (_display != nullptr) {
      _display->drawPixel(x, y, color);
    }
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (y < 0 || y >= _h || w <= 0) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (x + w > _w) w = _w - x;
    if (w <= 0) return;

    if (_buffer != nullptr) {
      uint16_t* row = _buffer + (int32_t)y * _w + x;
      for (int16_t i = 0; i < w; i++) row[i] = color;
    } else if (_display != nullptr) {
      _display->drawFastHLine(x, y, w, color);
    }
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (x < 0 || x >= _w || h <= 0) return;
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (y + h > _h) h = _h - y;
    if (h <= 0) return;

    if (_buffer != nullptr) {
      for (int16_t i = 0; i < h; i++) {
        _buffer[(int32_t)(y + i) * _w + x] = color;
      }
    } else if (_display != nullptr) {
      _display->drawFastVLine(x, y, h, color);
    }
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w <= 0 || h <= 0) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x + w > _w) w = _w - x;
    if (y + h > _h) h = _h - y;
    if (w <= 0 || h <= 0) return;

    if (_buffer != nullptr) {
      for (int16_t yy = 0; yy < h; yy++) {
        uint16_t* row = _buffer + (int32_t)(y + yy) * _w + x;
        for (int16_t xx = 0; xx < w; xx++) row[xx] = color;
      }
    } else if (_display != nullptr) {
      _display->fillRect(x, y, w, h, color);
    }
  }

  void fillScreen(uint16_t color) override {
    if (_buffer != nullptr) {
      size_t pixels = (size_t)_w * (size_t)_h;
      for (size_t i = 0; i < pixels; i++) _buffer[i] = color;
    } else if (_display != nullptr) {
      _display->fillScreen(color);
    }
  }

private:
  Adafruit_ST7789* _display = nullptr;
  uint16_t* _buffer = nullptr;
  int16_t _w;
  int16_t _h;
};

Adafruit_ST7789 realTft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
BufferedTFT tft(SCREEN_W, SCREEN_H);
U8G2_FOR_ADAFRUIT_GFX u8g2;
DHT dht(DHT_PIN, DHT_TYPE);
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
Preferences prefs;
WebServer wifiPortalServer(80);

// ================= TIMINGS =================
static const unsigned long UI_INTERVAL = 35;
static const unsigned long BUTTON_DEBOUNCE = 70;
static const unsigned long ENCODER_DEBOUNCE = 4;
static const unsigned long DHT_INTERVAL = 5000;
static const unsigned long TEMP_NOTIFY_INTERVAL = 60000;
static const unsigned long SOUND_HOLD_TIME = 700;
static const unsigned long SOUND_RETRIGGER_DELAY = 250;
static const unsigned long SOUND_BOOT_IGNORE_TIME = 2000;
static const unsigned long REED_BOOT_IGNORE_TIME = 2000;
static const unsigned long REED_HOLD_TIME = 900;
static const unsigned long REED_RETRIGGER_DELAY = 500;
static const unsigned long WATER_RETRIGGER_DELAY = 1000;
static const unsigned long RFID_RETRIGGER_DELAY = 1500;
static const unsigned long RFID_HOLD_TIME = 1200;
static const unsigned long RFID_POLL_INTERVAL = 100;
static const unsigned long BUZZER_DURATION = 1000;
static const unsigned long WIFI_RETRY_INTERVAL = 15000;
static const unsigned long WIFI_CONNECT_TIMEOUT = 9000;
static const unsigned long MENU_TIMEOUT = 30000;
static const unsigned long NOTIFICATION_CLEAR_HOLD = 5000;
static const unsigned long PIN_RESET_HOLD_TIME = 10000;

// ================= STATE =================
enum AppScreen : uint8_t {
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_SENSORS,
  SCREEN_LOG,
  SCREEN_NETWORK,
  SCREEN_WIFI_SETUP_INFO,
  SCREEN_SETTINGS,
  SCREEN_PIN_SETTINGS,
  SCREEN_GUARD_SETTINGS,
  SCREEN_ALERT_SETTINGS,
  SCREEN_TELEGRAM_SETTINGS,
  SCREEN_TELEGRAM_COMMANDS,
  SCREEN_PASSWORD,
  SCREEN_SET_PIN,
  SCREEN_SYSTEM,
  SCREEN_TETRIS,
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
bool dhtSensorEnabled = true;
bool rfidEnabled = true;
bool buzzerEnabled = true;
bool buzzerActive = false;
bool soundDetected = false;
bool reedDetected = false;
bool waterDetected = false;
bool rfidDetected = false;
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
unsigned long lastNextButtonTime = 0;
unsigned long lastBackButtonTime = 0;
unsigned long lastEncoderTime = 0;
unsigned long lastDhtRead = 0;
unsigned long lastTempNotify = 0;
unsigned long lastSoundSeen = 0;
unsigned long lastSoundEvent = 0;
unsigned long lastReedSeen = 0;
unsigned long lastReedEvent = 0;
unsigned long lastWaterEvent = 0;
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
uint8_t lastEncoderState = 0;
int8_t encoderStepAccumulator = 0;
unsigned long lastEncoderStableAt = 0;
long lastMinuteKey = -1;
uint8_t dhtFailureCount = 0;

bool lastSoundRawActive = false;
bool lastReedRawActive = false;
bool lastWaterWet = false;
uint32_t soundClickCount = 0;
uint32_t reedEventCount = 0;
uint32_t waterEventCount = 0;
uint32_t rfidEventCount = 0;
unsigned long lastRfidEvent = 0;
unsigned long lastRfidPoll = 0;
bool rfidReady = false;
uint8_t rfidVersionReg = 0;
char lastRfidUid[24] = "";
char pinStatusText[32] = "";
unsigned long pinStatusUntil = 0;

// UID разрешенной белой карточки которая в комплекте там была. Студенческий не примет, потому что там другой UID, но поставить его туда можно
const char* AUTHORIZED_RFID_UID = "83 D1 9E 1E";

// ================= TELEGRAM SETTINGS =================
bool telegramEnabled = true;
unsigned long lastTelegramAttempt = 0;
unsigned long lastTelegramPoll = 0;
long lastTelegramUpdateId = 0;
bool telegramBacklogCleared = false;
static const unsigned long TELEGRAM_POLL_INTERVAL = 3000;
static const unsigned long TELEGRAM_SEND_MIN_INTERVAL = 3000;
static const unsigned long TELEGRAM_HTTP_TIMEOUT = 900;
static const uint8_t TELEGRAM_QUEUE_CAPACITY = 6;
static const size_t TELEGRAM_QUEUE_MESSAGE_SIZE = 384;

// ================= WIFI SETUP PORTAL =================
char storedWifiSsid[33] = "";
char storedWifiPass[65] = "";
bool wifiSetupMode = false;
bool wifiPortalReconnectPending = false;
unsigned long wifiPortalSavedAt = 0;
static const char* WIFI_SETUP_AP_SSID = "SS-Q1-SETUP";
static const char* WIFI_SETUP_AP_PASS = "12345678";

// ================= TETRIS GAME =================
static const uint8_t TETRIS_W = 10;
static const uint8_t TETRIS_H = 18;
static const int16_t TETRIS_CELL = 12;
static const int16_t TETRIS_X = 18;
static const int16_t TETRIS_Y = 62;

bool tetrisRunning = false;
bool tetrisGameOver = false;
uint16_t tetrisScore = 0;
uint16_t tetrisHighScore = 0;
uint8_t tetrisNextPiece = 0;
bool tetrisNextReady = false;
bool tetrisNewRecord = false;
uint8_t tetrisBoard[TETRIS_H][TETRIS_W];
uint8_t tetrisPiece = 0;
uint8_t tetrisRotation = 0;
int8_t tetrisPieceX = 3;
int8_t tetrisPieceY = 0;
unsigned long lastTetrisFall = 0;
unsigned long tetrisFallInterval = 550;

const uint16_t TETRIS_COLORS[] = {
  COLOR_ACCENT,
  COLOR_ON,
  COLOR_WARN,
  COLOR_OFF,
  ST77XX_BLUE,
  ST77XX_MAGENTA,
  ST77XX_CYAN
};

const uint16_t TETRIS_SHAPES[7][4] = {
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
  { 0x2E00, 0x4460, 0x0E80, 0xC440 },
  { 0x6600, 0x6600, 0x6600, 0x6600 },
  { 0x6C00, 0x4620, 0x06C0, 0x8C40 },
  { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
  { 0xC600, 0x2640, 0x0C60, 0x4C80 }
};

struct TelegramQueueItem {
  char text[TELEGRAM_QUEUE_MESSAGE_SIZE];
};

QueueHandle_t telegramQueue = nullptr;
TaskHandle_t telegramTaskHandle = nullptr;

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
  { "Звук", &soundSensorEnabled },
  { "Геркон", &reedSensorEnabled },
  { "Вода", &waterSensorEnabled },
  { "DHT", &dhtSensorEnabled },
  { "RFID", &rfidEnabled },
  { "Сирена", &buzzerEnabled }
};

static const uint8_t SENSOR_COUNT = sizeof(sensorItems) / sizeof(sensorItems[0]);
const char* rootItems[] = { "Датчики", "Журнал", "Сеть", "Настройки", "Система" };
const char* systemItems[] = { "Состояние", "Характеристики", "Описание", "Тетрис" };
static const uint8_t SYSTEM_COUNT = sizeof(systemItems) / sizeof(systemItems[0]);
static const uint8_t ROOT_COUNT = sizeof(rootItems) / sizeof(rootItems[0]);

// ================= PROTOTYPES =================
void flushFrame();
void drawBootScreen();
void drawMainScreen();
void drawMenuScreen();
void drawRootMenu();
void drawSensorsMenu();
void drawLogMenu();
void drawNetworkMenu();
void drawWifiSetupInfoMenu();
void drawSettingsMenu();
void drawPinSettingsMenu();
void drawGuardSettingsMenu();
void drawAlertSettingsMenu();
void drawTelegramSettingsMenu();
void drawTelegramCommandsMenu();
void drawPasswordScreen();
void drawSetPinScreen();
void drawSystemMenu();
void drawTetrisScreen();
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
void initTetrisGame();
void updateTetrisGame();
bool tetrisCollision(int8_t x, int8_t y, uint8_t piece, uint8_t rotation);
void tetrisSpawnPiece();
void tetrisLockPiece();
void tetrisClearLines();
void tetrisMove(int8_t dx);
void tetrisRotatePiece();
void tetrisSoftDrop();
void tetrisHardDrop();
void tetrisExit();
void markAllDirty();
void markMainDirty(bool fullRedraw = false);
void handleButtons();
void handleEncoder();
void handleShortButtonPress(bool nextButton);
void handleSound();
void handleReed();
void handleWater();
void handleRfid();
void formatRfidUid(MFRC522::Uid* uid, char* buffer, size_t size);
bool rfidAccessConfigured();
bool rfidAuthorized(const char* uid);
void handleBuzzer();
void startAlarmBuzzer();
void startConfiguredBuzzer(bool continuousAllowed);
void updateGuard();
void toggleGuard();
void triggerAlarm(const char* source);
void resetAlarm();
bool telegramConfigured();
String urlEncode(const String& value);
bool sendTelegramMessage(const String& text);
bool sendTelegramMessageNow(const char* text);
void telegramWorkerTask(void* parameter);
void sendTelegramStatus();
void sendTelegramClimate();
void checkTelegramCommands();
void handleTelegramCommand(const String& command);
const char* activeWifiSsid();
const char* activeWifiPass();
void startWiFiSetupPortal();
void handleWiFiSetupPortal();
void stopWiFiSetupPortal();
void saveWiFiCredentials(const String& ssid, const String& pass);
void clearWiFiCredentials();
void readDht();
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
void cleanPinTail(char* pin, uint8_t len);
bool isValidPinString(const char* pin, uint8_t len);
bool pinsEqual(const char* a, const char* b, uint8_t len);
void normalizePinState();
void setDefaultPinForCurrentLength();
void resetPinInput();
void showPinStatus(const char* text);
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

// ================= SETUP / LOOP =================
void loadSettings() {
  prefs.begin("monitor", true);

  String wifiSsid = prefs.getString("wifi_ssid", "");
  String wifiPass = prefs.getString("wifi_pass", "");
  wifiSsid.toCharArray(storedWifiSsid, sizeof(storedWifiSsid));
  wifiPass.toCharArray(storedWifiPass, sizeof(storedWifiPass));

  soundSensorEnabled = prefs.getBool("snd_en", soundSensorEnabled);
  reedSensorEnabled = prefs.getBool("reed_en", reedSensorEnabled);
  waterSensorEnabled = prefs.getBool("water_en", waterSensorEnabled);
  dhtSensorEnabled = prefs.getBool("dht_en", dhtSensorEnabled);
  rfidEnabled = prefs.getBool("rfid_en", rfidEnabled);
  buzzerEnabled = prefs.getBool("buzz_en", buzzerEnabled);

  passwordEnabled = prefs.getBool("pwd_en", passwordEnabled);
  pinLength = prefs.getUChar("pin_len", pinLength);
  if (pinLength != 4 && pinLength != 6) pinLength = 4;
  pendingPinLength = pinLength;
  pinChangeRequiresNewPin = false;

  String saved = prefs.getString("pin", "");
  memset(savedPin, 0, sizeof(savedPin));
  saved.toCharArray(savedPin, sizeof(savedPin));

  normalizePinState();

  passwordTimeoutIndex = prefs.getUChar("pwd_to", passwordTimeoutIndex);
  if (passwordTimeoutIndex >= 5) passwordTimeoutIndex = 0;

  telegramEnabled = prefs.getBool("tg_en", telegramEnabled);

  alertBeepCountIndex = prefs.getUChar("al_cnt", alertBeepCountIndex);
  if (alertBeepCountIndex >= ALERT_COUNT_OPTION_COUNT) alertBeepCountIndex = 0;

  alertBeepDurationIndex = prefs.getUChar("al_dur", alertBeepDurationIndex);
  if (alertBeepDurationIndex >= ALERT_DURATION_OPTION_COUNT) alertBeepDurationIndex = 1;

  guardEnabled = prefs.getBool("guard_en", guardEnabled);
  guardDelayIndex = prefs.getUChar("guard_dl", guardDelayIndex);
  if (guardDelayIndex >= GUARD_DELAY_COUNT) guardDelayIndex = 0;

  tetrisHighScore = prefs.getUShort("tet_hi", tetrisHighScore);

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

  storedWifiSsid[0] = ' ';
  storedWifiPass[0] = ' ';

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

void saveSettings() {
  prefs.begin("monitor", false);

  prefs.putBool("snd_en", soundSensorEnabled);
  prefs.putBool("reed_en", reedSensorEnabled);
  prefs.putBool("water_en", waterSensorEnabled);
  prefs.putBool("dht_en", dhtSensorEnabled);
  prefs.putBool("rfid_en", rfidEnabled);
  prefs.putBool("buzz_en", buzzerEnabled);

  prefs.putBool("pwd_en", passwordEnabled);
  prefs.putUChar("pin_len", pinLength);
  prefs.putString("pin", savedPin);
  prefs.putUChar("pwd_to", passwordTimeoutIndex);
  prefs.putBool("tg_en", telegramEnabled);
  prefs.putUChar("al_cnt", alertBeepCountIndex);
  prefs.putUChar("al_dur", alertBeepDurationIndex);
  prefs.putBool("guard_en", guardEnabled);
  prefs.putUChar("guard_dl", guardDelayIndex);
  prefs.putUShort("tet_hi", tetrisHighScore);

  prefs.end();
}

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
  pinMode(REED_AO_PIN, INPUT);
  pinMode(DHT_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // На общей SPI-шине оба CS должны быть в HIGH до инициализации устройств.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(RFID_SS_PIN, OUTPUT);
  digitalWrite(RFID_SS_PIN, HIGH);
  pinMode(RFID_RST_PIN, OUTPUT);
  digitalWrite(RFID_RST_PIN, HIGH);

  dht.begin();
  analogReadResolution(12);

  SPI.begin(TFT_SCK, RFID_MISO_PIN, TFT_MOSI, TFT_CS);
  realTft.init(TFT_W, TFT_H);
  realTft.setSPISpeed(40000000);
  realTft.setRotation(DISPLAY_ROTATION);
  realTft.setTextWrap(false);
  realTft.invertDisplay(false);
  realTft.fillScreen(COLOR_BG);

  tft.begin(&realTft);
  tft.setTextWrap(false);
  tft.fillScreen(COLOR_BG);

  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM: ");
  Serial.println(ESP.getFreePsram());

  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_SS_PIN, HIGH);
  rfid.PCD_Init();
  rfid.PCD_AntennaOn();

  rfidVersionReg = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rfidReady = !(rfidVersionReg == 0x00 || rfidVersionReg == 0xFF);

  Serial.print("RFID RC522 version: 0x");
  Serial.println(rfidVersionReg, HEX);

  drawBootScreen();
  flushFrame();

  initClockFromCompileTime();
  loadSettings();

  lastSoundRawActive = digitalRead(SOUND_DO_PIN) == SOUND_ACTIVE_LEVEL;
  lastReedRawActive = digitalRead(REED_DO_PIN) == REED_ACTIVE_LEVEL;
  lastWaterWet = analogRead(WATER_PIN) >= WATER_THRESHOLD;

  addLog("Система запущена");
  addNotification("Система готова");

  if (rfidEnabled) {
    if (rfidReady) {
      addLog("RFID готов");
      addNotification("RFID готов");
    } else {
      addLog("RFID: нет связи");
      addNotification("RFID: нет связи");
    }
  }

  drawMainScreen();
  flushFrame();
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
  if (telegramQueue == nullptr) {
    telegramQueue = xQueueCreate(TELEGRAM_QUEUE_CAPACITY, sizeof(TelegramQueueItem));
  }

  if (telegramQueue != nullptr && telegramTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(
      telegramWorkerTask,
      "TelegramTask",
      8192,
      nullptr,
      1,
      &telegramTaskHandle,
      0);
  }

  Serial.println("ESP32-S3 monitor started");
  Serial.print("Display: ");
  Serial.print(tft.width());
  Serial.print(" x ");
  Serial.println(tft.height());
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
  handleRfid();
  handleBuzzer();
  handleWiFiSetupPortal();
  readDht();
  updateClock();
  updateWiFi();
  updateGuard();
  // Telegram commands are polled in TelegramTask, not in the main loop.
  updateMenuTimeout();
  updateUi();
}

void flushFrame() {
  if (tft.isBuffered()) {
    // Перед отправкой кадра гарантированно снимаем RFID с общей SPI-шины.
    digitalWrite(RFID_SS_PIN, HIGH);
    digitalWrite(TFT_CS, HIGH);
    realTft.drawRGBBitmap(0, 0, tft.getBuffer(), SCREEN_W, SCREEN_H);
  }
}

// ================= INPUT =================
void handleButtons() {
  bool nextState = digitalRead(BTN_NEXT);
  bool backState = digitalRead(BTN_BACK);
  unsigned long now = millis();

  // Аварийный сброс PIN: на экране PIN держим NEXT + BACK 10 секунд.
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
      lastNextButtonTime = now;
      lastBackButtonTime = now;
      return;
    }
  } else {
    pinResetComboStartedAt = 0;
    pinResetComboHandled = false;
  }

  // Долгое удержание NEXT.
  if (nextState == LOW && nextDownAt > 0 && !nextLongHandled) {
    unsigned long holdTime = NOTIFICATION_CLEAR_HOLD;
    if (screen == SCREEN_TETRIS) holdTime = 700;

    if (now - nextDownAt >= holdTime) {
      if (screen == SCREEN_MAIN) {
        clearNotifications();
      } else if (screen == SCREEN_TETRIS) {
        tetrisHardDrop();
      }

      nextLongHandled = true;
      menuDirty = true;
    }
  }

  // Долгое удержание BACK.
  if (backState == LOW && backDownAt > 0 && !backLongHandled) {
    if (now - backDownAt >= NOTIFICATION_CLEAR_HOLD) {
      if (screen == SCREEN_MAIN) {
        clearNotifications();
      }

      backLongHandled = true;
    }
  }

  // NEXT debounce отдельно от BACK.
  if (now - lastNextButtonTime > BUTTON_DEBOUNCE && nextState != lastNextState) {
    if (nextState == LOW) {
      nextDownAt = now;
      nextLongHandled = false;
    } else {
      if (!nextLongHandled && nextDownAt > 0) handleShortButtonPress(true);
      nextDownAt = 0;
    }
    lastNextState = nextState;
    lastNextButtonTime = now;
  }

  // BACK debounce отдельно от NEXT.
  if (now - lastBackButtonTime > BUTTON_DEBOUNCE && backState != lastBackState) {
    if (backState == LOW) {
      backDownAt = now;
      backLongHandled = false;
    } else {
      if (!backLongHandled && backDownAt > 0) handleShortButtonPress(false);
      backDownAt = 0;
    }
    lastBackState = backState;
    lastBackButtonTime = now;
  }
}

void handleShortButtonPress(bool nextButton) {
  noteInput();

  // BACK сбрасывает активную тревогу с любого экрана.
  if (!nextButton && alarmActive) {
    resetAlarm();
    return;
  }

  if (screen == SCREEN_TETRIS) {
    if (nextButton) tetrisRotatePiece();
    else tetrisExit();
    return;
  }

  if (screen == SCREEN_MAIN) openMenu();
  else if (nextButton) menuSelect();
  else menuBack();
}

void handleEncoder() {
  unsigned long now = millis();

  // Читаем оба канала энкодера. При INPUT_PULLUP нормальное состояние часто 11.
  uint8_t clkState = digitalRead(ENC_CLK) == HIGH ? 1 : 0;
  uint8_t dtState = digitalRead(ENC_DT) == HIGH ? 1 : 0;
  uint8_t currentState = (clkState << 1) | dtState;

  if (currentState == lastEncoderState) return;

  // Небольшой фильтр дребезга/помех. Слишком большой debounce тут не нужен,
  // потому что ниже используется таблица валидных переходов.
  if (now - lastEncoderStableAt < ENCODER_DEBOUNCE) return;
  lastEncoderStableAt = now;

  // Таблица валидных переходов quadrature encoder.
  // Невалидные скачки, вызванные помехами от датчиков/сирены, дают 0.
  static const int8_t transitionTable[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
  };

  uint8_t transition = (lastEncoderState << 2) | currentState;
  int8_t movement = transitionTable[transition];
  lastEncoderState = currentState;

  if (movement == 0) return;

  encoderStepAccumulator += movement;

  // Один физический щелчок обычно даёт 4 валидных перехода.
  // Поэтому одиночный шум уже не сможет сдвинуть меню вправо.
  if (encoderStepAccumulator >= 4) {
    encoderStepAccumulator = 0;
    noteInput();
    if (screen == SCREEN_MAIN) openMenu();
    else menuMove(-1);
  } else if (encoderStepAccumulator <= -4) {
    encoderStepAccumulator = 0;
    noteInput();
    if (screen == SCREEN_MAIN) openMenu();
    else menuMove(1);
  }
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

    addLog("Щелчок / звук");
    addNotification("Сработал датчик звука");
    triggerAlarm("Звук");

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
    triggerAlarm("Геркон");

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
    triggerAlarm("Вода");

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

void formatRfidUid(MFRC522::Uid* uid, char* buffer, size_t size) {
  if (size == 0) return;
  buffer[0] = ' ';

  for (byte i = 0; i < uid->size; i++) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", uid->uidByte[i]);

    if (i > 0) {
      strncat(buffer, " ", size - strlen(buffer) - 1);
    }
    strncat(buffer, part, size - strlen(buffer) - 1);
  }
}

char normalizeHexChar(char c) {
  if (c >= 'a' && c <= 'f') return c - 32;
  return c;
}

bool rfidAccessConfigured() {
  return strlen(AUTHORIZED_RFID_UID) > 0;
}

bool rfidAuthorized(const char* uid) {
  if (!rfidAccessConfigured()) return false;

  // Сравниваем только HEX-символы, игнорируя пробелы, регистр и разделители.
  // Так "83 D1 9E 1E", "83d19e1e" и "83-d1-9e-1e" будут одинаковыми.
  uint8_t i = 0;
  uint8_t j = 0;

  while (uid[i] != ' ' || AUTHORIZED_RFID_UID[j] != ' ') {
    while (uid[i] == ' ' || uid[i] == ':' || uid[i] == '-') i++;
    while (AUTHORIZED_RFID_UID[j] == ' ' || AUTHORIZED_RFID_UID[j] == ':' || AUTHORIZED_RFID_UID[j] == '-') j++;

    char a = normalizeHexChar(uid[i]);
    char b = normalizeHexChar(AUTHORIZED_RFID_UID[j]);

    if (a != b) return false;
    if (a == ' ' && b == ' ') return true;

    i++;
    j++;
  }

  return true;
}

void handleRfid() {
  unsigned long now = millis();

  // RFID читаем не постоянно, а только когда это реально нужно:
  // 1) экран PIN/RFID открыт;
  // 2) охрана включена/ожидает/активна.
  bool rfidNeededNow = (screen == SCREEN_PASSWORD) || (screen == SCREEN_MAIN);

  if (!rfidNeededNow) {
    if (rfidDetected && now - lastRfidEvent > RFID_HOLD_TIME) {
      rfidDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    return;
  }

  if (now - lastRfidPoll < RFID_POLL_INTERVAL) return;
  lastRfidPoll = now;

  if (!rfidEnabled) {
    if (rfidDetected) {
      rfidDetected = false;
      sensorStripDirty = true;
      mainDirty = true;
    }
    return;
  }

  if (!rfidReady) return;

  if (rfidDetected && now - lastRfidEvent > RFID_HOLD_TIME) {
    rfidDetected = false;
    sensorStripDirty = true;
    mainDirty = true;
  }

  // RC522 и экран сидят на общей SPI-шине. Перед чтением RFID снимаем экран с CS.
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(RFID_SS_PIN, HIGH);

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  if (lastRfidEvent != 0 && now - lastRfidEvent < RFID_RETRIGGER_DELAY) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  lastRfidEvent = now;
  rfidDetected = true;
  rfidEventCount++;

  char uidText[24];
  formatRfidUid(&rfid.uid, uidText, sizeof(uidText));
  strncpy(lastRfidUid, uidText, sizeof(lastRfidUid) - 1);
  lastRfidUid[sizeof(lastRfidUid) - 1] = ' ';

  Serial.print("RFID UID: ");
  Serial.println(lastRfidUid);
  Serial.print("RFID authorized: ");
  Serial.println(rfidAuthorized(lastRfidUid) ? "YES" : "NO");
  Serial.print("RFID screen: ");
  Serial.println((int)screen);

  char logMsg[72];
  snprintf(logMsg, sizeof(logMsg), "RFID %s", lastRfidUid);
  addLog(logMsg);

  bool authorized = rfidAuthorized(lastRfidUid);

  if (authorized) {
    if (screen == SCREEN_PASSWORD) {
      lastUnlockTime = millis();
      resetPinInput();
      screen = SCREEN_MENU;
      rootIndex = 0;
      sensorIndex = 0;
      settingsIndex = 0;
      menuDirty = true;

      addLog("RFID карта OK");
      addLog("Вход по RFID");
      addNotification("RFID карта OK");
      sendTelegramMessage(String("RFID карта разрешена: ") + String(lastRfidUid));
    } else {
      addLog("RFID карта OK");
      addNotification("RFID карта OK");
      sendTelegramMessage(String("RFID карта разрешена: ") + String(lastRfidUid));
      toggleGuard();
    }
  } else {
    if (rfidAccessConfigured()) {
      addLog("RFID отказ");
      addNotification("RFID отказ");
      if (screen == SCREEN_PASSWORD) {
        showPinStatus("RFID отказ");
        startConfiguredBuzzer(false);
      }
      sendTelegramMessage(String("RFID отказ: ") + String(lastRfidUid));

      // Сирена от неправильной RFID-карты только при активной охране.
      // Если охрана выключена, показываем отказ, но не пищим.
      if (screen == SCREEN_MAIN && guardActive) {
        triggerAlarm("RFID");
      }
    } else {
      addNotification("RFID карта считана");
      sendTelegramMessage(String("RFID UID: ") + String(lastRfidUid));
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  sensorStripDirty = true;
  mainDirty = true;
  menuDirty = true;
}

void readDht() {
  if (!dhtSensorEnabled) return;

  unsigned long now = millis();
  if (now - lastDhtRead < DHT_INTERVAL) return;
  lastDhtRead = now;

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  for (uint8_t attempt = 0; attempt < 3 && (isnan(newHumidity) || isnan(newTemperature)); attempt++) {
    delay(120);
    newHumidity = dht.readHumidity();
    newTemperature = dht.readTemperature();
  }

  if (isnan(newHumidity) || isnan(newTemperature)) {
    if (dhtFailureCount < 255) dhtFailureCount++;
    Serial.print("DHT read failed, count = ");
    Serial.println(dhtFailureCount);
    if (dhtFailureCount == 5) addLog("DHT: нет сигнала");
    sensorStripDirty = true;
    mainDirty = true;
    return;
  }

  // Защита от мусорных значений DHT. Например -26 C для DHT11 — это не реальная
  // температура, а ошибка чтения/контакта/питания/типа датчика.
  if (newTemperature < -5.0f || newTemperature > 60.0f || newHumidity < 0.0f || newHumidity > 100.0f) {
    if (dhtFailureCount < 255) dhtFailureCount++;
    Serial.print("DHT invalid value: T=");
    Serial.print(newTemperature);
    Serial.print(" H=");
    Serial.println(newHumidity);
    if (dhtFailureCount == 5) addLog("DHT: неверные данные");
    sensorStripDirty = true;
    mainDirty = true;
    return;
  }

  dhtFailureCount = 0;

  bool changed = false;
  if (isnan(humidity) || fabs(newHumidity - humidity) >= 0.1f) changed = true;
  humidity = newHumidity;

  if (isnan(temperature) || fabs(newTemperature - temperature) >= 0.1f) changed = true;
  temperature = newTemperature;

  if (changed) {
    sensorStripDirty = true;
    mainDirty = true;
  }

  if (!isnan(temperature) && !isnan(humidity) && (lastTempNotify == 0 || now - lastTempNotify >= TEMP_NOTIFY_INTERVAL)) {
    char message[64];
    snprintf(message, sizeof(message), "T %.1fC  H %.0f%%", temperature, humidity);
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
  Serial.println("BUZZER ON");
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

    if (alertBeepsLeft > 0) {
      alertPauseActive = true;
      alertPauseStartedAt = now;
    }
  }

  if (alertPauseActive && now - alertPauseStartedAt >= 250) {
    alertPauseActive = false;
    if (alertBeepsLeft > 0) {
      alertBeepsLeft--;
      startBuzzer(alertBeepDurationValues[alertBeepDurationIndex]);
    }
  }
}

void startConfiguredBuzzer(bool continuousAllowed) {
  alertPauseActive = false;

  uint8_t count = alertBeepCountValues[alertBeepCountIndex];

  // "До выкл" имеет смысл только для полноценной тревоги.
  // Для обычного события вне охраны не запускаем бесконечную сирену.
  if (count == 255) {
    if (continuousAllowed) {
      alertBeepsLeft = 0;
      startBuzzer(0);
    } else {
      alertBeepsLeft = 0;
      startBuzzer(alertBeepDurationValues[alertBeepDurationIndex]);
    }
    return;
  }

  alertBeepsLeft = count > 0 ? count - 1 : 0;
  startBuzzer(alertBeepDurationValues[alertBeepDurationIndex]);
}

void startAlarmBuzzer() {
  startConfiguredBuzzer(true);
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
  alarmSource[sizeof(alarmSource) - 1] = ' ';

  char msg[48];
  snprintf(msg, sizeof(msg), "ТРЕВОГА: %s", source);
  addLog(msg);
  addNotification(msg);

  if (strcmp(source, "Звук") == 0) {
    sendTelegramMessage("ТРЕВОГА: Сработал датчик звука!");
  } else if (strcmp(source, "Геркон") == 0) {
    sendTelegramMessage("ТРЕВОГА: Сработал геркон!");
  } else if (strcmp(source, "Вода") == 0) {
    sendTelegramMessage("ТРЕВОГА: Сработал датчик воды!");
  } else if (strcmp(source, "RFID") == 0) {
    sendTelegramMessage("ТРЕВОГА: неизвестная RFID-карта!");
  } else {
    sendTelegramMessage(String("ТРЕВОГА: ") + String(source));
  }

  // Сирена должна запускаться для любого источника тревоги.
  startAlarmBuzzer();
  markAllDirty();
}

void resetAlarm() {
  if (!alarmActive && !buzzerActive) return;

  alarmActive = false;
  alarmSource[0] = ' ';
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
  char monthStr[4] = { 0 };

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
  if (wifiSetupMode) {
    stopWiFiSetupPortal();
  }

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
  if (wifiSetupMode) {
    addNotification("Setup Wi-Fi уже включен");
    menuDirty = true;
    mainDirty = true;
    return;
  }

  // Для режима настройки используем только AP.
  // AP_STA иногда даёт нестабильное подключение телефона к точке доступа.
  stopWiFiSetupPortal();
  WiFi.disconnect(true, false);
  delay(250);
  WiFi.mode(WIFI_OFF);
  delay(250);
  WiFi.mode(WIFI_AP);
  delay(250);

  IPAddress localIp(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(localIp, gateway, subnet);

  // Канал 6 обычно стабильнее и меньше конфликтует с автоканалом роутера.
  bool apStarted = WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_PASS, 6, 0, 4);
  IPAddress ip = WiFi.softAPIP();

  wifiSetupMode = apStarted;
  wifiPortalReconnectPending = false;

  wifiPortalServer.on("/", HTTP_GET, []() {
    String html = "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>SS-Q1 Wi-Fi Setup</title></head><body>";
    html += "<h2>SS-Q1 Wi-Fi Setup</h2>";
    html += "<form method='POST' action='/save'>";
    html += "<p>SSID:<br><input name='ssid' maxlength='32'></p>";
    html += "<p>Password:<br><input name='pass' type='password' maxlength='64'></p>";
    html += "<p><button type='submit'>Save</button></p>";
    html += "</form>";
    html += "<p>After saving, reconnect your phone to normal Wi-Fi.</p>";
    html += "</body></html>";
    wifiPortalServer.send(200, "text/html", html);
  });

  wifiPortalServer.on("/save", HTTP_POST, []() {
    String ssid = wifiPortalServer.arg("ssid");
    String pass = wifiPortalServer.arg("pass");

    ssid.trim();
    pass.trim();

    if (ssid.length() == 0) {
      wifiPortalServer.send(400, "text/plain", "SSID is empty");
      return;
    }

    ssid.substring(0, sizeof(storedWifiSsid) - 1).toCharArray(storedWifiSsid, sizeof(storedWifiSsid));
    pass.substring(0, sizeof(storedWifiPass) - 1).toCharArray(storedWifiPass, sizeof(storedWifiPass));

    saveSettings();

    wifiPortalSavedAt = millis();
    wifiPortalReconnectPending = true;

    wifiPortalServer.send(200, "text/html", "<html><body><h2>Saved</h2><p>SS-Q1 will connect to Wi-Fi.</p></body></html>");
  });

  wifiPortalServer.begin();

  if (apStarted) {
    addLog("Setup Wi-Fi включен");
    addNotification("Setup Wi-Fi включен");
    Serial.print("Setup Wi-Fi AP: ");
    Serial.println(WIFI_SETUP_AP_SSID);
    Serial.print("Setup Wi-Fi IP: ");
    Serial.println(ip);
  } else {
    addLog("Setup Wi-Fi ошибка");
    addNotification("Setup Wi-Fi ошибка");
    Serial.println("Setup Wi-Fi AP start failed");
  }

  menuDirty = true;
  mainDirty = true;
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
}

// ================= TELEGRAM =================
bool telegramConfigured() {
  return strlen(TELEGRAM_BOT_TOKEN) > 0 && strlen(TELEGRAM_CHAT_ID) > 0;
}

String urlEncode(const String& value) {
  String encoded = "";
  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
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
  if (!telegramEnabled || !telegramConfigured()) return false;
  if (telegramQueue == nullptr) return false;

  TelegramQueueItem item;
  memset(&item, 0, sizeof(item));
  text.substring(0, TELEGRAM_QUEUE_MESSAGE_SIZE - 1).toCharArray(item.text, sizeof(item.text));

  // Быстро кладём сообщение в очередь и не ждём HTTPS.
  // Если очередь забита, сообщение пропускается, но интерфейс не зависает.
  return xQueueSend(telegramQueue, &item, 0) == pdTRUE;
}

bool sendTelegramMessageNow(const char* text) {
  if (!telegramEnabled || !telegramConfigured()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT);

  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage";
  String body = "chat_id=" + urlEncode(String(TELEGRAM_CHAT_ID)) + "&text=" + urlEncode(String(text));

  if (!http.begin(client, url)) return false;

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  http.end();

  return code >= 200 && code < 300;
}

void telegramWorkerTask(void* parameter) {
  TelegramQueueItem item;
  unsigned long lastWorkerPollCheck = 0;

  while (true) {
    // 1) Отправка исходящих сообщений из очереди.
    // Используем короткое ожидание, чтобы задача не засыпала навсегда
    // и могла параллельно проверять входящие команды Telegram.
    if (telegramQueue != nullptr && xQueueReceive(telegramQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
      unsigned long now = millis();
      if (now - lastTelegramAttempt < TELEGRAM_SEND_MIN_INTERVAL) {
        vTaskDelay(pdMS_TO_TICKS(TELEGRAM_SEND_MIN_INTERVAL - (now - lastTelegramAttempt)));
      }

      lastTelegramAttempt = millis();
      sendTelegramMessageNow(item.text);
    }

    // 2) Приём входящих команд Telegram в фоне.
    // checkTelegramCommands() сам проверяет TELEGRAM_POLL_INTERVAL,
    // поэтому здесь можно вызывать его часто и недорого.
    unsigned long now = millis();
    if (now - lastWorkerPollCheck >= 250) {
      lastWorkerPollCheck = now;
      checkTelegramCommands();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sendTelegramStatus() {
  String text;
  text.reserve(260);

  text += "SS-Q1: статус";
  text += (char)10;

  text += "Охрана: ";
  text += guardActive ? "включена" : (guardPending ? "ожидание" : "выключена");
  text += (char)10;

  text += "Тревога: ";
  text += alarmActive ? alarmSource : "нет";
  text += (char)10;

  text += "Wi-Fi: ";
  text += (WiFi.status() == WL_CONNECTED) ? "онлайн" : "офлайн";
  text += (char)10;

  text += "DHT: ";
  text += (!dhtSensorEnabled) ? "выключен" : ((!isnan(temperature) && !isnan(humidity)) ? "OK" : "нет сигнала");
  text += (char)10;

  text += "Звук: ";
  text += (!soundSensorEnabled) ? "выключен" : (soundDetected ? "сработал" : "OK");
  text += (char)10;

  text += "Геркон: ";
  text += (!reedSensorEnabled) ? "выключен" : "OK";
  text += (char)10;

  text += "Вода: ";
  text += (!waterSensorEnabled) ? "выключен" : (waterDetected ? "обнаружена" : "сухо");
  text += (char)10;

  text += "RFID: ";
  text += (!rfidEnabled) ? "выключен" : (strlen(lastRfidUid) > 0 ? lastRfidUid : "готов");

  sendTelegramMessage(text);
}

void sendTelegramClimate() {
  String text;
  text.reserve(140);

  text += "SS-Q1: климат";
  text += (char)10;

  if (!dhtSensorEnabled) {
    text += "DHT: датчик выключен";
  } else if (isnan(temperature) || isnan(humidity)) {
    text += "DHT: нет сигнала";
  } else {
    text += "Температура: ";
    text += String(temperature, 1);
    text += " C";
    text += (char)10;
    text += "Влажность: ";
    text += String(humidity, 1);
    text += " %";
  }

  sendTelegramMessage(text);
}

void handleTelegramCommand(const String& command) {
  Serial.print("Telegram command: ");
  Serial.println(command);

  if (command == "/status") {
    sendTelegramStatus();
    return;
  }

  if (command == "/temp" || command == "/climate") {
    sendTelegramClimate();
    return;
  }

  if (command == "/guard_on") {
    if (!guardEnabled) {
      toggleGuard();
    } else {
      sendTelegramMessage("Охрана уже включена");
    }
    return;
  }

  if (command == "/guard_off") {
    if (guardEnabled) {
      toggleGuard();
    } else {
      sendTelegramMessage("Охрана уже выключена");
    }
    return;
  }

  if (command == "/alarm_reset") {
    resetAlarm();
    sendTelegramMessage("Тревога сброшена");
    return;
  }

  if (command == "/help" || command == "/start") {
    String text;
    text.reserve(220);
    text += "SS-Q1: команды";
    text += (char)10;
    text += "/status - статус системы";
    text += (char)10;
    text += "/temp - температура и влажность";
    text += (char)10;
    text += "/guard_on - включить охрану";
    text += (char)10;
    text += "/guard_off - выключить охрану";
    text += (char)10;
    text += "/alarm_reset - сбросить тревогу";
    text += (char)10;
    text += "/help - команды";
    sendTelegramMessage(text);
    return;
  }

  sendTelegramMessage("SS-Q1: неизвестная команда. Напишите /help");
}

void checkTelegramCommands() {
  if (!telegramEnabled || !telegramConfigured()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  // Telegram getUpdates через HTTPS блокирует ESP32.
  // Поэтому команды проверяем только на главном экране,
  // но во время тревоги тоже разрешаем опрос, чтобы работал /alarm_reset.
  if (screen != SCREEN_MAIN) return;

  unsigned long now = millis();
  if (now - lastTelegramPoll < TELEGRAM_POLL_INTERVAL) return;
  lastTelegramPoll = now;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT);

  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/getUpdates?timeout=0&limit=5";
  if (lastTelegramUpdateId > 0) {
    url += "&offset=" + String(lastTelegramUpdateId + 1);
  }

  if (!http.begin(client, url)) {
    Serial.println("Telegram getUpdates begin failed");
    return;
  }

  int code = http.GET();
  if (code < 200 || code >= 300) {
    Serial.print("Telegram getUpdates HTTP error: ");
    Serial.println(code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  Serial.print("Telegram payload length: ");
  Serial.println(payload.length());

  int searchFrom = 0;
  bool handledAnyCommand = false;

  while (true) {
    int updatePos = payload.indexOf("update_id", searchFrom);
    if (updatePos < 0) break;

    int colonPos = payload.indexOf(':', updatePos);
    int commaPos = payload.indexOf(',', colonPos);
    if (colonPos < 0 || commaPos < 0) break;

    long updateId = payload.substring(colonPos + 1, commaPos).toInt();
    if (updateId > lastTelegramUpdateId) {
      lastTelegramUpdateId = updateId;
    }

    int nextUpdatePos = payload.indexOf("update_id", commaPos);
    int blockEnd = nextUpdatePos > 0 ? nextUpdatePos : payload.length();

    int textKeyPos = payload.indexOf("text", commaPos);
    if (textKeyPos > 0 && textKeyPos < blockEnd) {
      int textColon = payload.indexOf(':', textKeyPos);
      int textStartQuote = payload.indexOf('"', textColon + 1);
      int textEndQuote = payload.indexOf('"', textStartQuote + 1);

      if (textColon > 0 && textStartQuote > 0 && textEndQuote > textStartQuote && textEndQuote < blockEnd) {
        String command = payload.substring(textStartQuote + 1, textEndQuote);
        command.trim();

        int spaceIndex = command.indexOf(' ');
        if (spaceIndex > 0) command = command.substring(0, spaceIndex);

        int atIndex = command.indexOf('@');
        if (atIndex > 0) command = command.substring(0, atIndex);

        Serial.print("Telegram command received: ");
        Serial.println(command);

        if (command.startsWith("/")) {
          handledAnyCommand = true;
          handleTelegramCommand(command);
        }
      }
    }

    if (nextUpdatePos < 0) break;
    searchFrom = nextUpdatePos;
  }

  if (handledAnyCommand) {
    menuDirty = true;
    markMainDirty(false);
  }
}

// ================= LOGS =================
void addLog(const char* text) {
  if (logCount >= LOG_CAPACITY) {
    memmove(&logEntries[0], &logEntries[1], sizeof(LogEntry) * (LOG_CAPACITY - 1));
    logCount = LOG_CAPACITY - 1;
  }

  formatTime(logEntries[logCount].time, sizeof(logEntries[logCount].time), true);
  strncpy(logEntries[logCount].text, text, sizeof(logEntries[logCount].text) - 1);
  logEntries[logCount].text[sizeof(logEntries[logCount].text) - 1] = ' ';
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
  notifications[notifyCount].text[sizeof(notifications[notifyCount].text) - 1] = ' ';
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
void setDefaultPinForCurrentLength() {
  memset(savedPin, 0, sizeof(savedPin));

  if (pinLength == 6) {
    strcpy(savedPin, "123456");
  } else {
    pinLength = 4;
    strcpy(savedPin, "1234");
  }

  pendingPinLength = pinLength;
  cleanPinTail(savedPin, pinLength);
}

bool isValidPinString(const char* pin, uint8_t len) {
  if (len != 4 && len != 6) return false;

  for (uint8_t i = 0; i < len; i++) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }

  return true;
}

void cleanPinTail(char* pin, uint8_t len) {
  if (len > 6) len = 6;
  for (uint8_t i = len; i < 7; i++) {
    pin[i] = 0;
  }
}

bool pinsEqual(const char* a, const char* b, uint8_t len) {
  if (!isValidPinString(a, len)) return false;
  if (!isValidPinString(b, len)) return false;

  for (uint8_t i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }

  return true;
}

void normalizePinState() {
  if (pinLength != 4 && pinLength != 6) {
    pinLength = 4;
  }

  cleanPinTail(savedPin, pinLength);

  if (!isValidPinString(savedPin, pinLength)) {
    setDefaultPinForCurrentLength();
  } else {
    pendingPinLength = pinLength;
  }

  pinChangeRequiresNewPin = false;
}

bool passwordRequired() {
  if (!passwordEnabled) return false;

  unsigned long timeout = passwordTimeoutValues[passwordTimeoutIndex];
  if (timeout == 0) return true;

  return millis() - lastUnlockTime > timeout;
}

void resetPinInput() {
  pinCursor = 0;
  pinInputLength = 0;
  memset(enteredPin, 0, sizeof(enteredPin));
}

void showPinStatus(const char* text) {
  strncpy(pinStatusText, text, sizeof(pinStatusText) - 1);
  pinStatusText[sizeof(pinStatusText) - 1] = ' ';
  pinStatusUntil = millis() + 2500;
  menuDirty = true;
}

void emergencyResetPinAndOpenMenu() {
  if (pinLength == 6 || pendingPinLength == 6) {
    pinLength = 6;
  } else {
    pinLength = 4;
  }

  pendingPinLength = pinLength;
  setDefaultPinForCurrentLength();
  pinChangeRequiresNewPin = false;
  resetPinInput();

  lastUnlockTime = millis();
  screen = SCREEN_MENU;
  rootIndex = 0;
  sensorIndex = 0;
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
    enteredPin[pinInputLength] = 0;
    menuDirty = true;
    return;
  }

  closeMenu();
}

void deleteNewPinDigitOrExit() {
  if (pinInputLength > 0) {
    pinInputLength--;
    enteredPin[pinInputLength] = 0;
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
  if (screen == SCREEN_WIFI_SETUP_INFO) {
    screen = SCREEN_NETWORK;
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_PASSWORD) {
    deletePinDigitOrExit();
    return;
  }

  if (screen == SCREEN_SET_PIN) {
    deleteNewPinDigitOrExit();
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
  if (pinLength != 4 && pinLength != 6) {
    pinLength = 4;
    setDefaultPinForCurrentLength();
    saveSettings();
  }

  if (pinInputLength >= pinLength) return;

  enteredPin[pinInputLength] = (char)('0' + pinCursor);
  pinInputLength++;
  cleanPinTail(enteredPin, pinInputLength);

  if (pinInputLength >= pinLength) {
    cleanPinTail(enteredPin, pinLength);
    cleanPinTail(savedPin, pinLength);

    Serial.print("PIN check entered=[");
    for (uint8_t i = 0; i < pinLength; i++) Serial.print(enteredPin[i]);
    Serial.print("] saved=[");
    for (uint8_t i = 0; i < pinLength; i++) Serial.print(savedPin[i]);
    Serial.print("] len=");
    Serial.println(pinLength);

    if (pinsEqual(enteredPin, savedPin, pinLength)) {
      lastUnlockTime = millis();
      pendingPinLength = pinLength;
      pinChangeRequiresNewPin = false;
      resetPinInput();

      screen = SCREEN_MENU;
      rootIndex = 0;
      sensorIndex = 0;
      settingsIndex = 0;
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
  uint8_t targetPinLength = pinChangeRequiresNewPin ? pendingPinLength : pinLength;
  if (targetPinLength != 4 && targetPinLength != 6) targetPinLength = 4;
  if (pinInputLength >= targetPinLength) return;

  enteredPin[pinInputLength] = (char)('0' + pinCursor);
  pinInputLength++;
  cleanPinTail(enteredPin, pinInputLength);

  if (pinInputLength >= targetPinLength) {
    cleanPinTail(enteredPin, targetPinLength);

    memset(savedPin, 0, sizeof(savedPin));
    memcpy(savedPin, enteredPin, targetPinLength);
    cleanPinTail(savedPin, targetPinLength);

    pinLength = targetPinLength;
    pendingPinLength = pinLength;
    pinChangeRequiresNewPin = false;

    Serial.print("NEW PIN saved=[");
    for (uint8_t i = 0; i < pinLength; i++) Serial.print(savedPin[i]);
    Serial.print("] len=");
    Serial.println(pinLength);

    saveSettings();
    resetPinInput();

    addLog("PIN изменён");
    addNotification("PIN изменён");
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
    if (&dhtSensorEnabled == enabled && dhtSensorEnabled) lastDhtRead = 0;
    if (&reedSensorEnabled == enabled && !reedSensorEnabled) reedDetected = false;
    if (&waterSensorEnabled == enabled && !waterSensorEnabled) waterDetected = false;
    if (&rfidEnabled == enabled && !rfidEnabled) rfidDetected = false;

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
      stopWiFiSetupPortal();
      startWiFi(true);
      addNotification("Wi-Fi подключение");
    } else if (networkIndex == 1) {
      startWiFiSetupPortal();
      screen = SCREEN_WIFI_SETUP_INFO;
    } else if (networkIndex == 2) {
      clearWiFiCredentials();
      stopWiFiSetupPortal();
      startWiFiSetupPortal();
      screen = SCREEN_WIFI_SETUP_INFO;
    }
    menuDirty = true;
    return;
  }

  if (screen == SCREEN_SYSTEM) {
    if (systemIndex == 0) screen = SCREEN_STATUS;
    else if (systemIndex == 1) screen = SCREEN_CHARACTERISTICS;
    else if (systemIndex == 2) screen = SCREEN_DESCRIPTION;
    else if (systemIndex == 3) {
      screen = SCREEN_TETRIS;
      initTetrisGame();
    }
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
      telegramEnabled = !telegramEnabled;
      saveSettings();
      addLog(telegramEnabled ? "Telegram включён" : "Telegram выключен");
    } else if (telegramSettingsIndex == 1) {
      sendTelegramMessage("Тестовое сообщение от SS-Q1");
      addLog("Telegram test отправлен");
    } else if (telegramSettingsIndex == 2) {
      sendTelegramStatus();
    } else if (telegramSettingsIndex == 3) {
      screen = SCREEN_TELEGRAM_COMMANDS;
    }
    menuDirty = true;
    return;
  }
}

void menuMove(int8_t direction) {
  if (screen == SCREEN_TETRIS) {
    tetrisMove(direction > 0 ? 1 : -1);
  } else if (screen == SCREEN_PASSWORD || screen == SCREEN_SET_PIN) {
    pinCursor = direction > 0 ? (pinCursor + 1) % 10 : (pinCursor + 9) % 10;
  } else if (screen == SCREEN_MENU) rootIndex = direction > 0 ? (rootIndex + 1) % ROOT_COUNT : (rootIndex + ROOT_COUNT - 1) % ROOT_COUNT;
  else if (screen == SCREEN_SENSORS) sensorIndex = direction > 0 ? (sensorIndex + 1) % SENSOR_COUNT : (sensorIndex + SENSOR_COUNT - 1) % SENSOR_COUNT;
  else if (screen == SCREEN_NETWORK) networkIndex = direction > 0 ? (networkIndex + 1) % 3 : (networkIndex + 2) % 3;
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
  // Красные события: тревога, отказ доступа и срабатывания датчиков.
  if (strstr(text, "ТРЕВОГА") != NULL || strstr(text, "Сработал") != NULL || strstr(text, "Обнаружена вода") != NULL || strstr(text, "Неверный PIN") != NULL || strstr(text, "RFID отказ") != NULL || strstr(text, "отказ") != NULL) {
    return COLOR_OFF;
  }

  // Зелёные события: успешный вход/готовность/подключение.
  if (strstr(text, "Wi-Fi подключен") != NULL || strstr(text, "Система") != NULL || strstr(text, "Охрана включена") != NULL || strstr(text, "Охрана выключена") != NULL || strstr(text, "PIN изменён") != NULL || strstr(text, "Вход") != NULL || strstr(text, "RFID готов") != NULL || strstr(text, "RFID доступ OK") != NULL) {
    return COLOR_ON;
  }

  // Синие/акцентные события: данные датчиков и считанная RFID-карта.
  if (strstr(text, "T ") != NULL || strstr(text, "DHT") != NULL || strstr(text, "RFID ") != NULL) {
    return COLOR_ACCENT;
  }

  // Предупреждения.
  if (strstr(text, "нет") != NULL || strstr(text, "ожид") != NULL || strstr(text, "ошибка") != NULL || strstr(text, "выключ") != NULL) {
    return COLOR_WARN;
  }

  return COLOR_MUTED;
}

void drawPageTitle(const char* title) {
  tft.fillScreen(COLOR_BG);

  // Общая округлая рамка вокруг страницы меню.
  tft.drawRoundRect(5, 5, SCREEN_W - 10, SCREEN_H - 10, 12, COLOR_LINE);

  // Округлая рамка заголовка.
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
    tft.drawFastVLine(x + 28, y - 16, 21, COLOR_MUTED);
  } else {
    tft.drawCircle(x + 14, y - 6, 3, COLOR_MUTED);
  }

  drawUtf8(u8g2_font_9x15_t_cyrillic, x + 40, y, selected ? COLOR_TEXT : COLOR_MUTED, label);
}

void drawToggle(int16_t x, int16_t y, bool enabled) {
  uint16_t borderColor = enabled ? COLOR_TEXT : COLOR_MUTED;
  uint16_t dotColor = enabled ? COLOR_TEXT : COLOR_OFF;

  // Компактный переключатель без подписи ON/OFF, чтобы текст не залезал на рамки строк.
  tft.fillRoundRect(x, y - 19, 56, 23, 8, COLOR_BG);
  tft.drawRoundRect(x, y - 19, 56, 23, 8, borderColor);
  tft.fillCircle(enabled ? x + 43 : x + 13, y - 8, 6, dotColor);
}

void drawStatusLetter(const char* letter, int16_t x, int16_t y, uint16_t statusColor) {
  // Одинаковый компактный блок: буква + кружок состояния.
  // Кружок ближе к букве и не упирается в правый край рамки.
  static const int16_t boxW = 38;
  static const int16_t boxH = 22;
  static const int16_t r = 6;

  tft.drawRoundRect(x, y - 12, boxW, boxH, r, COLOR_LINE);

  drawUtf8(u8g2_font_9x15_t_cyrillic, x + 8, y + 5, COLOR_TEXT, letter);

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

  const char* labels[] = {
    "DISPLAY",
    "DHT",
    "SOUND",
    "REED",
    "WATER",
    "WIFI",
    "READY"
  };

  const char* statuses[] = {
    "OK",
    "INIT",
    "OK",
    "OK",
    "OK",
    wifiConfigured() ? "WAIT" : "OFF",
    "OK"
  };

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

  // Верхняя панель с обводкой — возвращаем, так выглядит аккуратнее.
  tft.drawRoundRect(5, 5, SCREEN_W - 10, 24, 8, COLOR_LINE);

  // Wi-Fi индикатор внутри панели: без отдельных рамок вокруг полосок.
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

  // Без обводок. Активные полоски — белые, неактивные — приглушённые серые.
  // Это имитирует полупрозрачность на ST7789, где настоящего alpha-blending нет.
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

  // Карточка часов без точек около часов. Шрифт часов не меняем.
  tft.drawRoundRect(10, HEADER_H + 8, SCREEN_W - 20, 94, 14, COLOR_LINE);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(6);
  tft.setCursor(30, 62);
  tft.print(timeText);

  drawUtf8Centered(u8g2_font_7x13_t_cyrillic, 126, COLOR_MUTED, TZ_LABEL);
}

void drawSensorStrip() {
  char dhtText[32];

  if (!dhtSensorEnabled || isnan(temperature) || isnan(humidity)) snprintf(dhtText, sizeof(dhtText), "--.- C   -- %%");
  else snprintf(dhtText, sizeof(dhtText), "%.1f C   %.0f %%", temperature, humidity);

  // Карточка датчиков как была до GAS-полосы.
  tft.fillRect(0, 138, SCREEN_W, NOTIFY_Y - 138, COLOR_BG);

  tft.drawRoundRect(10, 142, SCREEN_W - 20, 64, 12, COLOR_LINE);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(42, 151);
  tft.print(dhtText);

  tft.drawFastHLine(20, 174, SCREEN_W - 40, COLOR_LINE);

  uint16_t soundColor = !soundSensorEnabled ? COLOR_WARN : (soundDetected ? COLOR_OFF : COLOR_ON);
  uint16_t reedColor = !reedSensorEnabled ? COLOR_WARN : (reedDetected ? COLOR_OFF : COLOR_ON);
  uint16_t waterColor = !waterSensorEnabled ? COLOR_WARN : (waterDetected ? COLOR_OFF : COLOR_ON);
  uint16_t buzzerColor = !buzzerEnabled ? COLOR_WARN : (buzzerActive ? COLOR_OFF : COLOR_ON);

  // На главном экране оставляем только основные индикаторы:
  // З - звук, Г - геркон, В - вода, С - сирена.
  drawStatusLetter("З", 22, 190, soundColor);
  drawStatusLetter("Г", 73, 190, reedColor);
  drawStatusLetter("В", 124, 190, waterColor);
  drawStatusLetter("С", 175, 190, buzzerColor);
}

void drawNotifications() {
  tft.fillRect(0, NOTIFY_Y, SCREEN_W, SCREEN_H - NOTIFY_Y, COLOR_BG);

  // Notification card.
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
  // Обводка тревоги отключена.
  // Функция оставлена, потому что она вызывается из drawMainScreen() и updateUi().
}

void drawMainScreen() {
  tft.fillScreen(COLOR_BG);

  // Outer rounded frame for the whole main screen.
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

  for (uint8_t i = 0; i < ROOT_COUNT; i++) {
    drawMenuRow(i, rootItems[i], i == rootIndex);
  }
}

void drawSensorsMenu() {
  drawPageTitle("Датчики");

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    int16_t y = 64 + i * 40;
    bool selected = i == sensorIndex;
    int16_t x = 14;
    int16_t w = SCREEN_W - 28;
    int16_t h = 30;

    tft.fillRoundRect(x, y - 21, w, h, 8, selected ? COLOR_ROW : COLOR_BG);
    tft.drawRoundRect(x, y - 21, w, h, 8, selected ? COLOR_TEXT : COLOR_LINE);

    if (selected) {
      tft.fillCircle(x + 14, y - 6, 3, COLOR_TEXT);
      tft.drawFastVLine(x + 28, y - 18, 24, COLOR_MUTED);
    } else {
      tft.drawCircle(x + 14, y - 6, 3, COLOR_MUTED);
    }

    drawUtf8(u8g2_font_9x15_t_cyrillic, x + 40, y, selected ? COLOR_TEXT : COLOR_MUTED, sensorItems[i].name);
    drawToggle(SCREEN_W - 74, y + 1, *sensorItems[i].enabled);
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
  tft.fillScreen(COLOR_BG);
  drawPageTitle("Сеть");

  drawMenuRow(0, "Подключиться", networkIndex == 0);
  drawMenuRow(1, "Setup Wi-Fi", networkIndex == 1);
  drawMenuRow(2, "Сброс Wi-Fi", networkIndex == 2);

  const char* stateText;
  uint16_t stateColor;

  if (wifiSetupMode) {
    stateText = "Setup Wi-Fi активен";
    stateColor = COLOR_ACCENT;
  } else if (WiFi.status() == WL_CONNECTED) {
    stateText = "Wi-Fi подключен";
    stateColor = COLOR_ON;
  } else {
    stateText = "Wi-Fi не подключен";
    stateColor = COLOR_WARN;
  }

  drawUtf8Centered(u8g2_font_6x12_t_cyrillic, 286, stateColor, stateText);
}

void drawWifiSetupInfoMenu() {
  tft.fillScreen(COLOR_BG);
  drawPageTitle("Setup Wi-Fi");

  tft.drawRoundRect(14, 58, SCREEN_W - 28, 218, 12, COLOR_LINE);

  drawUtf8Centered(u8g2_font_9x15_t_cyrillic, 84, wifiSetupMode ? COLOR_ON : COLOR_WARN,
                   wifiSetupMode ? "Setup Wi-Fi включен" : "Setup Wi-Fi выключен");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 112, COLOR_TEXT, "1. Подключитесь к сети:");
  drawUtf8(u8g2_font_9x15_t_cyrillic, 28, 134, COLOR_ACCENT, WIFI_SETUP_AP_SSID);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 162, COLOR_TEXT, "2. Пароль:");
  drawUtf8(u8g2_font_9x15_t_cyrillic, 28, 184, COLOR_ACCENT, WIFI_SETUP_AP_PASS);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 212, COLOR_TEXT, "3. Откройте в браузере:");
  drawUtf8(u8g2_font_9x15_t_cyrillic, 28, 234, COLOR_ACCENT, "192.168.4.1");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 262, COLOR_MUTED, "BACK - назад в сеть");
}

void drawSettingsMenu() {
  drawPageTitle("Настройки");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 54, 10, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 78, COLOR_TEXT, "Разделы настроек");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 96, COLOR_MUTED, "PIN, охрана, сирена.");

  const char* items[4] = { "PIN", "Охрана", "Оповещение", "Telegram" };

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

  const char* rowLabels[4] = { "Пароль", "PIN", "Длина", "Запрос" };
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

  const char* rowLabels[2] = { "Писки", "Длина" };
  const char* rowValues[2] = {
    alertBeepCountLabels[alertBeepCountIndex],
    alertBeepDurationLabels[alertBeepDurationIndex]
  };

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
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 74, COLOR_TEXT, "Уведомления в бот.");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 92, telegramConfigured() ? COLOR_ON : COLOR_WARN,
           telegramConfigured() ? "Token/chat_id OK" : "Впиши token/chat_id");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 110, WiFi.status() == WL_CONNECTED ? COLOR_ON : COLOR_WARN,
           WiFi.status() == WL_CONNECTED ? "Wi-Fi online" : "Wi-Fi offline");

  const char* rowLabels[4] = { "Отправка", "Тест", "Статус", "Команды" };
  const char* rowValues[4] = {
    telegramEnabled ? "ON" : "OFF",
    "отправить",
    "отправить",
    "открыть"
  };

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

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 224, COLOR_TEXT, "/temp");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 224, COLOR_MUTED, "темп/влажн");

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 262, COLOR_TEXT, "/help");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 104, 262, COLOR_MUTED, "список команд");
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
  for (uint8_t i = 0; i < targetPinLength; i++) {
    pinMask[i] = i < pinInputLength ? '*' : '_';
  }
  pinMask[targetPinLength] = ' ';
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
  drawUtf8Centered(u8g2_font_9x15_t_cyrillic, 29, COLOR_TEXT, "PIN / RFID");

  tft.drawRoundRect(28, 54, SCREEN_W - 56, 42, 10, COLOR_LINE);

  char pinMask[7];
  memset(pinMask, 0, sizeof(pinMask));
  for (uint8_t i = 0; i < pinLength; i++) {
    pinMask[i] = i < pinInputLength ? '*' : '_';
  }
  pinMask[pinLength] = ' ';
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

  if (pinStatusText[0] != ' ' && millis() < pinStatusUntil) {
    drawUtf8Centered(u8g2_font_6x12_t_cyrillic, 296, COLOR_OFF, pinStatusText);
  } else {
    drawUtf8(u8g2_font_6x12_t_cyrillic, 18, 296, COLOR_MUTED, "PIN или RFID, Назад - удалить");
  }
}

void drawSystemMenu() {
  drawPageTitle("Система");

  for (uint8_t i = 0; i < SYSTEM_COUNT; i++) {
    drawMenuRow(i, systemItems[i], i == systemIndex);
  }
}

void drawTetrisPanel(int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
  tft.drawRoundRect(x, y, w, h, 8, COLOR_LINE);
  drawUtf8(u8g2_font_6x12_t_cyrillic, x + 8, y + 14, COLOR_MUTED, title);
  tft.drawFastHLine(x + 6, y + 20, w - 12, COLOR_LINE);
}

void drawTetrisCell(int16_t x, int16_t y, int16_t size, uint16_t color) {
  // Без белой обводки: так фигуры выглядят спокойнее и не рябят на маленьком экране.
  tft.fillRoundRect(x, y, size - 1, size - 1, 2, color);
}

void drawTetrisMiniPiece(int16_t x, int16_t y, uint8_t piece, uint8_t cellSize) {
  uint16_t shape = TETRIS_SHAPES[piece][0];

  int8_t minR = 4;
  int8_t maxR = -1;
  int8_t minC = 4;
  int8_t maxC = -1;

  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (shape & (0x8000 >> (r * 4 + c))) {
        if ((int8_t)r < minR) minR = r;
        if ((int8_t)r > maxR) maxR = r;
        if ((int8_t)c < minC) minC = c;
        if ((int8_t)c > maxC) maxC = c;
      }
    }
  }

  if (maxR < 0 || maxC < 0) return;

  int16_t pieceW = (maxC - minC + 1) * cellSize;
  int16_t pieceH = (maxR - minR + 1) * cellSize;

  // Центрируем фигуру внутри области 44x44.
  int16_t offsetX = x + (44 - pieceW) / 2 - minC * cellSize;
  int16_t offsetY = y + (44 - pieceH) / 2 - minR * cellSize;

  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (shape & (0x8000 >> (r * 4 + c))) {
        drawTetrisCell(offsetX + c * cellSize, offsetY + r * cellSize, cellSize, COLOR_ACCENT);
      }
    }
  }
}

void drawTetrisScreen() {
  drawPageTitle("Тетрис");

  // ================= LEFT: GAME FIELD =================
  const int16_t fieldPanelX = 12;
  const int16_t fieldPanelY = 52;
  const int16_t fieldPanelW = 122;
  const int16_t fieldPanelH = 238;

  drawTetrisPanel(fieldPanelX, fieldPanelY, fieldPanelW, fieldPanelH, "Поле");

  const int16_t cell = 10;
  const int16_t fieldX = fieldPanelX + 10;
  const int16_t fieldY = fieldPanelY + 30;
  const int16_t fieldW = TETRIS_W * cell;
  const int16_t fieldH = TETRIS_H * cell;

  tft.drawRoundRect(fieldX - 2, fieldY - 2, fieldW + 4, fieldH + 4, 4, COLOR_TEXT);
  tft.fillRect(fieldX, fieldY, fieldW, fieldH, COLOR_BG);

  // Зафиксированные блоки.
  for (uint8_t row = 0; row < TETRIS_H; row++) {
    for (uint8_t col = 0; col < TETRIS_W; col++) {
      if (tetrisBoard[row][col]) {
        uint8_t colorIndex = (tetrisBoard[row][col] - 1) % 7;
        drawTetrisCell(fieldX + col * cell, fieldY + row * cell, cell, TETRIS_COLORS[colorIndex]);
      }
    }
  }

  // Текущая падающая фигура.
  uint16_t currentShape = TETRIS_SHAPES[tetrisPiece][tetrisRotation];
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 4; c++) {
      if (currentShape & (0x8000 >> (r * 4 + c))) {
        int8_t bx = tetrisPieceX + c;
        int8_t by = tetrisPieceY + r;

        if (bx >= 0 && bx < TETRIS_W && by >= 0 && by < TETRIS_H) {
          drawTetrisCell(fieldX + bx * cell, fieldY + by * cell, cell, TETRIS_COLORS[tetrisPiece]);
        }
      }
    }
  }

  // ================= RIGHT: SCORE / RECORD =================
  const int16_t rightX = 142;
  const int16_t rightW = 84;

  drawTetrisPanel(rightX, 52, rightW, 78, "Счёт");

  char scoreText[16];
  snprintf(scoreText, sizeof(scoreText), "%lu", (unsigned long)tetrisScore);

  char bestText[16];
  snprintf(bestText, sizeof(bestText), "%lu", (unsigned long)tetrisHighScore);

  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 8, 82, COLOR_MUTED, "Сейчас");
  drawUtf8(u8g2_font_9x15_t_cyrillic, rightX + 8, 100, COLOR_TEXT, scoreText);

  uint16_t recordColor = COLOR_TEXT;
  if (tetrisNewRecord) {
    recordColor = ((millis() / 250) % 2 == 0) ? COLOR_WARN : COLOR_TEXT;
  }

  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 8, 116, COLOR_MUTED, "Рекорд");
  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 54, 116, recordColor, bestText);

  if (tetrisNewRecord) {
    drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 46, 100, COLOR_WARN, "NEW");
  }

  // ================= RIGHT: NEXT PIECE =================
  drawTetrisPanel(rightX, 138, rightW, 78, "Следующая");
  drawTetrisMiniPiece(rightX + 20, 160, tetrisNextPiece, 11);

  // ================= RIGHT: CONTROLS =================
  drawTetrisPanel(rightX, 224, rightW, 66, "Управление");

  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 8, 254, COLOR_TEXT, "ENC  <- ->");
  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 8, 268, COLOR_TEXT, "NEXT поворот");
  drawUtf8(u8g2_font_6x12_t_cyrillic, rightX + 8, 282, COLOR_TEXT, "BACK выход");

  if (tetrisGameOver) {
    // Аккуратная карточка GAME OVER поверх игрового поля.
    const int16_t goW = 94;
    const int16_t goH = 72;
    const int16_t goX = fieldX + (fieldW - goW) / 2;
    const int16_t goY = fieldY + (fieldH - goH) / 2;

    tft.fillRoundRect(goX, goY, goW, goH, 10, COLOR_BG);
    tft.drawRoundRect(goX, goY, goW, goH, 10, COLOR_OFF);
    tft.drawRoundRect(goX + 3, goY + 3, goW - 6, goH - 6, 8, COLOR_LINE);

    const char* title = "GAME OVER";
    u8g2.setFont(u8g2_font_9x15_t_cyrillic);
    int16_t titleW = u8g2.getUTF8Width(title);
    drawUtf8(u8g2_font_9x15_t_cyrillic, goX + (goW - titleW) / 2, goY + 25, COLOR_OFF, title);

    char finalScore[22];
    snprintf(finalScore, sizeof(finalScore), "Счёт: %lu", (unsigned long)tetrisScore);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    int16_t scoreW = u8g2.getUTF8Width(finalScore);
    drawUtf8(u8g2_font_6x12_t_cyrillic, goX + (goW - scoreW) / 2, goY + 44, COLOR_TEXT, finalScore);

    const char* hint = "BACK - выход";
    int16_t hintW = u8g2.getUTF8Width(hint);
    drawUtf8(u8g2_font_6x12_t_cyrillic, goX + (goW - hintW) / 2, goY + 61, COLOR_MUTED, hint);
  }
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

  const char* dhtText = (!dhtSensorEnabled) ? "выкл" : ((!isnan(temperature) && !isnan(humidity)) ? "OK" : "нет сигнала");
  uint16_t dhtColor = (!dhtSensorEnabled) ? COLOR_WARN : ((!isnan(temperature) && !isnan(humidity)) ? COLOR_ON : COLOR_OFF);

  const char* soundText = (!soundSensorEnabled) ? "выкл" : (soundDetected ? "сработал" : "OK");
  uint16_t soundColor = (!soundSensorEnabled) ? COLOR_WARN : (soundDetected ? COLOR_OFF : COLOR_ON);

  const char* reedText = (!reedSensorEnabled) ? "выкл" : (reedDetected ? "сработал" : "OK");
  uint16_t reedColor = (!reedSensorEnabled) ? COLOR_WARN : (reedDetected ? COLOR_OFF : COLOR_ON);

  const char* waterText = (!waterSensorEnabled) ? "выкл" : (waterDetected ? "вода" : "сухо");
  uint16_t waterColor = (!waterSensorEnabled) ? COLOR_WARN : (waterDetected ? COLOR_OFF : COLOR_ON);

  const char* rfidText = (!rfidEnabled) ? "выкл" : (strlen(lastRfidUid) > 0 ? "карта" : "готов");
  uint16_t rfidColor = (!rfidEnabled) ? COLOR_WARN : (rfidDetected ? COLOR_ACCENT : COLOR_ON);

  const char* buzzerText = (!buzzerEnabled) ? "выкл" : (buzzerActive ? "звучит" : "готова");
  uint16_t buzzerColor = (!buzzerEnabled) ? COLOR_WARN : (buzzerActive ? COLOR_OFF : COLOR_ON);

  const char* labels[] = { "Охрана", "Тревога", "Wi-Fi", "DHT", "Звук", "Геркон", "Вода", "RFID", "Сирена" };
  const char* values[] = { guardText, alarmText, wifiText, dhtText, soundText, reedText, waterText, rfidText, buzzerText };
  uint16_t colors[] = { guardColor, alarmColor, wifiColor, dhtColor, soundColor, reedColor, waterColor, rfidColor, buzzerColor };

  for (uint8_t i = 0; i < 9; i++) {
    int16_t y = 72 + i * 23;
    tft.drawRoundRect(18, y - 16, SCREEN_W - 36, 20, 5, colors[i]);
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

  tft.drawRoundRect(x, 54, w, 222, 10, COLOR_LINE);

  snprintf(line, sizeof(line), "Плата: ESP32-S3");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 78, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Экран: ST7789 %dx%d", tft.width(), tft.height());
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 100, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Датчик: %s", USE_WOKWI ? "DHT22" : "DHT11");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 122, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Звук: %lu", (unsigned long)soundClickCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 144, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Геркон: %lu", (unsigned long)reedEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 166, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "Вода: %lu", (unsigned long)waterEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 188, COLOR_TEXT, line);

  snprintf(line, sizeof(line), "RFID: %lu", (unsigned long)rfidEventCount);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 210, COLOR_TEXT, line);

  if (strlen(lastRfidUid) > 0) {
    snprintf(line, sizeof(line), "UID: %.18s", lastRfidUid);
    drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 232, COLOR_MUTED, line);
  }

  unsigned long uptime = millis() / 1000;
  snprintf(line, sizeof(line), "Uptime: %lum %lus", uptime / 60, uptime % 60);
  drawUtf8(u8g2_font_6x12_t_cyrillic, 32, 254, COLOR_TEXT, line);
}

void drawDescriptionMenu() {
  drawPageTitle("Описание");

  tft.drawRoundRect(14, 54, SCREEN_W - 28, 236, 10, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 76, COLOR_TEXT, "Система мониторинга");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 92, COLOR_MUTED, "отслеживает звук,");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 26, 108, COLOR_MUTED, "геркон, воду и сирену.");

  tft.drawFastHLine(24, 124, SCREEN_W - 48, COLOR_LINE);

  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 146, COLOR_TEXT, "З - звук");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 164, COLOR_TEXT, "Г - геркон");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 182, COLOR_TEXT, "В - вода");
  drawUtf8(u8g2_font_6x12_t_cyrillic, 28, 200, COLOR_TEXT, "С - сирена");

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
  else if (screen == SCREEN_WIFI_SETUP_INFO) drawWifiSetupInfoMenu();
  else if (screen == SCREEN_SETTINGS) drawSettingsMenu();
  else if (screen == SCREEN_PIN_SETTINGS) drawPinSettingsMenu();
  else if (screen == SCREEN_GUARD_SETTINGS) drawGuardSettingsMenu();
  else if (screen == SCREEN_ALERT_SETTINGS) drawAlertSettingsMenu();
  else if (screen == SCREEN_TELEGRAM_SETTINGS) drawTelegramSettingsMenu();
  else if (screen == SCREEN_TELEGRAM_COMMANDS) drawTelegramCommandsMenu();
  else if (screen == SCREEN_PASSWORD) drawPasswordScreen();
  else if (screen == SCREEN_SET_PIN) drawSetPinScreen();
  else if (screen == SCREEN_SYSTEM) drawSystemMenu();
  else if (screen == SCREEN_TETRIS) drawTetrisScreen();
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

void initTetrisGame() {
  memset(tetrisBoard, 0, sizeof(tetrisBoard));
  tetrisScore = 0;
  tetrisNewRecord = false;
  tetrisNextPiece = random(0, 7);
  tetrisNextReady = true;
  tetrisFallInterval = 550;
  tetrisRunning = true;
  tetrisGameOver = false;
  randomSeed(micros());
  tetrisSpawnPiece();
  lastTetrisFall = millis();
}

bool tetrisCollision(int8_t x, int8_t y, uint8_t piece, uint8_t rotation) {
  uint16_t mask = TETRIS_SHAPES[piece][rotation % 4];

  for (uint8_t py = 0; py < 4; py++) {
    for (uint8_t px = 0; px < 4; px++) {
      if (mask & (0x8000 >> (py * 4 + px))) {
        int8_t bx = x + px;
        int8_t by = y + py;
        if (bx < 0 || bx >= TETRIS_W || by >= TETRIS_H) return true;
        if (by >= 0 && tetrisBoard[by][bx] > 0) return true;
      }
    }
  }

  return false;
}

void tetrisSpawnPiece() {
  if (!tetrisNextReady) {
    tetrisNextPiece = random(0, 7);
    tetrisNextReady = true;
  }

  tetrisPiece = tetrisNextPiece;
  tetrisNextPiece = random(0, 7);
  tetrisRotation = 0;
  tetrisPieceX = 3;
  tetrisPieceY = 0;

  if (tetrisCollision(tetrisPieceX, tetrisPieceY, tetrisPiece, tetrisRotation)) {
    tetrisGameOver = true;
    tetrisRunning = false;
    if (tetrisScore > tetrisHighScore) {
      tetrisHighScore = tetrisScore;
      tetrisNewRecord = true;
      saveSettings();
    }
  }
}

void tetrisLockPiece() {
  uint16_t mask = TETRIS_SHAPES[tetrisPiece][tetrisRotation];
  for (uint8_t py = 0; py < 4; py++) {
    for (uint8_t px = 0; px < 4; px++) {
      if (mask & (0x8000 >> (py * 4 + px))) {
        int8_t bx = tetrisPieceX + px;
        int8_t by = tetrisPieceY + py;
        if (bx >= 0 && bx < TETRIS_W && by >= 0 && by < TETRIS_H) {
          tetrisBoard[by][bx] = tetrisPiece + 1;
        }
      }
    }
  }

  tetrisClearLines();
  tetrisSpawnPiece();
}

void tetrisClearLines() {
  uint8_t cleared = 0;

  for (int8_t y = TETRIS_H - 1; y >= 0; y--) {
    bool full = true;
    for (uint8_t x = 0; x < TETRIS_W; x++) {
      if (tetrisBoard[y][x] == 0) {
        full = false;
        break;
      }
    }

    if (full) {
      cleared++;
      for (int8_t yy = y; yy > 0; yy--) {
        memcpy(tetrisBoard[yy], tetrisBoard[yy - 1], TETRIS_W);
      }
      memset(tetrisBoard[0], 0, TETRIS_W);
      y++;
    }
  }

  if (cleared > 0) {
    tetrisScore += cleared * cleared * 100;
    if (tetrisScore > tetrisHighScore) {
      tetrisHighScore = tetrisScore;
      tetrisNewRecord = true;
    }
    if (tetrisFallInterval > 160) tetrisFallInterval -= cleared * 15;
  }
}

void tetrisMove(int8_t dx) {
  if (tetrisGameOver) return;
  if (!tetrisCollision(tetrisPieceX + dx, tetrisPieceY, tetrisPiece, tetrisRotation)) {
    tetrisPieceX += dx;
    menuDirty = true;
  }
}

void tetrisRotatePiece() {
  if (tetrisGameOver) {
    initTetrisGame();
    menuDirty = true;
    return;
  }

  uint8_t newRotation = (tetrisRotation + 1) % 4;
  if (!tetrisCollision(tetrisPieceX, tetrisPieceY, tetrisPiece, newRotation)) {
    tetrisRotation = newRotation;
  } else if (!tetrisCollision(tetrisPieceX - 1, tetrisPieceY, tetrisPiece, newRotation)) {
    tetrisPieceX--;
    tetrisRotation = newRotation;
  } else if (!tetrisCollision(tetrisPieceX + 1, tetrisPieceY, tetrisPiece, newRotation)) {
    tetrisPieceX++;
    tetrisRotation = newRotation;
  }
  menuDirty = true;
}

void tetrisSoftDrop() {
  if (tetrisGameOver) return;
  if (!tetrisCollision(tetrisPieceX, tetrisPieceY + 1, tetrisPiece, tetrisRotation)) {
    tetrisPieceY++;
    tetrisScore++;
  } else {
    tetrisLockPiece();
  }
  menuDirty = true;
}

void tetrisHardDrop() {
  if (tetrisGameOver) {
    initTetrisGame();
    menuDirty = true;
    return;
  }

  while (!tetrisCollision(tetrisPieceX, tetrisPieceY + 1, tetrisPiece, tetrisRotation)) {
    tetrisPieceY++;
    tetrisScore += 2;
  }
  tetrisLockPiece();
  menuDirty = true;
}

void updateTetrisGame() {
  if (screen != SCREEN_TETRIS || !tetrisRunning || tetrisGameOver) return;

  if (millis() - lastTetrisFall >= tetrisFallInterval) {
    lastTetrisFall = millis();
    tetrisSoftDrop();
  }
}

void tetrisExit() {
  if (tetrisScore > tetrisHighScore) {
    tetrisHighScore = tetrisScore;
    tetrisNewRecord = true;
    saveSettings();
  }
  tetrisRunning = false;
  screen = SCREEN_SYSTEM;
  menuDirty = true;
}

void updateUi() {
  unsigned long now = millis();
  bool frameChanged = false;

  if (screen == SCREEN_PASSWORD && pinStatusText[0] != ' ' && now >= pinStatusUntil) {
    pinStatusText[0] = ' ';
    menuDirty = true;
  }

  updateTetrisGame();

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
        frameChanged = true;
        mainFullDirty = false;
        topBarDirty = false;
        clockDirty = false;
        sensorStripDirty = false;
        notificationsDirty = false;
      } else {
        if (topBarDirty) {
          drawTopBar();
          frameChanged = true;
          topBarDirty = false;
        }
        if (clockDirty) {
          drawClockBlock();
          frameChanged = true;
          clockDirty = false;
        }
        if (sensorStripDirty) {
          drawSensorStrip();
          frameChanged = true;
          sensorStripDirty = false;
        }
        if (notificationsDirty) {
          drawNotifications();
          frameChanged = true;
          notificationsDirty = false;
        }
      }

      mainDirty = topBarDirty || clockDirty || sensorStripDirty || notificationsDirty || mainFullDirty;
    }
  } else if (menuDirty) {
    drawMenuScreen();
    drawAlarmFrame();
    frameChanged = true;
    menuDirty = false;
  }

  if (frameChanged) {
    flushFrame();
  }
}
