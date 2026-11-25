// EUCLID + XOX, firmware for the HAGIWO / Modulove A-RYTH-MATIK. Credits and license: README.md, LICENSE.

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EncoderButton.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "Font4x7.h"

const char sSAVE[] PROGMEM = "SAVE";
const char sLOAD[] PROGMEM = "LOAD";
const char sINT[] PROGMEM = "INT";
const char sEXT[] PROGMEM = "EXT";
const char sCLOCK[] PROGMEM = "CLOCK";
#define FS(x) reinterpret_cast<const __FlashStringHelper *>(x)
void textAt(int x, int y, byte size = 1);
void numAt(int x, int y, byte size = 1);

#if defined(__LGT8FX8P__)
#define LGT8FX_BOARD
#endif

#ifdef LGT8FX_BOARD
#define NUM_MEMORY_SLOTS 10
#else
#define NUM_MEMORY_SLOTS 20
#endif

#ifdef ROTATE_PANEL

#define RESET FastGPIO::Pin<13>
#define CLK FastGPIO::Pin<11>
#define OUTPUT1 FastGPIO::Pin<8>
#define OUTPUT2 FastGPIO::Pin<9>
#define OUTPUT3 FastGPIO::Pin<10>
#define OUTPUT4 FastGPIO::Pin<5>
#define OUTPUT5 FastGPIO::Pin<6>
#define OUTPUT6 FastGPIO::Pin<7>
#define LED1 FastGPIO::Pin<0>
#define LED2 FastGPIO::Pin<1>
#define LED3 FastGPIO::Pin<17>
#define LED4 FastGPIO::Pin<14>
#define LED5 FastGPIO::Pin<15>
#define LED6 FastGPIO::Pin<16>
#else

#define RESET FastGPIO::Pin<11>
#define CLK FastGPIO::Pin<13>
#define OUTPUT1 FastGPIO::Pin<5>
#define OUTPUT2 FastGPIO::Pin<6>
#define OUTPUT3 FastGPIO::Pin<7>
#define OUTPUT4 FastGPIO::Pin<8>
#define OUTPUT5 FastGPIO::Pin<9>
#define OUTPUT6 FastGPIO::Pin<10>
#define LED1 FastGPIO::Pin<14>
#define LED2 FastGPIO::Pin<15>
#define LED3 FastGPIO::Pin<16>
#define LED4 FastGPIO::Pin<0>
#define LED5 FastGPIO::Pin<1>
#define LED6 FastGPIO::Pin<17>
#endif
#define CLK_LED FastGPIO::Pin<4>

const byte ENCODER_PIN1 = 2;
const byte ENCODER_PIN2 = 3;
const byte ENCODER_SW_PIN = 12;

enum TopMenu {
  MENU_CH_1,
  MENU_CH_2,
  MENU_CH_3,
  MENU_CH_4,
  MENU_CH_5,
  MENU_CH_6
};

enum Setting {
  SETTING_TOP_MENU,
  SETTING_HITS,
  SETTING_OFFSET,
  SETTING_LIMIT,
  SETTING_RESET,
  SETTING_PROB,
  SETTING_COND,
  SETTING_XOX
};

// One cyclic chain. Click: next. Double click: previous. Long click: opens COND, XOX, RNDM, MUTE, SYST, runs RSET, closes an open mode.
// Press and turn: change channel.
enum Nav : byte {
  NAV_CH,
  NAV_HITS,
  NAV_OFFS,
  NAV_LIMI,
  NAV_RESET,
  NAV_PROB,
  NAV_COND,
  NAV_XOX,
  NAV_RNDM,
  NAV_MUTE,
  NAV_RSET,
  NAV_SYST,
  NAV_LAST
};
#define MULTI_CLICK_MS 220

#define FIRMWARE_MAGIC "EUCLID11"
#define FIRMWARE_MAGIC_ADDRESS 0

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define EEPROM_START_ADDRESS 20
#define CONFIG_SIZE (sizeof(StoredConfig))
#define LAST_USED_SLOT_ADDRESS (EEPROM_START_ADDRESS + NUM_MEMORY_SLOTS * CONFIG_SIZE)

#define AUTOSAVE_ADDRESS (LAST_USED_SLOT_ADDRESS + 2)
#define AUTOSAVE_MARK 0xA5
#define AUTOSAVE_DELAY_MS 10000UL

byte playing_step[6] = { 0 };

// COND and RNDM: index into barMask (bit n = bar n+1 of 4). COND is stored in the high nibble of offset (xor 15);
// RNDM in the 3 high bits of hits and the top bit of probability.
const byte barMask[16] PROGMEM = { 0, 1, 2, 4, 8, 3, 6, 12, 5, 10, 9, 7, 14, 11, 13, 15 };
#define BAR_ALL 15
byte barCond[6] = { BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL };
byte barClock = 15;
byte barIdx = 3;

TopMenu selected_menu = MENU_CH_1;
Setting selected_setting = SETTING_TOP_MENU;
byte nav = NAV_CH;
bool xoxMode = false;
bool systMode = false;
byte sysItem = 0;
byte confirming = 0;
byte selected_slot = 0;
bool disp_refresh = true;
bool force_refresh = true;

byte cutMask = 0;

bool condMode = false;

bool muteMode = false;
byte muteCursor = 0;

bool rndmMode = false;
byte rndmCursor = 0;
byte rndmCond[6] = { 0, 0, 0, 0, 0, 0 };
bool internalClock = false;
bool showOverlay = false;

int tempo = 120;
int period = 60000 / tempo / 4;

int externalBPM = 0;

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 }, graph_y[6] = { 0, 0, 0, 32, 32, 32 };

const byte x16[16] PROGMEM = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, y16[16] PROGMEM = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;
unsigned long gate_timer = 0;
#define LED_FLASH_MS 5
#define LONG_CLICK_MS 300
#define TRIG_MS 25

unsigned long last_clock_input = 0;
unsigned long internalClockTimer = 0;

const static byte euc16[MAX_PATTERNS][MAX_STEPS] PROGMEM = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
  { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0 },
  { 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0 },
  { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1 },
  { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
  { 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1 },
  { 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
  { 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }
};
uint16_t offset_buf[MAX_CHANNELS];
#define stepOn(ch, j) ((offset_buf[ch] >> ((j) & 15)) & 1)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#endif

struct SlotConfiguration {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  bool mute[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  byte probability[MAX_CHANNELS];
  int tempo;
  bool internalClock;
};

SlotConfiguration currentConfig;
byte lastUsedSlot = 0;

uint16_t xoxPattern[MAX_CHANNELS];
byte xoxMask = 0;
byte xoxCursor[MAX_CHANNELS];

// EEPROM slot, 41 bytes
struct StoredConfig {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  byte probability[MAX_CHANNELS];
  byte muteMask;
  byte xoxMask;
  uint16_t xox[MAX_CHANNELS];
  int tempo;
  byte internalClock;
};

unsigned long lastChangeTime = 0;
bool autosavePending = false;
bool autosaveWriting = false;
byte autosaveIndex = 0;

#define NAMES_ADDRESS (AUTOSAVE_ADDRESS + sizeof(StoredConfig) + 2)
#define PRESETS_FLAG_ADDRESS (NAMES_ADDRESS + NUM_MEMORY_SLOTS * 3)
#define PRESETS_MARK 0x5D
const char NAME_CHARS[] PROGMEM = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NUM_NAME_CHARS 38
byte nameEdit = 0;
char nameBuf[3];

const StoredConfig presets[] PROGMEM = {

  { { 2, 3, 4, 5, 6, 7 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },

  { { 4, 5, 8, 4, 7, 9 }, { 0, 12, 0, 2, 0, 10 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },

  { { 4, 6, 8, 4, 10, 2 }, { 0, 5, 0, 2, 3, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 100, 2 },

  { { 4, 4, 8, 4, 6, 4 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x1111, 0x4848, 0x5555, 0x4444, 0x4949, 0x4141 }, 95, 2 },

  { { 4, 2, 12, 4, 4, 3 }, { 4, 14, 15, 12, 0, 10 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x20, { 0x0101, 0x4444, 0x5555, 0xDDDD, 0x40C0, 0x0181 }, 95, 2 },

  { { 4, 2, 3, 4, 5, 9 }, { 0, 10, 0, 10, 11, 4 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 87, 60 }, 0, 0x0F, { 0x0103, 0x8100, 0x05A5, 0x1D11, 0x4208, 0x0241 }, 132, 2 },

  { { 4, 2, 12, 4, 7, 2 }, { 0, 0, 0, 0, 0, 0 }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x0249, 0x0208, 0x0FFF, 0x0924, 0x0AD5, 0x0041 }, 90, 2 },

  { { 4, 3, 5, 4, 2, 5 }, { 0, 0, 0, 0, 0, 0 }, { 16, 7, 11, 9, 5, 13 }, { 100, 100, 80, 60, 70, 50 }, 0, 0x3F, { 0x1111, 0x0029, 0x02A9, 0x00A9, 0x0009, 0x0949 }, 120, 2 },

  { { 16, 8, 4, 2, 1, 4 }, { 0, 0, 0, 0, 0, 2 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0, { 0 }, 120, 2 },
};
const char presetNames[] PROGMEM = "EUCBOSCUBDEMCMBBRKTERPOLDIV";
#define NUM_PRESETS (sizeof(presets) / sizeof(StoredConfig))

byte autosaveCheck = 0;

const unsigned char Splash_Logo [] PROGMEM = {
  0x03, 0xff, 0x8f, 0xcf, 0xe0, 0x00, 0x00, 0x03, 0xfe, 0x06,
  0x00, 0x50, 0x68, 0x31, 0xc0, 0xa0, 0x46, 0x03, 0x0f, 0xff,
  0x7f, 0xff, 0xf3, 0xfe, 0xff, 0x4f, 0xfd, 0x1f, 0xfd, 0xfc,
  0xfe, 0x77, 0xf9, 0xfd, 0x7f, 0xe5, 0x20, 0x03, 0x82, 0x81,
  0x7e, 0x07, 0x07, 0x60, 0x15, 0x7f, 0xfb, 0xfb, 0xfd, 0x7f,
  0xf7, 0xf3, 0xff, 0xd5, 0x80, 0x0c, 0x0f, 0x05, 0x70, 0x14,
  0x0b, 0x80, 0x75, 0x80, 0x0c, 0x0f, 0x05, 0xc0, 0x1c, 0x0b,
  0x80, 0x76, 0x80, 0x0c, 0x0f, 0x05, 0x80, 0x1c, 0x0b, 0x80,
  0x7c, 0x80, 0x0c, 0x0f, 0x05, 0x00, 0x1c, 0x0b, 0x80, 0x7e,
  0x80, 0x0c, 0x0f, 0x07, 0x00, 0x1c, 0x0b, 0x80, 0x7b, 0x80,
  0xfc, 0x0f, 0x07, 0x00, 0x1c, 0x0a, 0x80, 0x6d, 0x81, 0xdc,
  0x0f, 0x06, 0x03, 0xfc, 0x0b, 0xc0, 0xe5, 0x80, 0xcc, 0x06,
  0x06, 0x02, 0x04, 0x08, 0xc0, 0x95, 0x80, 0x74, 0x00, 0x06,
  0x03, 0xf4, 0x0f, 0xc0, 0xd5, 0x80, 0x0a, 0x00, 0x07, 0x00,
  0x14, 0x01, 0x80, 0x75, 0x80, 0x0a, 0x00, 0x07, 0x00, 0x1c,
  0x01, 0x80, 0x76, 0x80, 0x0a, 0x00, 0x07, 0x00, 0x1c, 0x01,
  0x80, 0x7c, 0x80, 0x0b, 0x00, 0x09, 0x80, 0x1c, 0x01, 0x80,
  0x78, 0x80, 0x0f, 0x80, 0x1b, 0xc0, 0x1c, 0x01, 0x80, 0x70,
  0x80, 0x0c, 0xe0, 0x77, 0xf8, 0x1c, 0x01, 0x80, 0x40, 0x7f,
  0xfe, 0xff, 0xdc, 0x1f, 0xff, 0xff, 0xff, 0xc0, 0x01, 0xfb,
  0xfd, 0x7f, 0xc2, 0xfc, 0xff, 0x58, 0x00, 0x02, 0x0d, 0x03,
  0xf0, 0x31, 0x82, 0xc1, 0x50, 0x00, 0x02, 0x05, 0x03, 0xc0,
  0x0d, 0x02, 0x81, 0x70, 0x00, 0x02, 0x06, 0x03, 0x80, 0x05,
  0x01, 0x81, 0xe0, 0x00, 0x02, 0x02, 0x03, 0x00, 0x03, 0x01,
  0x01, 0xd0, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x80, 0x01,
  0xd0, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x01, 0x80, 0x03, 0x48,
  0x00, 0x01, 0x80, 0x0e, 0x07, 0x81, 0x40, 0x03, 0x28, 0x00,
  0x00, 0x80, 0x1a, 0x04, 0x81, 0xe0, 0x04, 0xac, 0x00, 0x01,
  0x80, 0x0a, 0x07, 0x81, 0xc0, 0x02, 0x9c, 0x00, 0x01, 0x00,
  0x0e, 0x00, 0x01, 0xc0, 0x02, 0x5c, 0x00, 0x02, 0x00, 0x07,
  0x00, 0x03, 0x80, 0x01, 0x58, 0x00, 0x02, 0x02, 0x03, 0x00,
  0x03, 0x01, 0x01, 0x70, 0x00, 0x02, 0x06, 0x03, 0x80, 0x05,
  0x01, 0x81, 0x40, 0x00, 0x02, 0x07, 0x03, 0xc0, 0x0d, 0x03,
  0x81, 0xc0, 0x00, 0x02, 0x0d, 0x02, 0x30, 0x31, 0x82, 0xc1,
  0x00, 0x00, 0x01, 0xf8, 0xfc, 0x0f, 0xc0, 0xfc, 0x7f, 0x00,
  0x00
};
const byte logoWidth = 72;
const byte logoHeight = 39;

void drawAnimation() {
  display.clearDisplay();
  display.drawBitmap(31, 13, Splash_Logo, logoWidth, logoHeight, WHITE);
  display.display();
}

void (*ClockHandler)() = NULL;
void (*ResetHandler)() = NULL;

volatile bool clockTriggerFlag = false;
volatile bool resetTriggerFlag = false;

volatile bool prev_clk_state = false;
volatile bool prev_rst_state = false;

void handleClockInterrupt() {
  clockTriggerFlag = true;
  last_clock_input = millis();

  static unsigned long lastPulseUs = 0;
  static unsigned long avgUs = 0;
  unsigned long now = micros();
  unsigned long interval = now - lastPulseUs;
  if (lastPulseUs && interval < 2000000UL) {
    avgUs = avgUs ? (avgUs * 3 + interval) / 4 : interval;
    externalBPM = (15000000UL + avgUs / 2) / avgUs;
    if (externalBPM > 360) externalBPM = 360;
    period = avgUs / 1000;
  } else {
    avgUs = 0;
  }
  lastPulseUs = now;
}

void handleResetInterrupt() {
  resetTriggerFlag = true;
}

ISR(PCINT0_vect) {

  bool current_clk_state, current_rst_state;

#ifdef ROTATE_PANEL

  current_clk_state = (PINB & (1 << PINB3)) != 0;
  current_rst_state = (PINB & (1 << PINB5)) != 0;
#else

  current_clk_state = (PINB & (1 << PINB5)) != 0;
  current_rst_state = (PINB & (1 << PINB3)) != 0;
#endif

  if (!prev_clk_state && current_clk_state && ClockHandler) {
    ClockHandler();
  }

  if (!prev_rst_state && current_rst_state && ResetHandler) {
    ResetHandler();
  }

  prev_clk_state = current_clk_state;
  prev_rst_state = current_rst_state;
}

void setupPinChangeInterrupts() {

  if (ClockHandler == NULL) ClockHandler = handleClockInterrupt;
  if (ResetHandler == NULL) ResetHandler = handleResetInterrupt;

  cli();

#ifdef ROTATE_PANEL
  prev_clk_state = (PINB & (1 << PINB3)) != 0;
  prev_rst_state = (PINB & (1 << PINB5)) != 0;
#else
  prev_clk_state = (PINB & (1 << PINB5)) != 0;
  prev_rst_state = (PINB & (1 << PINB3)) != 0;
#endif

  PCICR |= (1 << PCIE0);

#ifdef ROTATE_PANEL

  PCMSK0 |= (1 << PCINT3) | (1 << PCINT5);
#else

  PCMSK0 |= (1 << PCINT5) | (1 << PCINT3);
#endif

  sei();
}

void Random_change(byte k) {
  byte h;
  do {
    h = random(1, MAX_PATTERNS);
  } while (h == currentConfig.hits[k]);
  xoxMask &= ~(1 << k);
  currentConfig.hits[k] = h;
  byte o;
  do {
    o = random(0, MAX_STEPS);
  } while (o == currentConfig.offset[k]);
  currentConfig.offset[k] = o;
}

void onOverlayTimeout(EncoderButton &eb) {
  if (!nameEdit) return;
  finishName();
  disp_refresh = true;
}

void setup() {

  setupEncoder();
  initIO();

  setupPinChangeInterrupts();
  initDisplay();

#if !defined(LGT8FX_BOARD) && !defined(DISABLE_BOOT_LOGO)
  drawAnimation();
  delay(1200);
#endif

  checkAndInitializeSettings();
  writePresets();

  EEPROM.get(LAST_USED_SLOT_ADDRESS, lastUsedSlot);
  if (lastUsedSlot >= NUM_MEMORY_SLOTS) {
    lastUsedSlot = 0;
  }
  selected_slot = lastUsedSlot;

  if (!loadAutosave()) {
    loadFromEEPROM(lastUsedSlot);
  }

  updateRythm();
  OLED_display();

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  last_clock_input = millis();
  internalClockTimer = millis();
}

void loop() {
  encoder.update();

  updateRythm();

  bool beat_start = false;

  if (resetTriggerFlag) {
    resetTriggerFlag = false;
    resetSeq();
    disp_refresh = true;
  }

  if (!internalClock && clockTriggerFlag) {
    clockTriggerFlag = false;
    beat_start = true;
  }

  if (internalClock && (millis() - internalClockTimer >= period)) {
    beat_start = true;
    internalClockTimer = millis();
    last_clock_input = millis();
  }

  if (beat_start) {
    gate_timer = millis();
    force_refresh = true;
    CLK_LED::setOutput(1);
    for (int i = 0; i < MAX_CHANNELS; i++) {
      playing_step[i]++;
      if (playing_step[i] >= currentConfig.limit[i]) {
        playing_step[i] = 0;
      }
    }
    if (++barClock >= 16) {
      barClock = 0;
      barIdx = (barIdx + 1) & 3;
      bool changed = false;
      for (byte k = 0; k < MAX_CHANNELS; k++) {
        if ((pgm_read_byte(&barMask[rndmCond[k]]) >> barIdx) & 1) {
          Random_change(k);
          changed = true;
        }
      }
      if (changed) updateRythm();
    }

    outputGateSignals();

    disp_refresh = true;
  }

  if (gate_timer + TRIG_MS <= millis()) {
    OUTPUT1::setOutput(0);
    OUTPUT2::setOutput(0);
    OUTPUT3::setOutput(0);
    OUTPUT4::setOutput(0);
    OUTPUT5::setOutput(0);
    OUTPUT6::setOutput(0);
  }
  if (gate_timer + LED_FLASH_MS <= millis()) {
    CLK_LED::setOutput(0);
    LED1::setOutput(0);
    LED2::setOutput(0);
    LED3::setOutput(0);
    LED4::setOutput(0);
    LED5::setOutput(0);
    LED6::setOutput(0);
  }

  if (selected_setting == SETTING_XOX) {
    static bool blinkPhase = false;
    bool phase = millis() & 512;
    if (phase != blinkPhase) {
      blinkPhase = phase;
      disp_refresh = true;
    }
  }

  if (millis() - gate_timer >= LED_FLASH_MS) OLED_display();

  autosaveStep();
}

void setupEncoder() {
  encoder.setDebounceInterval(2);
  encoder.setMultiClickInterval(MULTI_CLICK_MS);
  encoder.setRateLimit(100);
  encoder.setIdleTimeout(5000);
  encoder.setIdleHandler(onOverlayTimeout);
  encoder.setLongClickDuration(LONG_CLICK_MS);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setDoubleClickHandler(onEncoderDoubleClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);
}

void initIO() {
  RESET::setInput();
  CLK::setInput();
  OUTPUT1::setOutputLow();
  OUTPUT2::setOutputLow();
  OUTPUT3::setOutputLow();
  OUTPUT4::setOutputLow();
  OUTPUT5::setOutputLow();
  OUTPUT6::setOutputLow();

  LED1::setOutputLow();
  LED2::setOutputLow();
  LED3::setOutputLow();
  LED4::setOutputLow();
  LED5::setOutputLow();
  LED6::setOutputLow();
  CLK_LED::setOutputLow();
}

void textAt(int x, int y, byte size) {
  display.setTextSize(size);
  display.setCursor(x, y);
}

void numAt(int x, int y, byte size) {
  textAt(x + size, y, size);
}

void initDisplay() {
  delay(1000);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  Wire.setClock(400000L);
  display.clearDisplay();
  display.setFont(&Font4x7);
  display.setTextSize(1);
  display.setTextColor(WHITE);

#ifdef ROTATE_PANEL
  display.setRotation(2);
#endif

#ifdef DEBUG

  display.println(F("ARYTHMATIK Euclid"));
  display.println(F("Firmware: v0.1"));
  display.println(F("6CH Euclidean Rhythm"));
  display.display();
  delay(1000);
#endif
}

void outputGateSignals() {
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (stepOn(i, playing_step[i]) && currentConfig.mute[i] == 0 && ((pgm_read_byte(&barMask[barCond[i]]) >> barIdx) & 1) && random(100) < currentConfig.probability[i]) {
      switch (i) {
        case 0:
          OUTPUT1::setOutput(1);
          LED1::setOutput(1);
          break;
        case 1:
          OUTPUT2::setOutput(1);
          LED2::setOutput(1);
          break;
        case 2:
          OUTPUT3::setOutput(1);
          LED3::setOutput(1);
          break;
        case 3:
          OUTPUT4::setOutput(1);
          LED4::setOutput(1);
          break;
        case 4:
          OUTPUT5::setOutput(1);
          LED5::setOutput(1);
          break;
        case 5:
          OUTPUT6::setOutput(1);
          LED6::setOutput(1);
          break;
      }
    }
  }
}

bool isClockRunning() {
  return internalClock || (millis() - last_clock_input < period);
}

bool inMode() {
  return xoxMode || muteMode || rndmMode || condMode || systMode;
}

void syncNav() {
  selected_setting = nav < NAV_COND ? static_cast<Setting>(nav) : (nav == NAV_COND && condMode ? SETTING_COND : (nav == NAV_XOX && xoxMode ? SETTING_XOX : SETTING_TOP_MENU));
  encoder.setMultiClickInterval(inMode() ? 20 : MULTI_CLICK_MS);
  disp_refresh = true;
}

void changeChannel(int dir) {
  selected_menu = static_cast<TopMenu>((selected_menu + (dir > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS);
}

void onEncoderClicked(EncoderButton &eb) {
  markChange();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;

  if (nameEdit) {
    if (++nameEdit > 3) finishName();
  } else if (systMode) {
    if (!showOverlay) {
      showOverlay = true;
      confirming = 0;
    } else if (sysItem < 2 && !confirming) {
      confirming = 1;
    } else if (sysItem == 0 && confirming == 2) {
      bool used = slotUsed(selected_slot);
      saveToEEPROM(selected_slot);
      for (byte k = 0; k < 3; k++) nameBuf[k] = used ? nameChar(selected_slot, k) : '-';
      nameEdit = 1;
    } else {
      if (sysItem == 1 && confirming == 2) {
        loadFromEEPROM(selected_slot);
        leaveSyst();
      }
      showOverlay = false;
    }
  } else if (xoxMode) {
    toggleXoxStep(selected_menu);
  } else if (muteMode) {
    currentConfig.mute[muteCursor] = !currentConfig.mute[muteCursor];
    cutMask = 0;
  } else if (rndmMode || condMode) {

  } else {
    nav = (nav + 1) % NAV_LAST;
    syncNav();
  }
}

void onEncoderDoubleClicked(EncoderButton &eb) {
  if (inMode()) {
    onEncoderClicked(eb);
    onEncoderClicked(eb);
    return;
  }
  markChange();
  nav = (nav + NAV_LAST - 1) % NAV_LAST;
  syncNav();
}

void onEncoderLongClicked(EncoderButton &eb) {
  markChange();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;

  if (nameEdit) {
    finishName();
  } else if (systMode) {
    if (showOverlay) showOverlay = false;
    else systMode = false;
  } else if (xoxMode) {
    xoxMode = false;
  } else if (muteMode) {
    muteMode = false;
  } else if (rndmMode) {
    rndmMode = false;
  } else if (condMode) {
    condMode = false;
  } else {
    switch (nav) {
      case NAV_XOX: xoxMode = true; break;
      case NAV_RNDM: rndmMode = true; break;
      case NAV_COND: condMode = true; break;
      case NAV_MUTE: muteMode = true; break;
      case NAV_RSET: resetSeq(); break;
      case NAV_SYST:
        systMode = true;
        sysItem = 0;
        showOverlay = false;
        break;
    }
  }
  syncNav();
}

void sysTurn(int increment, int accelerated) {
  if (!showOverlay) {
    sysItem = (sysItem + (increment > 0 ? 1 : 3)) & 3;
  } else if (sysItem < 2) {
    if (nameEdit) {
      char *c = &nameBuf[nameEdit - 1];
      byte i = (const char *)strchr_P(NAME_CHARS, *c) - NAME_CHARS;
      *c = pgm_read_byte(&NAME_CHARS[(i + increment + NUM_NAME_CHARS) % NUM_NAME_CHARS]);
    } else if (confirming) {
      confirming = 3 - confirming;
    } else {
      selected_slot = (selected_slot + increment + NUM_MEMORY_SLOTS) % NUM_MEMORY_SLOTS;
    }
  } else if (sysItem == 2) {
    internalClock = !internalClock;
    currentConfig.internalClock = internalClock;
    if (internalClock) period = 60000 / tempo / 4;
  } else if (internalClock) {
    tempo = constrain(tempo + accelerated, 40, 240);
    period = 60000 / tempo / 4;
  }
}

void onEncoderRotation(EncoderButton &eb) {
  int increment = encoder.increment();
  if (increment == 0) return;
  markChange();
  disp_refresh = true;

  if (muteMode) {
    muteCursor = (muteCursor + (increment > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS;
    return;
  }
  if (rndmMode) {
    rndmCond[rndmCursor] = (rndmCond[rndmCursor] + (increment > 0 ? 1 : 15)) & 15;
    return;
  }

  int acceleratedIncrement = increment * increment;
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  if (systMode) {
    sysTurn(increment, acceleratedIncrement);
  } else if (nav == NAV_CH) {
    changeChannel(increment);
  } else if (nav < NAV_COND || xoxMode || condMode) {
    handleSettingNavigation(acceleratedIncrement);
  }

}

void onEncoderPressedRotation(EncoderButton &eb) {
  int increment = encoder.increment();
  if (increment == 0) return;
  markChange();
  disp_refresh = true;

  if (muteMode) {
    if (increment > 0) {
      if (!cutMask) {
        cutMask = ~mutedMask() & 0x3F;
        setMutes(0x3F);
      }
    } else if (cutMask) {
      setMutes(mutedMask() & ~cutMask);
      cutMask = 0;
    } else {
      setMutes(0);
    }
  } else if (rndmMode) {
    rndmCursor = (rndmCursor + (increment > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS;
  } else if (!systMode) {
    changeChannel(increment);
  }
}

bool isXox(byte ch) {
  return xoxMask & (1 << ch);
}

byte xoxPos(byte ch) {
  byte len = currentConfig.limit[ch];
  if (len == 0 || len > MAX_STEPS) len = MAX_STEPS;
  return xoxCursor[ch] % len;
}

void leaveXox(byte ch) {
  if (!isXox(ch)) return;
  xoxMask &= ~(1 << ch);
  currentConfig.hits[ch] = __builtin_popcount(xoxPattern[ch]);
}

void toggleXoxStep(byte ch) {
  if (!isXox(ch)) {
    uint16_t p = 0;
    for (byte s = 0; s < MAX_STEPS; s++) {
      if (pgm_read_byte(&euc16[currentConfig.hits[ch]][s])) p |= 1u << s;
    }
    xoxPattern[ch] = p;
    xoxMask |= 1 << ch;
  }

  xoxPattern[ch] ^= 1u << ((xoxPos(ch) + currentConfig.offset[ch]) % MAX_STEPS);
}

byte checksum8(const byte *p, byte n) {
  byte c = 0x5A;
  while (n--) c = ((c << 1) | (c >> 7)) ^ *p++;
  return c;
}

void markChange() {
  lastChangeTime = millis();
  autosavePending = true;
  autosaveWriting = false;
}

void autosaveStep() {
  if (!autosaveWriting) {
    if (!autosavePending || millis() - lastChangeTime < AUTOSAVE_DELAY_MS) return;
    autosaveCheck = 0x5A;
    autosaveIndex = 0;
    autosavePending = false;
    autosaveWriting = true;
  }
  if (!eeprom_is_ready()) return;
  byte value;
  if (autosaveIndex == 0) value = AUTOSAVE_MARK;
  else if (autosaveIndex <= sizeof(StoredConfig)) {

    StoredConfig s;
    packConfig(s);
    value = ((const byte *)&s)[autosaveIndex - 1];
    autosaveCheck = ((autosaveCheck << 1) | (autosaveCheck >> 7)) ^ value;
  } else value = autosaveCheck;
  EEPROM.update(AUTOSAVE_ADDRESS + autosaveIndex, value);
  if (++autosaveIndex > sizeof(StoredConfig) + 1) autosaveWriting = false;
}

bool loadAutosave() {
  if (EEPROM.read(AUTOSAVE_ADDRESS) != AUTOSAVE_MARK) return false;
  StoredConfig s;
  EEPROM.get(AUTOSAVE_ADDRESS + 1, s);
  if (EEPROM.read(AUTOSAVE_ADDRESS + 1 + sizeof(StoredConfig)) != checksum8((const byte *)&s, sizeof(StoredConfig))) return false;
  unpackConfig(s);
  return true;
}

void packConfig(StoredConfig &s) {
  memcpy(s.hits, currentConfig.hits, MAX_CHANNELS);
  memcpy(s.offset, currentConfig.offset, MAX_CHANNELS);
  memcpy(s.limit, currentConfig.limit, MAX_CHANNELS);
  memcpy(s.probability, currentConfig.probability, MAX_CHANNELS);

  for (byte c = 0; c < MAX_CHANNELS; c++) {
    s.offset[c] |= (barCond[c] ^ BAR_ALL) << 4;
    s.hits[c] |= (rndmCond[c] & 7) << 5;
    s.probability[c] |= (rndmCond[c] >> 3) << 7;
  }
  s.muteMask = 0;
  for (byte c = 0; c < MAX_CHANNELS; c++) {
    if (currentConfig.mute[c]) s.muteMask |= 1 << c;
  }
  s.xoxMask = xoxMask;
  memcpy(s.xox, xoxPattern, sizeof(xoxPattern));
  s.tempo = tempo;
  s.internalClock = internalClock;
}

void unpackConfig(const StoredConfig &s) {
  for (byte c = 0; c < MAX_CHANNELS; c++) {
    barCond[c] = (s.offset[c] >> 4) ^ BAR_ALL;
    rndmCond[c] = (s.hits[c] >> 5) | ((s.probability[c] >> 7) << 3);
    currentConfig.hits[c] = (s.hits[c] & 31) < MAX_PATTERNS ? (s.hits[c] & 31) : 0;
    currentConfig.offset[c] = (s.offset[c] & 15) % MAX_STEPS;
    currentConfig.limit[c] = s.limit[c];
    currentConfig.probability[c] = (s.probability[c] & 127) <= 100 ? (s.probability[c] & 127) : 100;
    currentConfig.mute[c] = s.muteMask & (1 << c);
  }
  cutMask = 0;
  xoxMask = s.xoxMask;
  memcpy(xoxPattern, s.xox, sizeof(xoxPattern));
  tempo = constrain(s.tempo, 40, 240);
  if (s.internalClock < 2) internalClock = s.internalClock;
  currentConfig.tempo = tempo;
  currentConfig.internalClock = internalClock;
  period = 60000 / tempo / 4;
  updateRythm();
  disp_refresh = true;
}

void handleSettingNavigation(int changeDirection) {
  switch (selected_setting) {
    case SETTING_HITS:
      leaveXox(selected_menu);
      currentConfig.hits[selected_menu] = (currentConfig.hits[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;
      break;
    case SETTING_OFFSET:
      currentConfig.offset[selected_menu] = (currentConfig.offset[selected_menu] - changeDirection + MAX_STEPS) % MAX_STEPS;
      break;
    case SETTING_LIMIT:
      currentConfig.limit[selected_menu] = (currentConfig.limit[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;
      break;
    case SETTING_RESET:
      playing_step[selected_menu] = 0;
      break;
    case SETTING_PROB:
      currentConfig.probability[selected_menu] = (currentConfig.probability[selected_menu] + changeDirection + 101) % 101;
      break;
    case SETTING_COND:
      barCond[selected_menu] = (barCond[selected_menu] + (changeDirection > 0 ? 1 : 15)) & 15;
      break;
    case SETTING_XOX: {
      byte len = currentConfig.limit[selected_menu];
      if (len == 0 || len > MAX_STEPS) len = MAX_STEPS;
      xoxCursor[selected_menu] = (xoxPos(selected_menu) + (changeDirection > 0 ? 1 : len - 1)) % len;
      break;
    }
  }
}

void saveToEEPROM(byte slot) {
  StoredConfig s;
  packConfig(s);
  EEPROM.put(EEPROM_START_ADDRESS + slot * CONFIG_SIZE, s);
  EEPROM.update(LAST_USED_SLOT_ADDRESS, slot);
  lastUsedSlot = slot;
}

void loadFromEEPROM(byte slot) {
  StoredConfig s;
  EEPROM.get(EEPROM_START_ADDRESS + slot * CONFIG_SIZE, s);
  unpackConfig(s);
  lastUsedSlot = slot;
}

void defaultConfig(StoredConfig &s) {
  memset(&s, 0, sizeof(s));
  memset(s.hits, 4, MAX_CHANNELS);
  memset(s.limit, MAX_STEPS, MAX_CHANNELS);
  memset(s.probability, 100, MAX_CHANNELS);
  s.tempo = 120;
}

bool slotUsed(byte slot) {
  int a = EEPROM_START_ADDRESS + slot * CONFIG_SIZE;
  for (byte i = 0; i < sizeof(StoredConfig); i++) {
    byte e = i < 6 ? 4 : i < 12 ? 0 : i < 18 ? MAX_STEPS : i < 24 ? 100 : i == 38 ? 120 : 0;
    if (EEPROM.read(a + i) != e) return true;
  }
  return false;
}

void writePresets() {
  if (EEPROM.read(PRESETS_FLAG_ADDRESS) == PRESETS_MARK) return;

  for (byte k = 0; k < NUM_MEMORY_SLOTS * 3; k++) EEPROM.update(NAMES_ADDRESS + k, '-');
  for (byte i = 0; i < NUM_PRESETS; i++) {
    if (slotUsed(i)) continue;
    StoredConfig s;
    memcpy_P(&s, &presets[i], sizeof(s));
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, s);
    for (byte k = 0; k < 3; k++) EEPROM.update(NAMES_ADDRESS + i * 3 + k, pgm_read_byte(&presetNames[i * 3 + k]));
  }
  EEPROM.update(PRESETS_FLAG_ADDRESS, PRESETS_MARK);
}

char nameChar(byte slot, byte k) {
  char c = EEPROM.read(NAMES_ADDRESS + slot * 3 + k);
  return strchr_P(NAME_CHARS, c) && c ? c : '-';
}

void leaveSyst() {
  systMode = false;
  nav = NAV_CH;
  syncNav();
}

void finishName() {
  if (nameEdit) {
    for (byte k = 0; k < 3; k++) EEPROM.update(NAMES_ADDRESS + selected_slot * 3 + k, nameBuf[k]);
    nameEdit = 0;
  }
  showOverlay = false;
  leaveSyst();
}

void initializeDefaultRhythms() {
  StoredConfig s;
  defaultConfig(s);
  for (byte i = 0; i < NUM_MEMORY_SLOTS; i++) {
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, s);
  }
  EEPROM.update(LAST_USED_SLOT_ADDRESS, 0);
}

byte mutedMask() {
  byte m = 0;
  for (byte c = 0; c < MAX_CHANNELS; c++)
    if (currentConfig.mute[c]) m |= 1 << c;
  return m;
}

void setMutes(byte m) {
  for (byte c = 0; c < MAX_CHANNELS; c++) currentConfig.mute[c] = (m >> c) & 1;
  markChange();
  disp_refresh = true;
}

void resetSeq() {
  for (int k = 0; k < MAX_CHANNELS; k++) {
    playing_step[k] = 15;
  }
  barClock = 15;
  barIdx = 3;
}

void verticalMenu(byte x, byte y, char c1, char c2, char c3, char c4) {
  char c[4] = { c1, c2, c3, c4 };
  for (byte i = 0; i < 4; i++) {
    display.setCursor(x, y + 9 * i);
    display.print(c[i]);
  }
}

void leftMenu(char c1, char c2, char c3, char c4) {
  verticalMenu(0, 30, c1, c2, c3, c4);
}

void rightGlyph(byte row, char c, bool inverted) {
  byte y = 9 * row;
  if (inverted) {
    display.fillRect(119, y - 1, 7, 9, WHITE);
    display.setTextColor(BLACK);
  }
  display.setCursor(120, y);
  display.print(c);
  display.setTextColor(WHITE);
}

void drawRightColumn() {
  if (showOverlay || systMode) return;
  if (muteMode || rndmMode || condMode) {
    rightGlyph(0, '1' + (muteMode ? muteCursor : rndmMode ? rndmCursor : selected_menu), true);
    return;
  }
  if (nav >= NAV_COND) return;
  rightGlyph(0, '1' + selected_menu, nav == NAV_CH);
  rightGlyph(1, 'H', nav == NAV_HITS);
  rightGlyph(2, 'O', nav == NAV_OFFS);
}

void leftName(char c1, char c2, char c3, char c4, bool closed) {
  if (closed) {
    display.fillRect(0, 28, 7, 37, WHITE);
    display.setTextColor(BLACK);
  }
  verticalMenu(1, 30, c1, c2, c3, c4);
  display.setTextColor(WHITE);
}

void drawLeftColumn() {
  if (showOverlay || systMode) return;
  switch (nav) {
    case NAV_CH: drawBpmColumn(); break;
    case NAV_HITS: leftName('H', 'I', 'T', 'S', false); break;
    case NAV_OFFS: leftName('O', 'F', 'F', 'S', false); break;
    case NAV_LIMI: leftName('L', 'I', 'M', 'T', false); break;
    case NAV_RESET: leftName('R', 'S', 'E', 'T', false); break;
    case NAV_PROB: leftName('P', 'R', 'O', 'B', false); break;
    case NAV_COND: leftName('C', 'O', 'N', 'D', true); break;
    case NAV_XOX: leftName('X', 'O', 'X', ' ', true); break;
    case NAV_RNDM: leftName('R', 'N', 'D', 'M', true); break;
    case NAV_MUTE: leftName('M', 'U', 'T', 'E', true); break;
    case NAV_RSET: leftName('R', 'S', 'E', 'T', true); break;
    case NAV_SYST: leftName('S', 'Y', 'S', 'T', true); break;
  }
}

void checkAndInitializeSettings() {
  char magic[sizeof(FIRMWARE_MAGIC)];
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);

  if (strncmp(magic, FIRMWARE_MAGIC, sizeof(FIRMWARE_MAGIC)) == 0) return;

  initializeDefaultRhythms();
  EEPROM.update(AUTOSAVE_ADDRESS, 0);
  EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
}

void drawStepDots(const SlotConfiguration &currentConfig) {
  if (showOverlay) return;
  for (int k = 0; k < MAX_CHANNELS; k++) {
    for (int j = 0; j < currentConfig.limit[k]; j++) {
      int x_pos = pgm_read_byte(&x16[j % 16]) + graph_x[k];
      int y_pos = pgm_read_byte(&y16[j % 16]) + graph_y[k];
      if (x_pos < 128 && y_pos < 64 && currentConfig.mute[k] == 0) {
        display.drawPixel(x_pos, y_pos, WHITE);
      }
    }
  }
}

void OLED_display() {
  static uint32_t last_refresh = 0;
  uint32_t current_time = millis();

  if (!force_refresh && (!disp_refresh || (current_time - last_refresh < MIN_REFRESH_DURATION))) {
    return;
  }

  disp_refresh = false;
  force_refresh = false;
  last_refresh = current_time;

  display.clearDisplay();

  drawMainScreen();

  if (showOverlay) {
    drawOverlay();
  }

  display.display();
}

void drawMainScreen() {
  drawRightColumn();
  drawLeftColumn();

  if (systMode && !showOverlay) {
    drawSystList();
    return;
  }

  if (selected_setting == SETTING_PROB) {
    drawProbabilityConfig();
  } else {
    drawStepDots(currentConfig);
    drawEuclideanRhythms();
    drawXoxCursor();
    if (nav == NAV_COND) drawBarStickers(barCond, selected_menu);
    else if (nav == NAV_RNDM && !showOverlay) drawBarStickers(rndmCond, rndmMode ? rndmCursor : -1);
    if (muteMode) {
      if (currentConfig.mute[muteCursor]) {
        display.drawCircle(graph_x[muteCursor] + 15, graph_y[muteCursor] + 15, 11, WHITE);
      } else {
        drawSticker(muteCursor, false, 'M');
      }
    }
  }
}

#ifndef STICKER_Q
#define STICKER_Q 4  // quarter side in px; the sticker is 2 * STICKER_Q + 1 px
#endif
void drawBarStickers(const byte *conds, int8_t selected) {
  for (byte ch = 0; ch < MAX_CHANNELS; ch++) {
    int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
    display.fillRect(cx - STICKER_Q - 1, cy - STICKER_Q - 1, 2 * STICKER_Q + 3, 2 * STICKER_Q + 3, BLACK);
    byte m = pgm_read_byte(&barMask[conds[ch]]);
    for (byte i = 0; i < 4; i++) {
      int qx = cx - STICKER_Q + (i & 1) * (STICKER_Q + 1), qy = cy - STICKER_Q + (i >> 1) * (STICKER_Q + 1);
      if ((m >> i) & 1) display.fillRect(qx, qy, STICKER_Q, STICKER_Q, WHITE);
      else display.drawRect(qx, qy, STICKER_Q, STICKER_Q, WHITE);
    }
    if (ch == selected) display.drawCircle(cx, cy, STICKER_Q + 6, WHITE);
  }
}

void drawXoxCursor() {
  if (showOverlay || selected_setting != SETTING_XOX || selected_menu > MENU_CH_6) return;
  byte ch = selected_menu;
  byte pos = xoxPos(ch);
  if (stepOn(ch, pos) && (millis() & 512)) return;
  int x = pgm_read_byte(&x16[pos]) + graph_x[ch];
  int y = pgm_read_byte(&y16[pos]) + graph_y[ch];
  for (int8_t d = -2; d <= 2; d++) {
    byte half = 2 - abs(d);
    display.drawFastHLine(x - half, y + d, half * 2 + 1, WHITE);
  }
}

void drawOverlay() {
  if (!systMode) return;
  if (sysItem < 2) drawSaveLoadSelection();
  else if (sysItem == 2) drawClock();
  else drawTempo();
}

void drawSystList() {
  for (byte i = 0; i < 4; i++) {
    int y = 9 + i * 12;
    bool sel = i == sysItem;
    if (sel) display.fillRect(8, y - 2, 112, 11, WHITE);
    display.setTextColor(sel ? BLACK : WHITE);
    textAt(14, y);
    switch (i) {
      case 0: display.print(FS(sSAVE)); break;
      case 1: display.print(FS(sLOAD)); break;
      case 2: display.print(FS(sCLOCK)); break;
      case 3: display.print(F("BPM")); break;
    }
    if (i >= 2) {
      textAt(86, y);
      if (i == 2) display.print(internalClock ? FS(sINT) : FS(sEXT));
      else display.print(internalClock ? tempo : externalBPM);
    }
  }
  display.setTextColor(WHITE);
}

void drawClock() {
  textAt(0, 0);
  display.print(FS(sCLOCK));
  textAt(0, 14, 3);
  display.print(internalClock ? FS(sINT) : FS(sEXT));
  textAt(0, 44);
  display.print(F("TURN INT OR EXT"));
  textAt(0, 55);
  display.print(F("CLICK CLOSE"));
}

void drawEuclideanRhythms() {
  if (showOverlay) return;

  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] || selected_setting == SETTING_LIMIT) continue;
    int8_t first = -1, prev = -1;
    for (byte m = 0; m < MAX_STEPS; m++) {
      if (!stepOn(k, m)) continue;
      if (prev < 0) first = m;
      else display.drawLine(pgm_read_byte(&x16[prev]) + graph_x[k], pgm_read_byte(&y16[prev]) + graph_y[k], pgm_read_byte(&x16[m]) + graph_x[k], pgm_read_byte(&y16[m]) + graph_y[k], WHITE);
      prev = m;
    }
    if (first >= 0) {

      byte x0 = pgm_read_byte(&x16[first]) + graph_x[k], y0 = pgm_read_byte(&y16[first]) + graph_y[k];
      if (first == prev) display.drawLine(15 + graph_x[k], 15 + graph_y[k], x0, y0, WHITE);
      else display.drawLine(x0, y0, pgm_read_byte(&x16[prev]) + graph_x[k], pgm_read_byte(&y16[prev]) + graph_y[k], WHITE);
    }
  }

  for (int k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] == 0 && selected_setting != SETTING_PROB) {
      int px = pgm_read_byte(&x16[playing_step[k]]) + graph_x[k], py = pgm_read_byte(&y16[playing_step[k]]) + graph_y[k];
      if (stepOn(k, playing_step[k])) display.fillCircle(px, py, 3, WHITE);
      else display.drawCircle(px, py, 2, WHITE);
    }
  }

  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] && selected_setting != SETTING_LIMIT && nav != NAV_RNDM && nav != NAV_COND)
      drawSticker(k, true, 'M');
  }

  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;

    byte nHits = isXox(ch) ? __builtin_popcount(xoxPattern[ch]) : currentConfig.hits[ch];
    if (currentConfig.mute[ch] == 0 && nHits > 4 && selected_setting != SETTING_LIMIT && nav != NAV_RNDM && nav != NAV_COND) {
      drawRingNumber(ch, nHits);
    }

    if (selected_setting == SETTING_LIMIT) drawRingNumber(ch, currentConfig.limit[ch]);
  }
}

void drawRingNumber(byte ch, byte n) {
  numAt(graph_x[ch] + (n > 9 ? 10 : 13), graph_y[ch] + 12);
  display.print(n);
}

void drawSticker(byte ch, bool filled, char letter) {
  int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
  display.fillCircle(cx, cy, 9, filled ? WHITE : BLACK);
  display.drawCircle(cx, cy, 9, WHITE);
  display.setTextColor(filled ? BLACK : WHITE);
  textAt(cx - 2, cy - 3);
  display.print(letter);
  display.setTextColor(WHITE);
}

void drawBpmColumn() {
  int bpm = internalClock ? tempo : externalBPM;
  char d[5] = "---";
  if (bpm > 0) itoa(bpm, d, 10);
  leftMenu(d[0], d[1], d[2] ? d[2] : ' ', ' ');
}

void drawProbabilityConfig() {
  for (byte ch = 0; ch < MAX_CHANNELS; ch++) {
    int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
    byte p = currentConfig.probability[ch];

    byte pos = p * 64 / 25 > 255 ? 255 : p * 64 / 25;
    byte k = pos >> 4, fr = pos & 15, k2 = (k + 1) & 15;
    int vx = ((int)pgm_read_byte(&x16[k]) - 15) * (16 - fr) + ((int)pgm_read_byte(&x16[k2]) - 15) * fr;
    int vy = ((int)pgm_read_byte(&y16[k]) - 15) * (16 - fr) + ((int)pgm_read_byte(&y16[k2]) - 15) * fr;
    for (int8_t y = -14; y <= 14; y++) {
      for (int8_t x = -14; x <= 14; x++) {
        int d = x * x + y * y;
        if (d > 196 || d <= 144) continue;
        bool in;
        if (p >= 100) in = true;
        else {
          bool cw = (long)x * vy - (long)y * vx > 0;
          in = p <= 50 ? (x >= 0 && cw) : (x >= 0 || cw);
        }
        if (in && p) display.drawPixel(cx + x, cy + y, WHITE);
      }
    }
    display.drawCircle(cx, cy, 14, WHITE);
    display.drawCircle(cx, cy, 12, WHITE);
    if (ch == selected_menu) display.drawCircle(cx, cy, 16, WHITE);
    numAt(cx - (p >= 100 ? 9 : p >= 10 ? 6 : 3), cy - 3);
    display.print(p);
  }
}

byte glyphField(char c, byte field) {
  return pgm_read_byte((const byte *)&Font4x7Glyphs[c - 0x20] + field);
}

void drawSaveLoadSelection() {
  textAt(0, 0);
  display.print(sysItem == 0 ? F("SAVE TO SLOT") : F("LOAD FROM SLOT"));
  {
    numAt(0, 14, 2);
    display.print(selected_slot + 1, DEC);

    bool used = slotUsed(selected_slot);
    if (used) display.fillCircle(32, 20, 4, WHITE);
    else display.drawCircle(32, 20, 4, WHITE);
    textAt(44, 14, 2);
    for (byte k = 0; k < 3; k++) display.print(nameEdit ? nameBuf[k] : nameChar(selected_slot, k));
    if (nameEdit) {
      byte x = 44;
      for (byte k = 0; k < nameEdit - 1; k++) x += 2 * glyphField(nameBuf[k], offsetof(GFXglyph, xAdvance));
      display.drawFastHLine(x, 31, 2 * glyphField(nameBuf[nameEdit - 1], offsetof(GFXglyph, width)), WHITE);
    }
    textAt(0, 35);
    if (nameEdit) display.print(F("NAME TURN CLICK"));
    else if (confirming) {
      display.print(F("CANCEL"));
      textAt(40, 35);
      display.print(sysItem == 0 ? FS(sSAVE) : FS(sLOAD));
      display.drawFastHLine(confirming == 2 ? 40 : 0, 44, confirming == 2 ? 19 : 29, WHITE);
    } else {
      display.print(F("CLICK TO "));
      display.print(sysItem == 0 ? (used ? F("OVERWRITE") : FS(sSAVE)) : FS(sLOAD));
    }
  }
}

void drawTempo() {
  int16_t x1 = 10, y1 = 10;
  uint16_t w = 108, h = 44;
  uint16_t b = 4;
  uint16_t b2 = 8;

  if (internalClock) {
    textAt(0, 0);
    display.print(F("DIAL IN TEMPO"));

    int filledWidth = (long)(tempo - 40) * (w - 22) / (240 - 40);

    display.drawRoundRect(x1 + 10, y1 + h / 2 - 5, w - 20, 10, 2, WHITE);

    display.fillRoundRect(x1 + 11, y1 + h / 2 - 3, filledWidth, 6, 2, WHITE);

    numAt(12, 45);
    display.print(F("40"));

    numAt(50, 45);
    display.print(tempo);

    numAt(95, 45);
    display.print(F("240"));

  } else {
    if (externalBPM == 0) {
      textAt(0, 0);
      display.print(F("PATCH CLK OR"));
      textAt(0, 13);
      display.print(F("USE CLOCK FOR"));
      textAt(0, 26);
      display.print(F("INT CLK"));
    } else {
      textAt(0, 0);
      display.print(F("EXT BPM"));
      numAt(0, 13);
      display.print(externalBPM, DEC);
    }
  }
}

void updateRythm() {
  for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
    uint8_t hits = currentConfig.hits[i];
    uint8_t offset = currentConfig.offset[i];
    uint16_t m = 0;
    for (uint8_t j = 0; j < MAX_STEPS; ++j) {
      uint8_t s = (j + offset) % MAX_STEPS;
      if (isXox(i) ? (xoxPattern[i] >> s) & 1 : pgm_read_byte(&euc16[hits][s])) m |= 1u << j;
    }
    offset_buf[i] = m;
  }
}
