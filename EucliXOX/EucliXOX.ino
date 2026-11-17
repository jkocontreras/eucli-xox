/**
 * @file EucliXOX.ino (based on ARYTHMATIK_Euclid.ino)
 * @author Modulove & friends
 * EUCLID + XOX modifications by Joaquín Contreras, 2026: XOX step mode, autosaved state,
 * mute mode, presets in slots, slot names. https://github.com/jkocontreras/eucli-xox
 * @brief 6CH Eurorack (HAGIWO) euclidean Rhythm Generator
 * @version 0.2
 * @date 2024-04-23
 *
 * @copyright Copyright (c) 2025
 *
 * Usage:
 *
 *  Connect clock source to the CLK input and each output will output triggers according to settings set in the UI
 * 
 * Features:
 * - Interactive OLED menu for control and visualization.
 * - Rotary encoder for parameter adjustments and easy menu navigation.
 * - Reset through dedicated input and button press (Also possible individually per channel)
 * - Random auto advance, Save / Load State
 *
 * Hardware:
 * - Clock input (CLK) for timing triggers.
 * - Input: Clock (CLK), Reset (RST) and Rotary Encoder (with Button).
 * - Output: 6 Trigger Channels with LED indicators + CLK LED
 *
 * Encoder:
 * - Short press: Toggle between menue options (right side) & left menue options (channel options)
 * - rotate CW: dial in individual channel values for Hits, Offset, Limit, Mute. Reset channel, randomize channel
 * - rotate CCW: quick access menue for sequence Reset & Mute
 *
 */

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EncoderButton.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

void textAt(int x, int y, byte size = 1);  // see initDisplay()
void numAt(int x, int y, byte size = 1);

// Configuration flags
//#define ENCODER_REVERSED  // uncomment (or compile with -DENCODER_REVERSED) if the encoder turns the wrong way
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

#if defined(__LGT8FX8P__)
#define LGT8FX_BOARD
#endif

#ifdef LGT8FX_BOARD
#define NUM_MEMORY_SLOTS 10
#else
#define NUM_MEMORY_SLOTS 20
#endif

#ifdef ROTATE_PANEL
// When panel is rotated
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
// When panel is not rotated
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

// Additional Pins
const byte ENCODER_PIN1 = 2;
const byte ENCODER_PIN2 = 3;
const byte ENCODER_SW_PIN = 12;

//#define DEBUG  // Uncomment for enabling debug print to serial monitoring output. Note: this affects performance and locks LED 4 & 5 on HIGH.

// Top level menu for selecting a channel or global settings.
enum TopMenu {
  MENU_CH_1,
  MENU_CH_2,
  MENU_CH_3,
  MENU_CH_4,
  MENU_CH_5,
  MENU_CH_6,
  MENU_RANDOM_ADVANCE,
  MENU_RAND,
  MENU_SAVE,
  MENU_LOAD,
  MENU_TEMPO,
  MENU_ALL_RESET,
  MENU_ALL_MUTE,
  MENU_LAST
};

// Enum for individual channel settings.
enum Setting {
  SETTING_TOP_MENU,
  SETTING_HITS,
  SETTING_OFFSET,
  SETTING_LIMIT,
  SETTING_RESET,
  SETTING_RANDOM,
  SETTING_PROB,
  SETTING_XOX,  // XOX mode: turn moves the cursor, click toggles the step, long click leaves
  SETTING_LAST
};

// For debug / UI
// EUCLID11: compact slot format with XOX patterns. EUCLID10 slots are converted at power-up.
#define FIRMWARE_MAGIC "EUCLID11"
#define LEGACY_MAGIC "EUCLID10"
#define FIRMWARE_MAGIC_ADDRESS 0  // Store firmware magic (read with debug fw!)

// OLED
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// EEPROM
#define EEPROM_START_ADDRESS 20
#define CONFIG_SIZE (sizeof(StoredConfig))
#define LAST_USED_SLOT_ADDRESS (EEPROM_START_ADDRESS + NUM_MEMORY_SLOTS * CONFIG_SIZE)
// Autosaved working state: the last state is saved a few seconds after the last change.
// Layout: a mark (1 byte) + StoredConfig + a checksum byte.
#define AUTOSAVE_ADDRESS (LAST_USED_SLOT_ADDRESS + 2)
#define AUTOSAVE_MARK 0xA5
#define AUTOSAVE_DELAY_MS 10000UL  // time without encoder actions before saving

// Timing
bool trg_in = false, old_trg_in = false, rst_in = false, old_rst_in = false;

byte playing_step[6] = { 0 };

// display Menu and UI
// Select settings menu and channel menu
TopMenu selected_menu = MENU_CH_1;
Setting selected_setting = SETTING_TOP_MENU;
byte selected_slot = 0;
bool disp_refresh = true;
bool force_refresh = true;
unsigned long last_refresh = 0;
// Global cut: hold the encoder on M (outside mute mode).
// 0.4 s mutes all (or restores the previous mix if a cut is active), 2 s blinks all, 4 s unmutes all.
byte cutMask = 0;    // channels muted by the cut (0 = no active cut)
byte blinkMask = 0;  // stickers that blink
byte holdStage = 0;
unsigned long cutTime = 0;
// Mute mode on M: turn chooses a channel, click mutes/unmutes, long click leaves
bool muteMode = false;
byte muteCursor = 0;
bool internalClock = false;
bool showOverlay = false;

int tempo = 120;                 // beats per minute.
int period = 60000 / tempo / 4;  // one minute in ms divided by tempo divided by 4 for 16th note period.
// BPM derived from the external clock pulses
int externalBPM = 0;

//const byte graph_x[6] PROGMEM = { 0, 40, 80, 15, 55, 95 }, graph_y[6] PROGMEM = { 0, 0, 0, 32, 32, 32 };
const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 }, graph_y[6] = { 0, 0, 0, 32, 32, 32 };


// In PROGMEM to leave more RAM for the stack (read with pgm_read_byte)
const byte x16[16] PROGMEM = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, y16[16] PROGMEM = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//Sequence variables
constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;  // 250;  // 125;  // Used by fast inputs like encoder rotation to throttle the display refresh. (3 steps)
unsigned long gate_timer = 0;
#define LED_FLASH_MS 5  // LED flash (channels and clock); shorter looks dimmer (original: 30)
#define LONG_CLICK_MS 375  // long click duration
#define TRIG_MS 25      // output trigger length in code. The screen redraws after the LED flash and takes
                        // ~25 ms, so in practice triggers last ~30 ms, as before.
unsigned long last_clock_input = 0;
unsigned long internalClockTimer = 0;

const static byte euc16[MAX_PATTERNS][MAX_STEPS] PROGMEM = {  //euclidian rythm
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
uint16_t offset_buf[MAX_CHANNELS];  // steps that sound per channel (bit j = step j), with OFFS applied. 12 bytes of RAM instead of 96.
#define stepOn(ch, j) ((offset_buf[ch] >> ((j) & 15)) & 1)

// random assign
const PROGMEM uint8_t hit_occ[MAX_CHANNELS] = { 5, 1, 20, 20, 40, 80 };
const PROGMEM uint8_t off_occ[MAX_CHANNELS] = { 1, 3, 20, 30, 40, 20 };
const PROGMEM uint8_t mute_occ[MAX_CHANNELS] = { 0, 2, 20, 20, 20, 20 };
const PROGMEM uint8_t hit_rng_max[MAX_CHANNELS] = { 6, 5, 8, 4, 4, 6 };
const PROGMEM uint8_t hit_rng_min[MAX_CHANNELS] = { 3, 2, 2, 1, 1, 1 };
const int bar_max[MAX_CHANNELS] PROGMEM = { 2, 4, 6, 8, 12, 16 };  // control

byte bar_now = 1;
byte bar_select = 1;  // ToDo: selected bar needs to be saved as well!
byte step_cnt = 0;

// Display Setup
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// reverse encoder
#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#endif

// State of the rhythm that is playing (in RAM)

struct SlotConfiguration {
  byte hits[MAX_CHANNELS];         // Number of hits per pattern in each channel
  byte offset[MAX_CHANNELS];       // Step offset for each channel
  bool mute[MAX_CHANNELS];         // Mute status for each channel
  byte limit[MAX_CHANNELS];        // Step limit (length) of the pattern for each channel
  byte probability[MAX_CHANNELS];  // Probability of triggering each hit (0-100%)
  int tempo;                       // Tempo
  bool internalClock;              // Clock source state
};

SlotConfiguration currentConfig;
byte lastUsedSlot = 0;

// XOX pattern per channel: bit s = step s on (before OFFS is applied).
// Bit i of xoxMask = channel i plays its XOX pattern instead of the euclidean one.
uint16_t xoxPattern[MAX_CHANNELS];
byte xoxMask = 0;
byte xoxCursor[MAX_CHANNELS];

// Compact EEPROM format (41 bytes).
struct StoredConfig {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  byte probability[MAX_CHANNELS];
  byte muteMask;
  byte xoxMask;
  uint16_t xox[MAX_CHANNELS];
  int tempo;
  byte internalClock;  // 0 external, 1 internal, 2 = loading does not change the clock (presets)
};

// EUCLID10 format (46 bytes per slot), only used to convert old slots.
struct LegacySlot {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  bool mute[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  byte probability[MAX_CHANNELS];
  char name[10];
  int tempo;
  bool internalClock;
  byte lastUsedSlot;
  byte selectedPreset;
  bool lastLoadedFromPreset;
};
#define LEGACY_SLOT_SIZE (sizeof(LegacySlot))
#define LEGACY_LAST_USED_SLOT_ADDRESS (EEPROM_START_ADDRESS + NUM_MEMORY_SLOTS * LEGACY_SLOT_SIZE)

// Autosave state
unsigned long lastChangeTime = 0;
bool autosavePending = false;
bool autosaveWriting = false;
byte autosaveIndex = 0;

// 3-letter slot names and the presets-written mark, after the autosave area
#define NAMES_ADDRESS (AUTOSAVE_ADDRESS + sizeof(StoredConfig) + 2)
#define PRESETS_FLAG_ADDRESS (NAMES_ADDRESS + NUM_MEMORY_SLOTS * 3)
#define PRESETS_MARK 0x5D
const char NAME_CHARS[] PROGMEM = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NUM_NAME_CHARS 38
byte nameEdit = 0;  // 0 = not naming; 1..3 = letter being edited
char nameBuf[3];

// Presets written once into slots 1 to 9 (only where the slot holds the initial rhythm or the unchanged
// old factory preset). From simple to complex: EUC, BOS and CUB in eucli mode (16 steps, they show HITS
// and OFFS); DEM, CMB and BRK drawn in XOX (CMB and BRK mix in eucli channels); TER (12 steps) and POL
// (different lengths) show LIMI.
// Channel order: 1 kick, 2 snare/clap, 3 closed hat, 4 open hat/cymbal, 5 percussion, 6 percussion/accent.
const StoredConfig presets[] PROGMEM = {
  // EUC: HITS: 2 to 7 hits, almost regular shapes
  { { 2, 3, 4, 5, 6, 7 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },
  // BOS: OFFS: bossa clave (5) and samba (7), rotated
  { { 4, 5, 8, 4, 7, 9 }, { 0, 12, 0, 2, 0, 10 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },
  // CUB: tresillo (6 in 16) and cinquillo (10 in 16)
  { { 4, 6, 8, 4, 10, 2 }, { 0, 5, 0, 2, 3, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 100, 2 },
  // DEM: dembow
  { { 4, 4, 8, 4, 6, 4 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x1111, 0x4848, 0x5555, 0x4444, 0x4949, 0x4141 }, 95, 2 },
  // CMB: classic Colombian cumbia (by JKO), channel 5 is a cowbell
  { { 4, 4, 8, 12, 4, 3 }, { 0, 14, 0, 0, 0, 6 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x2C, { 0x0101, 0x4444, 0x5555, 0xDDDD, 0x40C0, 0x0181 }, 95, 2 },
  // BRK: breakbeat by JKO
  { { 4, 2, 3, 4, 5, 9 }, { 0, 10, 0, 10, 11, 4 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 87, 60 }, 0, 0x0F, { 0x0103, 0x8100, 0x05A5, 0x1D11, 0x4208, 0x0241 }, 132, 2 },
  // TER: 12 steps, triple pulse with a bembé bell
  { { 4, 2, 12, 4, 7, 2 }, { 0, 0, 0, 0, 0, 0 }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x0249, 0x0208, 0x0FFF, 0x0924, 0x0AD5, 0x0041 }, 90, 2 },
  // POL: polymeter with probability
  { { 4, 3, 5, 4, 2, 5 }, { 0, 0, 0, 0, 0, 0 }, { 16, 7, 11, 9, 5, 13 }, { 100, 100, 80, 60, 70, 50 }, 0, 0x3F, { 0x1111, 0x0029, 0x02A9, 0x00A9, 0x0009, 0x0949 }, 120, 2 },
  // DIV: clock divider (utility)
  { { 16, 8, 4, 2, 1, 4 }, { 0, 0, 0, 0, 0, 2 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0, { 0 }, 120, 2 },
};
const char presetNames[] PROGMEM = "EUCBOSCUBDEMCMBBRKTERPOLDIV";
#define NUM_PRESETS (sizeof(presets) / sizeof(StoredConfig))

// Factory presets of the original Euclid, stored in slots 1 to 9 (hits, offsets, lengths)
const byte oldFactory[9][18] PROGMEM = {
  { 4, 4, 7, 4, 4, 4, 0, 2, 3, 2, 1, 0, 16, 16, 12, 8, 16, 16 },        // Techno
  { 4, 4, 4, 4, 4, 4, 0, 1, 0, 1, 0, 1, 16, 16, 16, 16, 16, 16 },       // Funk
  { 4, 4, 4, 4, 4, 4, 0, 1, 3, 1, 0, 2, 12, 12, 12, 12, 12, 12 },       // Swing
  { 3, 4, 3, 4, 3, 4, 0, 2, 3, 2, 0, 2, 8, 8, 8, 8, 8, 8 },             // Samba
  { 4, 3, 4, 3, 4, 3, 0, 2, 0, 2, 0, 2, 12, 12, 12, 12, 12, 12 },       // Waltz
  { 2, 2, 3, 3, 3, 3, 0, 2, 0, 2, 0, 2, 16, 16, 16, 16, 16, 16 },       // BossaN
  { 7, 7, 7, 7, 7, 7, 0, 2, 4, 6, 8, 10, 12, 12, 12, 12, 12, 12 },      // Bell
  { 2, 2, 3, 1, 2, 1, 0, 1, 0, 2, 1, 0, 24, 18, 24, 21, 16, 30 },       // Gen
  { 4, 4, 4, 4, 4, 4, 0, 1, 0, 2, 0, 3, 16, 16, 16, 16, 16, 16 },       // Gahu
};
byte autosaveCheck = 0;

// Boot splash, EUCLID + XOX, 80x36 px, 360 bytes. Generated from JKO's drawing (128x64 canvas)
const unsigned char Splash_Logo [] PROGMEM = {
  0x01, 0xc2, 0x02, 0x01, 0xc0, 0x70, 0x03, 0xfe, 0x1e, 0x00,
  0x03, 0x45, 0x05, 0x03, 0x60, 0x58, 0x02, 0x03, 0x13, 0x00,
  0x06, 0x75, 0x84, 0x86, 0x30, 0x4c, 0x02, 0x03, 0x11, 0x80,
  0x0c, 0xf5, 0xc4, 0xc4, 0x18, 0x5c, 0x03, 0xcf, 0x90, 0x40,
  0x19, 0xe5, 0xc4, 0xcc, 0xcc, 0x5c, 0x00, 0x4e, 0x12, 0x20,
  0x33, 0xc5, 0xc4, 0xc9, 0xe4, 0x5c, 0x00, 0x4c, 0x13, 0x18,
  0x67, 0x85, 0xc4, 0xd3, 0xfe, 0x5c, 0x00, 0x4c, 0x13, 0x8c,
  0xcf, 0xe4, 0xc4, 0xf3, 0x8e, 0xdc, 0x00, 0x4c, 0x13, 0x64,
  0x80, 0x24, 0xc4, 0xe7, 0x00, 0xdc, 0x00, 0x4c, 0x13, 0x26,
  0x80, 0x34, 0xc4, 0xe7, 0x00, 0xdc, 0x00, 0x4c, 0x13, 0xc7,
  0xc7, 0xfc, 0xc9, 0xe2, 0x08, 0xdc, 0x00, 0x4c, 0x13, 0x8f,
  0x67, 0x06, 0x59, 0xd3, 0x34, 0xdc, 0xe0, 0x4c, 0x13, 0x3e,
  0x33, 0x03, 0x33, 0x89, 0xe6, 0xdc, 0x90, 0x4c, 0x12, 0x78,
  0x19, 0x81, 0x07, 0x8c, 0xcf, 0x9f, 0x3b, 0xce, 0x10, 0xf0,
  0x0c, 0xc0, 0x87, 0x06, 0x1e, 0x80, 0x3a, 0x01, 0x11, 0xe0,
  0x06, 0x60, 0xce, 0x02, 0x3c, 0x4f, 0xf2, 0x03, 0x93, 0xc0,
  0x03, 0x60, 0x7e, 0x01, 0xf8, 0x3f, 0xf1, 0xff, 0x8f, 0x80,
  0x00, 0xf0, 0x0c, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0x01, 0xc1, 0xe0, 0x60, 0x30, 0x00,
  0x00, 0x00, 0x00, 0x02, 0x83, 0x41, 0x30, 0xd0, 0x48, 0x00,
  0x00, 0x00, 0x00, 0x02, 0x46, 0x63, 0x18, 0x98, 0xcc, 0x00,
  0x00, 0x00, 0x00, 0x02, 0x64, 0xf2, 0x08, 0x4d, 0x9c, 0x00,
  0x00, 0x03, 0x00, 0x01, 0x39, 0xe4, 0xcc, 0x67, 0x3c, 0x00,
  0x00, 0x07, 0x80, 0x01, 0x99, 0xcc, 0xe6, 0x36, 0x78, 0x00,
  0x00, 0x04, 0x80, 0x00, 0xc3, 0x89, 0xe6, 0x10, 0x70, 0x00,
  0x00, 0x1c, 0xe0, 0x00, 0x47, 0x91, 0xd3, 0x08, 0xe0, 0x00,
  0x00, 0x30, 0x30, 0x00, 0x47, 0x13, 0x93, 0x08, 0xc0, 0x00,
  0x00, 0x30, 0x30, 0x00, 0xc3, 0x19, 0x93, 0x90, 0x40, 0x00,
  0x00, 0x1c, 0xf8, 0x01, 0x99, 0x09, 0xa7, 0xb6, 0x60, 0x00,
  0x00, 0x04, 0xc0, 0x01, 0x3d, 0x8c, 0xe7, 0x67, 0x30, 0x00,
  0x00, 0x07, 0xc0, 0x02, 0x7c, 0xc4, 0x4e, 0xcf, 0x98, 0x00,
  0x00, 0x03, 0xc0, 0x04, 0x72, 0x62, 0x1e, 0x9e, 0xc8, 0x00,
  0x00, 0x00, 0x00, 0x04, 0xe3, 0x23, 0x1c, 0xbc, 0x4c, 0x00,
  0x00, 0x00, 0x00, 0x07, 0xe1, 0xf1, 0xb8, 0xf8, 0x2e, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xc0, 0xb0, 0xf8, 0x10, 0x1e, 0x00
};
const byte logoWidth = 80;
const byte logoHeight = 36;

// Boot splash: the image keeps its position from the original 128x64 canvas
void drawAnimation() {
  display.clearDisplay();
  display.drawBitmap(23, 15, Splash_Logo, logoWidth, logoHeight, WHITE);
  display.display();
}

// Function pointers for interrupt handlers
void (*ClockHandler)() = NULL;
void (*ResetHandler)() = NULL;

// Interrupt flags - set by ISR, cleared by main loop
volatile bool clockTriggerFlag = false;
volatile bool resetTriggerFlag = false;

// Previous pin states for edge detection in ISR
volatile bool prev_clk_state = false;
volatile bool prev_rst_state = false;

// No-operation function for default handlers
void noop() {
  // Do nothing
}

// Clock interrupt handler - called when clock rising edge is detected
void handleClockInterrupt() {
  clockTriggerFlag = true;
  last_clock_input = millis();
  
  // External clock BPM (pulses = 16th notes). Measured in microseconds with a moving average
  // of ~4 pulses and rounding, so a steady clock shows a steady number.
  // (The original measured whole ms and truncated: at 70 BPM it alternated 69 and 70.)
  static unsigned long lastPulseUs = 0;
  static unsigned long avgUs = 0;  // average interval between pulses, in µs
  unsigned long now = micros();
  unsigned long interval = now - lastPulseUs;
  if (lastPulseUs && interval < 2000000UL) {  // ignore the first pulse and gaps longer than 2 s
    avgUs = avgUs ? (avgUs * 3 + interval) / 4 : interval;
    externalBPM = (15000000UL + avgUs / 2) / avgUs;  // 60 s / 4 semicorcheas = 15.000.000 µs
    if (externalBPM > 360) externalBPM = 360;        // Limit BPM to avoid high speed issues
    period = avgUs / 1000;
  } else {
    avgUs = 0;
  }
  lastPulseUs = now;
}

// Reset interrupt handler - called when reset rising edge is detected
void handleResetInterrupt() {
  resetTriggerFlag = true;
}

// Pin Change Interrupt Service Routine for Port B (pins 8-13)
ISR(PCINT0_vect) {
  // Read current pin states
  bool current_clk_state, current_rst_state;
  
#ifdef ROTATE_PANEL
  // When panel is rotated: CLK=Pin11(PCINT3), RESET=Pin13(PCINT5)
  current_clk_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
  current_rst_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
#else
  // Normal orientation: CLK=Pin13(PCINT5), RESET=Pin11(PCINT3)
  current_clk_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
  current_rst_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
#endif

  // Detect rising edges and call appropriate handlers
  if (!prev_clk_state && current_clk_state && ClockHandler) {
    ClockHandler();
  }
  
  if (!prev_rst_state && current_rst_state && ResetHandler) {
    ResetHandler();
  }
  
  // Update previous states
  prev_clk_state = current_clk_state;
  prev_rst_state = current_rst_state;
}

// Function to setup pin change interrupts
void setupPinChangeInterrupts() {
  // Assign interrupt handlers
  if (ClockHandler == NULL) ClockHandler = handleClockInterrupt;
  if (ResetHandler == NULL) ResetHandler = handleResetInterrupt;

  // Disable interrupts during setup
  cli();
  
  // Read initial pin states
#ifdef ROTATE_PANEL
  prev_clk_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
  prev_rst_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
#else
  prev_clk_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
  prev_rst_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
#endif

  // Enable Pin Change Interrupt Control Register for Port B
  PCICR |= (1 << PCIE0);  // Enable PCIE0 (Port B)
  
#ifdef ROTATE_PANEL
  // Enable PCINT3 (Pin 11 - CLK) & PCINT5 (Pin 13 - RST)
  PCMSK0 |= (1 << PCINT3) | (1 << PCINT5);
#else
  // Enable PCINT5 (Pin 13 - CLK) & PCINT3 (Pin 11 - RST)
  PCMSK0 |= (1 << PCINT5) | (1 << PCINT3);
#endif

  // Re-enable interrupts
  sei();
}

void Random_change(bool includeMute, bool allChannels, byte select_ch = 0) {
  static constexpr uint8_t MAX_RANDOM = 100;
  for (uint8_t k = 0; k < MAX_CHANNELS; k++) {
    if (!allChannels && k != select_ch) continue;

    uint8_t random_value = random(1, MAX_RANDOM);
    if (pgm_read_byte(&hit_occ[k]) >= random_value) {
      xoxMask &= ~(1 << k);  // changing HITS returns the channel to eucli
      currentConfig.hits[k] = random(pgm_read_byte(&hit_rng_min[k]), pgm_read_byte(&hit_rng_max[k]) + 1);
    }
    if (pgm_read_byte(&off_occ[k]) >= random_value) {
      currentConfig.offset[k] = random(0, MAX_STEPS);
    }
    cutMask = 0;
    if (includeMute && k > 0) {
      currentConfig.mute[k] = pgm_read_byte(&mute_occ[k]) >= random_value;
    } else {
      currentConfig.mute[k] = false;
    }
  }
}

void onOverlayTimeout(EncoderButton &eb) {
  finishName();
  disp_refresh = true;
}

void setup() {

  setupEncoder();
  initIO();
  // Setup pin change interrupts AFTER initIO
  setupPinChangeInterrupts();
  initDisplay();

// boot logo animation only on nano for now
#if !defined(LGT8FX_BOARD) && !defined(DISABLE_BOOT_LOGO)
  drawAnimation();
  delay(1200);
#endif

  checkAndInitializeSettings();
  writePresets();

  // Load the last used slot from EEPROM
  EEPROM.get(LAST_USED_SLOT_ADDRESS, lastUsedSlot);
  if (lastUsedSlot >= NUM_MEMORY_SLOTS) {
    lastUsedSlot = 0;
  }
  selected_slot = lastUsedSlot;

  // Restore what was playing (autosave). Without a valid one, load the last used slot.
  if (!loadAutosave()) {
    loadFromEEPROM(lastUsedSlot);
  }

  updateRythm();
  OLED_display();

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  // Initialize the last external clock time to the current time
  last_clock_input = millis();
  internalClockTimer = millis();
}

void loop() {
  encoder.update();  // Process Encoder & button updates
  //display.fillScreen(BLACK);

  updateRythm();

  //-----------------trigger detect, reset & output----------------------

  bool beat_start = false;

  // Handle reset interrupt flag
  if (resetTriggerFlag) {
    resetTriggerFlag = false;  // Clear the flag
    resetSeq();
    disp_refresh = true;
  }

  // Handle clock interrupt flag (external clock)
  if (!internalClock && clockTriggerFlag) {
    clockTriggerFlag = false;  // Clear the flag
    beat_start = true;
  }

  // Handle internal clock
  if (internalClock && (millis() - internalClockTimer >= period)) {
    beat_start = true;
    internalClockTimer = millis();
    last_clock_input = millis();  // Update for clock detection
  }

  if (beat_start) {
    gate_timer = millis();
    force_refresh = true;
    CLK_LED::setOutput(1);
    for (int i = 0; i < MAX_CHANNELS; i++) {
      playing_step[i]++;
      if (playing_step[i] >= currentConfig.limit[i]) {
        playing_step[i] = 0;  // Step limit is reached
      }
    }

    // Output gate signal
    outputGateSignals();

    disp_refresh = true;  // Updates the display where the trigger was entered.

    // Random advance mode (mode 6)
    if (selected_menu == MENU_RANDOM_ADVANCE) {  // random mode setting
      step_cnt++;
      if (step_cnt >= 16) {
        bar_now++;
        step_cnt = 0;
        if (bar_now > pgm_read_word(&bar_max[bar_select])) {
          bar_now = 1;
          Random_change(true, true);
        }
      }
    }
  }

  if (gate_timer + TRIG_MS <= millis()) {  // turn the output triggers off
    OUTPUT1::setOutput(0);
    OUTPUT2::setOutput(0);
    OUTPUT3::setOutput(0);
    OUTPUT4::setOutput(0);
    OUTPUT5::setOutput(0);
    OUTPUT6::setOutput(0);
  }
  if (gate_timer + LED_FLASH_MS <= millis()) {  // turn the channel and clock LEDs off
    CLK_LED::setOutput(0);
    LED1::setOutput(0);
    LED2::setOutput(0);
    LED3::setOutput(0);
    LED4::setOutput(0);
    LED5::setOutput(0);
    LED6::setOutput(0);
  }

  // In XOX the screen refreshes when the cursor blink phase changes
  // (and faster while global cut stickers blink)
  if (selected_setting == SETTING_XOX || blinkMask) {
    static bool blinkPhase = false;
    bool phase = millis() & (blinkMask ? 128 : 512);
    if (phase != blinkPhase) {
      blinkPhase = phase;
      disp_refresh = true;
    }
  }

  globalCut();
  if (showOverlay && encoder.isPressed()) disp_refresh = true;  // long click line in S and L

  // Redrawing the screen takes ~25 ms and nothing can be switched off meanwhile:
  // wait until the LED flashes end, so they really last LED_FLASH_MS.
  if (millis() - gate_timer >= LED_FLASH_MS) OLED_display();

  autosaveStep();  // autosave
}

void setupEncoder() {
  encoder.setDebounceInterval(2);  // Increase debounce interval
  encoder.setMultiClickInterval(20);
  encoder.setRateLimit(100);
  encoder.setIdleTimeout(5000);
  encoder.setIdleHandler(onOverlayTimeout);
  encoder.setLongClickDuration(LONG_CLICK_MS);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);  // Add long click handler
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);  // Added handler again for pressed rotation
}

void initIO() {
  RESET::setInput();        // RST
  CLK::setInput();          // CLK
  OUTPUT1::setOutputLow();  // CH1
  OUTPUT2::setOutputLow();  // CH2
  OUTPUT3::setOutputLow();  // CH3
  OUTPUT4::setOutputLow();  // CH4
  OUTPUT5::setOutputLow();  // CH5
  OUTPUT6::setOutputLow();  // CH6
  // LED outputs
  LED1::setOutputLow();     // CH1 LED
  LED2::setOutputLow();     // CH2 LED
  LED3::setOutputLow();     // CH3 LED
  LED4::setOutputLow();     // CH6 LED
  LED5::setOutputLow();     // CH4 LED
  LED6::setOutputLow();     // CH5 LED
  CLK_LED::setOutputLow();  // CLK LED
}

// Text in the system font (x, y = top left corner).
void textAt(int x, int y, byte size) {
  display.setFont(NULL);
  display.setTextSize(size);
  display.setCursor(x, y);
}

// Numbers in the system font. The positions were laid out for a font whose digits started
// 1 px to the right, so x moves by one pixel per size step.
void numAt(int x, int y, byte size) {
  textAt(x + size, y, size);
}

void initDisplay() {
  delay(1000);  // Screen needs a sec to initialize ? How to fix ?
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  Wire.setClock(400000L);  // Set I2C clock to 400kHz to see if this improves perfomance ToDo: measure this!
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

// Check if the panel should be rotated
#ifdef ROTATE_PANEL
  display.setRotation(2);  // Adjust rotation for upside down mounting
#endif

#ifdef DEBUG
  // Display the firmware version and basic instructions when debugging
  display.println(F("ARYTHMATIK Euclid"));
  display.println(F("Firmware: v0.1"));
  display.println(F("6CH Euclidean Rhythm"));
  display.display();
  delay(1000);
#endif
}

void outputGateSignals() {
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (stepOn(i, playing_step[i]) && currentConfig.mute[i] == 0 && random(100) < currentConfig.probability[i]) {
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

void onEncoderClicked(EncoderButton &eb) {
  markChange();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;  // just in case

  if (nameEdit) {
    if (++nameEdit > 3) finishName();  // click: next letter; after the third, close
  } else if (showOverlay) {
    onOverlayTimeout(eb);  // Handle click to exit overlay
  } else {
    switch (selected_menu) {
      // Channel-specific actions
      case MENU_CH_1:
      case MENU_CH_2:
      case MENU_CH_3:
      case MENU_CH_4:
      case MENU_CH_5:
      case MENU_CH_6:
        if (selected_setting == SETTING_XOX) {
          toggleXoxStep(selected_menu);  // in XOX a click toggles the step under the cursor
        } else {
          // Click should only advance selected setting when a channel top menu is selected.
          selected_setting = static_cast<Setting>((selected_setting + 1) % SETTING_LAST);
        }
        break;
      // Mode-specific actions
      case MENU_ALL_RESET:
        resetSeq();
        break;
      case MENU_ALL_MUTE:
        if (!muteMode) {
          muteMode = true;   // enter mute mode
        } else {
          currentConfig.mute[muteCursor] = !currentConfig.mute[muteCursor];
          cutMask = 0;       // the mix stored by the cut no longer applies
        }
        break;
      case MENU_RAND:
        Random_change(false, true);
        break;
      case MENU_RANDOM_ADVANCE:
        Random_change(false, false);
        break;
      case MENU_SAVE:
      case MENU_LOAD:
      case MENU_TEMPO:
        showOverlay = !showOverlay;
        break;
    }
  }
}

// now toggles the mute status of the selected channel when in SETTING_TOP_MENU
void onEncoderLongClicked(EncoderButton &eb) {
  markChange();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;

  // If we're in the parameter settings (Hits, Offset, Limit, etc.) for one of the channels
  if (selected_menu <= MENU_CH_6 && selected_setting == SETTING_XOX) {
    selected_setting = SETTING_TOP_MENU;  // long click leaves XOX
  } else if (selected_menu <= MENU_CH_6 && selected_setting != SETTING_TOP_MENU) {
    // Decrement the selected menu to allow back navigation
    selected_setting = static_cast<Setting>((selected_setting - 1 + SETTING_LAST) % SETTING_LAST);
  } else if (showOverlay) {
    // Handle specific overlay actions
    switch (selected_menu) {
      case MENU_SAVE:
        if (nameEdit) {
          finishName();  // long click while naming: close
        } else {
          bool used = slotUsed(selected_slot);
          saveToEEPROM(selected_slot);
          for (byte k = 0; k < 3; k++) nameBuf[k] = used ? nameChar(selected_slot, k) : '-';
          nameEdit = 1;  // after saving, the slot is named
        }
        break;
      case MENU_LOAD:
        loadFromEEPROM(selected_slot);
        showOverlay = false;                         // Turn off the overlay
        break;
    }
  } else if (selected_menu == MENU_ALL_MUTE) {
    if (muteMode) {  // leave mute mode and jump to channel 1 (the next item when turning)
      muteMode = false;
      selected_menu = MENU_CH_1;
    }
    // outside the mode, the global cut is handled while held (in loop)
  } else if (selected_menu == MENU_TEMPO) {
    internalClock = !internalClock;  // Toggle the internal clock state
    showOverlay = true;              // Show overlay to indicate clock state change
    if (internalClock) period = 60000 / tempo / 4;
  } else if (selected_setting == SETTING_TOP_MENU && selected_menu <= MENU_CH_6) {
    // Mute the selected channel
    int channelIndex = selected_menu - MENU_CH_1;
    currentConfig.mute[channelIndex] = !currentConfig.mute[channelIndex];
    cutMask = 0;
  } 
}


void onEncoderRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;
  markChange();
  disp_refresh = true;

  if (muteMode) {  // in mute mode, turning moves the cursor one channel
    muteCursor = (muteCursor + (increment > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS;
    return;
  }

  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  // Only handle setting navigation if not all muted and the overlay is not shown.
  if (!showOverlay) {
    handleSettingNavigation(acceleratedIncrement);
    return;
  }

  // Overlay shown menu adjustments.
  if (selected_setting == SETTING_TOP_MENU && showOverlay) {
    switch (selected_menu) {
      case MENU_TEMPO:
        // Increment the tempo with the accelerated increment
        tempo += acceleratedIncrement;

        // Ensure the tempo stays within the range of 40 to 240 BPM
        if (tempo < 40) {
          tempo = 40;
        } else if (tempo > 240) {
          tempo = 240;
        }

        // Calculate the period based on the tempo
        period = 60000 / (tempo * 4);
        break;

      case MENU_SAVE:
      case MENU_LOAD:
        if (nameEdit) {  // turning changes the letter
          char *c = &nameBuf[nameEdit - 1];
          byte i = (const char *)strchr_P(NAME_CHARS, *c) - NAME_CHARS;
          *c = pgm_read_byte(&NAME_CHARS[(i + increment + NUM_NAME_CHARS) % NUM_NAME_CHARS]);
        } else {
          selected_slot = (selected_slot + increment + NUM_MEMORY_SLOTS) % NUM_MEMORY_SLOTS;
        }
        break;

      default:
        // If no valid menu is selected, do nothing.
        break;
    }
  }
}

void onEncoderPressedRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;
  markChange();
  disp_refresh = true;

  int acceleratedIncrement = increment * increment;  // Squaring the increment for quicker adjustments
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  // Handle specific modes when not in MENU_ALL_MUTE
  switch (selected_menu) {
    case MENU_CH_1:
    case MENU_CH_2:
    case MENU_CH_3:
    case MENU_CH_4:
    case MENU_CH_5:
    case MENU_CH_6:
      if (selected_setting == SETTING_TOP_MENU) {
        // Adjust the Hits value for the selected channel to quickly edit the beat/rhythm
        leaveXox(selected_menu);
        currentConfig.hits[selected_menu] = (currentConfig.hits[selected_menu] + increment + 17) % 17;
      } else {
        // Handle channel switching when in specific modes
        selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
        // Ensure the selected_menu is within the range of channels
        if (selected_menu > MENU_CH_6) {
          selected_menu = MENU_CH_1;
        }
      }
      break;

    case MENU_RAND:
      // Check rotation direction to call appropriate random change function
      if (increment > 0) {
        Random_change(false, true);  // Rotate CW: Random change without mute
      } else {
        Random_change(true, true);  // Rotate CCW: Random change with mute
      }
      break;

    case MENU_RANDOM_ADVANCE:
      // Cycle length: index 0..5 of bar_max = 2, 4, 6, 8, 12, 16 bars.
      // (The original went from 1 to 6: it never reached 2 bars and at 6 read outside the table.)
      bar_select = (bar_select + (increment > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS;
      break;

    default:
      // Handle other settings or ignore
      if (selected_setting != SETTING_TOP_MENU) {
        selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
        // Ensure the selected_menu is within the range of channels
        if (selected_menu > MENU_CH_6) {
          selected_menu = MENU_CH_1;
        }
      }
      break;
  }
}


//---------------------------- XOX ----------------------------

bool isXox(byte ch) {
  return xoxMask & (1 << ch);
}

// Cursor position within the channel length (LIMI)
byte xoxPos(byte ch) {
  byte len = currentConfig.limit[ch];
  if (len == 0 || len > MAX_STEPS) len = MAX_STEPS;
  return xoxCursor[ch] % len;
}

// Back to eucli. HITS starts from the number of steps the drawing had.
void leaveXox(byte ch) {
  if (!isXox(ch)) return;
  xoxMask &= ~(1 << ch);
  currentConfig.hits[ch] = __builtin_popcount(xoxPattern[ch]);
}

// Toggle the step under the cursor. The first time, the channel switches to XOX
// and the drawing starts from the euclidean rhythm that is playing.
void toggleXoxStep(byte ch) {
  if (!isXox(ch)) {
    uint16_t p = 0;
    for (byte s = 0; s < MAX_STEPS; s++) {
      if (pgm_read_byte(&euc16[currentConfig.hits[ch]][s])) p |= 1u << s;
    }
    xoxPattern[ch] = p;
    xoxMask |= 1 << ch;
  }
  // The cursor moves over the steps as they sound; OFFS rotates the drawing.
  xoxPattern[ch] ^= 1u << ((xoxPos(ch) + currentConfig.offset[ch]) % MAX_STEPS);
}

//------------------------- Autosave -------------------------

byte checksum8(const byte *p, byte n) {
  byte c = 0x5A;
  while (n--) c = ((c << 1) | (c >> 7)) ^ *p++;
  return c;
}

void markChange() {
  lastChangeTime = millis();
  autosavePending = true;
  autosaveWriting = false;  // if it was writing, start again after the delay
}

// Called on every loop() pass. Writes one byte per pass, only when the EEPROM is ready,
// so the rhythm keeps running. EEPROM.update only writes bytes that changed.
void autosaveStep() {
  if (!autosaveWriting) {
    if (!autosavePending || millis() - lastChangeTime < AUTOSAVE_DELAY_MS) return;
    autosaveCheck = 0x5A;  // checksum8 is computed byte by byte
    autosaveIndex = 0;
    autosavePending = false;
    autosaveWriting = true;
  }
  if (!eeprom_is_ready()) return;
  byte value;
  if (autosaveIndex == 0) value = AUTOSAVE_MARK;
  else if (autosaveIndex <= sizeof(StoredConfig)) {
    // No global buffer (41 bytes of RAM left for the stack): pack on every pass and take one byte.
    // loop() calls autosaveStep() after drawing, so this copy does not add to the drawing stack.
    StoredConfig s;
    packConfig(s);
    value = ((const byte *)&s)[autosaveIndex - 1];
    autosaveCheck = ((autosaveCheck << 1) | (autosaveCheck >> 7)) ^ value;
  } else value = autosaveCheck;  // the checksum byte goes last
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

//------------------------ Slot format ------------------------

void packConfig(StoredConfig &s) {
  memcpy(s.hits, currentConfig.hits, MAX_CHANNELS);
  memcpy(s.offset, currentConfig.offset, MAX_CHANNELS);
  memcpy(s.limit, currentConfig.limit, MAX_CHANNELS);
  memcpy(s.probability, currentConfig.probability, MAX_CHANNELS);
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
    currentConfig.hits[c] = s.hits[c] < MAX_PATTERNS ? s.hits[c] : 0;
    currentConfig.offset[c] = s.offset[c] % MAX_STEPS;
    currentConfig.limit[c] = s.limit[c];
    currentConfig.probability[c] = s.probability[c] <= 100 ? s.probability[c] : 100;
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

// Convert EUCLID10 slots (46 bytes) to the compact format (41 bytes).
// New slots start before the old ones, so each one is read completely before it is overwritten.
void migrateLegacySlots() {
  byte last = EEPROM.read(LEGACY_LAST_USED_SLOT_ADDRESS);
  for (byte i = 0; i < NUM_MEMORY_SLOTS; i++) {
    LegacySlot old;
    StoredConfig s;
    EEPROM.get(EEPROM_START_ADDRESS + i * LEGACY_SLOT_SIZE, old);
    memcpy(s.hits, old.hits, MAX_CHANNELS);
    memcpy(s.offset, old.offset, MAX_CHANNELS);
    memcpy(s.limit, old.limit, MAX_CHANNELS);
    memcpy(s.probability, old.probability, MAX_CHANNELS);
    s.muteMask = 0;
    for (byte c = 0; c < MAX_CHANNELS; c++) {
      if (old.mute[c]) s.muteMask |= 1 << c;
    }
    s.xoxMask = 0;
    memset(s.xox, 0, sizeof(s.xox));
    s.tempo = old.tempo;
    s.internalClock = old.internalClock;
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, s);
  }
  EEPROM.update(LAST_USED_SLOT_ADDRESS, last < NUM_MEMORY_SLOTS ? last : 0);
}

void handleSettingNavigation(int changeDirection) {
  switch (selected_setting) {
    case SETTING_TOP_MENU:                                                                              // Select channel
      selected_menu = static_cast<TopMenu>((selected_menu + changeDirection + MENU_LAST) % MENU_LAST);  // Wrap-around for channel selection
      break;
    case SETTING_HITS:                                                                                                          // Hits
      leaveXox(selected_menu);  // changing HITS returns the channel to eucli
      currentConfig.hits[selected_menu] = (currentConfig.hits[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;  // Ensure hits wrap properly
      break;
    case SETTING_OFFSET:
      currentConfig.offset[selected_menu] = (currentConfig.offset[selected_menu] - changeDirection + MAX_STEPS) % MAX_STEPS;  // Wrap-around for offset (reversed the logic of offset so it rotates in the right direction)
      break;
    case SETTING_LIMIT:                                                                                                           // Limit
      currentConfig.limit[selected_menu] = (currentConfig.limit[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;  // Wrap-around for limit
      break;
    case SETTING_RESET:  // Reset channel step
      playing_step[selected_menu] = 0;
      break;
    case SETTING_RANDOM:  // Randomize channel
      Random_change(false, false, selected_menu);
      break;
    case SETTING_PROB:  // Set probability
      currentConfig.probability[selected_menu] = (currentConfig.probability[selected_menu] + changeDirection + 101) % 101;
      break;
    case SETTING_XOX: {  // turning moves the cursor one step
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

// First boot without recognizable slots: all 20 slots start with a basic rhythm
// (4 hits in 16 steps per channel, 120 BPM, external clock).
void defaultConfig(StoredConfig &s) {
  memset(&s, 0, sizeof(s));
  memset(s.hits, 4, MAX_CHANNELS);
  memset(s.limit, MAX_STEPS, MAX_CHANNELS);
  memset(s.probability, 100, MAX_CHANNELS);
  s.tempo = 120;
}

// A slot counts as empty when it holds exactly the initial rhythm.
// Compares byte by byte from the EEPROM without copying the slot to the stack: it is called while the
// S and L screens are drawn, and two StoredConfig on the stack reached the display buffer
// (the small animated pixel square in the lower right corner came back).
bool slotUsed(byte slot) {
  int a = EEPROM_START_ADDRESS + slot * CONFIG_SIZE;
  for (byte i = 0; i < sizeof(StoredConfig); i++) {
    byte e = i < 6 ? 4 : i < 12 ? 0 : i < 18 ? MAX_STEPS : i < 24 ? 100 : i == 38 ? 120 : 0;  // initial rhythm
    if (EEPROM.read(a + i) != e) return true;
  }
  return false;
}

// The slot holds the unchanged old factory preset for its position (from the previous firmware)
bool isOldFactory(byte slot) {
  StoredConfig s;
  byte ref[18];
  EEPROM.get(EEPROM_START_ADDRESS + slot * CONFIG_SIZE, s);
  memcpy_P(ref, oldFactory[slot], 18);
  for (byte c = 0; c < MAX_CHANNELS; c++)
    if (s.probability[c] != 100) return false;
  return !s.muteMask && !s.xoxMask && !memcmp(&s, ref, 18);
}

// Write the presets once, without overwriting slots with the user's own rhythms
// (the initial rhythm and unchanged old factory presets are replaced)
void writePresets() {
  if (EEPROM.read(PRESETS_FLAG_ADDRESS) == PRESETS_MARK) return;
  // first time: the name area may hold leftovers of the old format; all names become dashes
  for (byte k = 0; k < NUM_MEMORY_SLOTS * 3; k++) EEPROM.update(NAMES_ADDRESS + k, '-');
  for (byte i = 0; i < NUM_PRESETS; i++) {
    if (slotUsed(i) && !isOldFactory(i)) continue;
    StoredConfig s;
    memcpy_P(&s, &presets[i], sizeof(s));
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, s);
    for (byte k = 0; k < 3; k++) EEPROM.update(NAMES_ADDRESS + i * 3 + k, pgm_read_byte(&presetNames[i * 3 + k]));
  }
  EEPROM.update(PRESETS_FLAG_ADDRESS, PRESETS_MARK);
}

// Letter of a stored name; anything outside NAME_CHARS (unwritten EEPROM) shows as a dash
char nameChar(byte slot, byte k) {
  char c = EEPROM.read(NAMES_ADDRESS + slot * 3 + k);
  return strchr_P(NAME_CHARS, c) && c ? c : '-';
}

void finishName() {
  if (nameEdit) {
    for (byte k = 0; k < 3; k++) EEPROM.update(NAMES_ADDRESS + selected_slot * 3 + k, nameBuf[k]);
    nameEdit = 0;
  }
  showOverlay = false;
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

// Global cut on M, by hold time (the library's long click only fires on release)
void globalCut() {
  if (selected_menu == MENU_ALL_MUTE && !muteMode && !showOverlay && encoder.isPressed()) {
    unsigned long d = encoder.currentDuration();
    if (holdStage == 0 && d >= 400) {
      if (cutMask) {  // a cut is active: restore the previous mix
        setMutes(mutedMask() & ~cutMask);
        cutMask = 0;
        holdStage = 3;
      } else {        // cut: mute all, the newly muted ones blink
        cutMask = ~mutedMask() & 0x3F;
        blinkMask = cutMask;
        cutTime = millis();
        setMutes(0x3F);
        holdStage = 1;
      }
    } else if (holdStage == 1 && d >= 2000) {
      blinkMask = 0x3F;  // still held: all blink
      holdStage = 2;
    } else if (holdStage == 2 && d >= 4000) {
      setMutes(0);       // and at 4 s everything is unmuted
      cutMask = 0;
      blinkMask = 0;
      holdStage = 3;
    }
  } else if (!encoder.isPressed()) {
    holdStage = 0;
  }
  if (blinkMask && holdStage != 2 && millis() - cutTime >= 2000) {
    blinkMask = 0;
    disp_refresh = true;
  }
}

void resetSeq() {
  for (int k = 0; k < MAX_CHANNELS; k++) {
    playing_step[k] = 15;
  }
}

// Vertical side labels: system font (fixed width keeps the letters aligned).
void verticalMenu(byte x, byte y, char c1, char c2, char c3, char c4) {
  char c[4] = { c1, c2, c3, c4 };
  display.setFont(NULL);
  for (byte i = 0; i < 4; i++) {
    display.setCursor(x, y + 9 * i);
    display.print(c[i]);
  }
}

void leftMenu(char c1, char c2, char c3, char c4) {
  verticalMenu(0, 30, c1, c2, c3, c4);
}

void rightMenu(char c1, char c2, char c3, char c4) {
  verticalMenu(120, 0, c1, c2, c3, c4);
}

void drawTopMenuRight(TopMenu select_ch) {
  if (showOverlay) return;  // Exit
  switch (select_ch) {
    case MENU_CH_1: rightMenu('1', 'H', 'O', ' '); break;
    case MENU_CH_2: rightMenu('2', 'H', 'O', ' '); break;
    case MENU_CH_3: rightMenu('3', 'H', 'O', ' '); break;
    case MENU_CH_4: rightMenu('4', 'H', 'O', ' '); break;
    case MENU_CH_5: rightMenu('5', 'H', 'O', ' '); break;
    case MENU_CH_6: rightMenu('6', 'H', 'O', ' '); break;
    case MENU_RANDOM_ADVANCE: rightMenu('R', 'N', 'D', ' '); break;
    case MENU_SAVE: rightMenu('S', ' ', ' ', ' '); break;
    case MENU_LOAD: rightMenu('L', ' ', ' ', ' '); break;
    case MENU_ALL_RESET: rightMenu('R', ' ', ' ', ' '); break;
    case MENU_ALL_MUTE: rightMenu('M', ' ', ' ', ' '); break;
    case MENU_RAND: rightMenu('X', ' ', ' ', ' '); break;
    case MENU_TEMPO:
      if (internalClock) {
        rightMenu('I', 'N', 'T', ' ');
      } else {
        rightMenu('E', 'X', 'T', ' ');
      }
      break;
    default: break;
  }
}

// left side menue - Channel Settings
void drawChannelEditMenu(TopMenu select_ch, Setting select_menu) {
  if (showOverlay) return;  // Exit
  switch (select_menu) {
    case SETTING_HITS: leftMenu('H', 'I', 'T', 'S'); break;
    case SETTING_OFFSET: leftMenu('O', 'F', 'F', 'S'); break;
    case SETTING_LIMIT: leftMenu('L', 'I', 'M', 'I'); break;
    case SETTING_RESET: leftMenu('R', 'S', 'E', 'T'); break;
    case SETTING_RANDOM: leftMenu('R', 'A', 'N', 'D'); break;
    case SETTING_PROB: leftMenu('P', 'R', 'O', 'B'); break;
    case SETTING_XOX: leftMenu('X', 'O', 'X', ' '); break;
    default: break;
  }
}

// left side menue - Menu Options
void drawModeMenu(TopMenu select_ch) {
  if (showOverlay) return;  // Exit
  switch (select_ch) {
    case MENU_SAVE: leftMenu('S', 'A', 'V', 'E'); break;
    case MENU_LOAD: leftMenu('L', 'O', 'A', 'D'); break;
    case MENU_ALL_RESET: leftMenu('R', 'S', 'E', 'T'); break;
    case MENU_ALL_MUTE:
      if (muteMode) {  // inside mute mode: MUTE inverted on a white strip
        display.fillRect(0, 28, 7, 37, WHITE);
        display.setTextColor(BLACK);
        verticalMenu(1, 30, 'M', 'U', 'T', 'E');
        display.setTextColor(WHITE);
      } else {
        leftMenu('M', 'U', 'T', 'E');
      }
      break;
    case MENU_TEMPO: leftMenu('*', 'C', 'L', 'K'); break;
    case MENU_RAND: leftMenu('R', 'A', 'N', 'D'); break;
    default: break;
  }
}

void checkAndInitializeSettings() {
  char magic[sizeof(FIRMWARE_MAGIC)];
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);

  if (strncmp(magic, FIRMWARE_MAGIC, sizeof(FIRMWARE_MAGIC)) == 0) return;

  if (strncmp(magic, LEGACY_MAGIC, sizeof(LEGACY_MAGIC)) == 0) {
    migrateLegacySlots();  // slots from the previous firmware: convert them
  } else {
    initializeDefaultRhythms();  // EEPROM without a known format: basic rhythm in every slot
  }
  EEPROM.update(AUTOSAVE_ADDRESS, 0);  // no valid autosave yet
  EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
}

// Drawing random advance indicator
void drawRandomModeAdvanceSquare(int bar_select, int bar_now, const int *bar_max) {  // Change to const int*
  if (62 - pgm_read_word(&bar_max[bar_select]) * 2 >= 0 && 64 - bar_now * 2 >= 0) {
    display.drawRoundRect(1, 62 - pgm_read_word(&bar_max[bar_select]) * 2, 6, pgm_read_word(&bar_max[bar_select]) * 2 + 2, 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_now * 2, WHITE);
  }
}

void drawSelectionIndicator(Setting select_menu) {
  if (showOverlay) return;
  if (select_menu == SETTING_TOP_MENU) display.drawTriangle(113, 0, 113, 6, 118, 3, WHITE);
  else if (select_menu == SETTING_HITS) display.drawTriangle(113, 9, 113, 15, 118, 12, WHITE);
  else if (select_menu == SETTING_OFFSET) display.drawTriangle(113, 18, 113, 24, 118, 21, WHITE);

  if (select_menu == SETTING_LIMIT) display.drawTriangle(12, 34, 12, 41, 7, 37, WHITE);
  else if (select_menu == SETTING_RESET) display.drawTriangle(12, 50, 12, 57, 7, 53, WHITE);
  else if (select_menu == SETTING_RANDOM) display.drawTriangle(12, 58, 12, 65, 7, 61, WHITE);
  // PROB has no triangle: it fell off the screen (row 66 of 64)
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
  drawTopMenuRight(selected_menu);
  drawChannelEditMenu(selected_menu, selected_setting);
  drawModeMenu(selected_menu);
  if (!showOverlay && selected_setting == SETTING_TOP_MENU && selected_menu <= MENU_CH_6) drawBpmColumn();

  if (selected_menu == MENU_RANDOM_ADVANCE) {
    drawRandomModeAdvanceSquare(bar_select, bar_now, bar_max);
  }

  drawSelectionIndicator(selected_setting);

  if (selected_setting == SETTING_PROB) {
    drawProbabilityConfig();
  } else {
    drawStepDots(currentConfig);
    drawEuclideanRhythms();
    drawXoxCursor();
    if (muteMode) {  // mute mode cursor
      if (currentConfig.mute[muteCursor]) {
        display.drawCircle(graph_x[muteCursor] + 15, graph_y[muteCursor] + 15, 11, WHITE);  // thin ring around the sticker
      } else {
        drawMuteSticker(muteCursor, false);  // outlined sticker over the shape
      }
    }
  }
}

// XOX cursor: filled diamond on the step. It blinks when the step is on and stays still when it is off.
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
  if (selected_setting == SETTING_TOP_MENU) {
    switch (selected_menu) {
      case MENU_SAVE:
      case MENU_LOAD:
        drawSaveLoadSelection();
        break;
      case MENU_TEMPO:
        drawTempo();
        break;
    }
  }
}

void drawEuclideanRhythms() {
  if (showOverlay) return;  // Exit
  // draw hits line : 2~16hits if not muted
  // Drawn point to point without copying points to arrays (saves 34 bytes of RAM for the stack).
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] || selected_setting == SETTING_LIMIT) continue;  // in LIMI the shape is not drawn
    int8_t first = -1, prev = -1;
    for (byte m = 0; m < MAX_STEPS; m++) {
      if (!stepOn(k, m)) continue;
      if (prev < 0) first = m;
      else display.drawLine(pgm_read_byte(&x16[prev]) + graph_x[k], pgm_read_byte(&y16[prev]) + graph_y[k], pgm_read_byte(&x16[m]) + graph_x[k], pgm_read_byte(&y16[m]) + graph_y[k], WHITE);
      prev = m;
    }
    if (first >= 0) {
      // Close the shape. With a single active step (eucli or XOX), a line from the ring center to that step.
      byte x0 = pgm_read_byte(&x16[first]) + graph_x[k], y0 = pgm_read_byte(&y16[first]) + graph_y[k];
      if (first == prev) display.drawLine(15 + graph_x[k], 15 + graph_y[k], x0, y0, WHITE);
      else display.drawLine(x0, y0, pgm_read_byte(&x16[prev]) + graph_x[k], pgm_read_byte(&y16[prev]) + graph_y[k], WHITE);
    }
  }

  //draw play step circle
  for (int k = 0; k < MAX_CHANNELS; k++) {                                 //ch count
    if (currentConfig.mute[k] == 0 && selected_setting != SETTING_PROB) {  //mute on = no display circle
      if (!stepOn(k, playing_step[k])) {
        display.drawCircle(pgm_read_byte(&x16[playing_step[k]]) + graph_x[k], pgm_read_byte(&y16[playing_step[k]]) + graph_y[k], 2, WHITE);
      }
      if (stepOn(k, playing_step[k])) {
        display.fillCircle(pgm_read_byte(&x16[playing_step[k]]) + graph_x[k], pgm_read_byte(&y16[playing_step[k]]) + graph_y[k], 3, WHITE);
      }
    }
  }

  // Muted channel: filled white sticker with a black M (in LIMI the step count is shown instead)
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] && selected_setting != SETTING_LIMIT)
      drawMuteSticker(k, !((blinkMask >> k) & 1 && (millis() & 128)));  // parpadeo: relleno / borde
  }

  // draw channel info in edit mode, should be helpful while editing.
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;


    // From 5 hits on, counting the corners gets slow: show the number at the center, as in LIMI.
    // In XOX it counts the active steps of the drawing.
    byte nHits = isXox(ch) ? __builtin_popcount(xoxPattern[ch]) : currentConfig.hits[ch];
    if (currentConfig.mute[ch] == 0 && nHits > 4 && selected_setting != SETTING_LIMIT ) {
      drawRingNumber(ch, nHits);
    }

    // draw selected parameter UI for currently active channel when editing
    if (selected_setting != SETTING_TOP_MENU) {
      switch (selected_setting) {
        case SETTING_HITS: break;
        case SETTING_OFFSET: break;
        case SETTING_LIMIT:
          drawRingNumber(ch, currentConfig.limit[ch]);  // step count at the ring center
          break;
      }
    }
  }
}

// Number at the center of a channel ring. Digits are 7 px tall, so drawn from y+12
// they are centered on the ring (center y+15). (Double size, 14 px, looked too big.)
void drawRingNumber(byte ch, byte n) {
  numAt(graph_x[ch] + (n > 9 ? 10 : 13), graph_y[ch] + 12);
  display.print(n);
}

// Mute sticker: 19 px circle at the ring center with an M (system font, 5 x 7 px).
// filled = muted channel (white with a black M); otherwise white outline, black inside and a white M.
void drawMuteSticker(byte ch, bool filled) {
  int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
  display.fillCircle(cx, cy, 9, filled ? WHITE : BLACK);
  display.drawCircle(cx, cy, 9, WHITE);
  display.setTextColor(filled ? BLACK : WHITE);
  textAt(cx - 2, cy - 3);
  display.print('M');
  display.setTextColor(WHITE);
}

// Vertical BPM in the left column (empty while a channel is selected).
// Internal clock: the set tempo. External: the tempo measured at CLK ("---" without a signal).
void drawBpmColumn() {
  int bpm = internalClock ? tempo : externalBPM;
  char d[5] = "---";
  if (bpm > 0) itoa(bpm, d, 10);
  leftMenu(d[0], d[1], d[2] ? d[2] : ' ', ' ');
}

// Probability as a donut: it fills clockwise from the top, like the step order,
// and the number sits in the hole. The selected channel gets a thin outer ring,
// like the mute mode cursor. No trigonometry: the edge direction is interpolated from the
// 16-direction ring table, and each pixel is tested with cross products.
void drawProbabilityConfig() {
  if (selected_setting != SETTING_PROB) return;  // Exit early
  for (byte ch = 0; ch < MAX_CHANNELS; ch++) {
    int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
    byte p = currentConfig.probability[ch];
    // sector edge direction, in 16ths of a turn with a 4-bit fraction
    byte pos = p * 64 / 25 > 255 ? 255 : p * 64 / 25;  // p * 256 / 100
    byte k = pos >> 4, fr = pos & 15, k2 = (k + 1) & 15;
    int vx = ((int)pgm_read_byte(&x16[k & 15]) - 15) * (16 - fr) + ((int)pgm_read_byte(&x16[k2]) - 15) * fr;
    int vy = ((int)pgm_read_byte(&y16[k & 15]) - 15) * (16 - fr) + ((int)pgm_read_byte(&y16[k2]) - 15) * fr;
    for (int8_t y = -14; y <= 14; y++) {
      for (int8_t x = -14; x <= 14; x++) {
        int d = x * x + y * y;
        if (d > 196 || d <= 100) continue;  // only the ring between radius 10 and 14 (the hole fits "100")
        bool in;
        if (p >= 100) in = true;
        else {
          bool cw = (long)x * vy - (long)y * vx > 0;  // the edge is further clockwise than the pixel
          in = p <= 50 ? (x >= 0 && cw) : (x >= 0 || cw);
        }
        if (in && p) display.drawPixel(cx + x, cy + y, WHITE);
      }
    }
    display.drawCircle(cx, cy, 14, WHITE);
    display.drawCircle(cx, cy, 10, WHITE);
    if (ch == selected_menu) display.drawCircle(cx, cy, 16, WHITE);  // selected channel
    numAt(cx - (p >= 100 ? 9 : p >= 10 ? 6 : 3), cy - 3);
    display.print(p);
  }
}

//  Display selected slot
void drawSaveLoadSelection() {
  textAt(0, 0);
  display.print(selected_menu == MENU_SAVE ? F("Save to Slot") : F("Load from Slot"));
  {
    numAt(0, 14, 2);
    display.print(selected_slot + 1, DEC);
    // slot with something saved: filled circle; initial rhythm: outline only
    bool used = slotUsed(selected_slot);
    if (used) display.fillCircle(32, 20, 4, WHITE);
    else display.drawCircle(32, 20, 4, WHITE);
    textAt(44, 14, 2);
    for (byte k = 0; k < 3; k++) display.print(nameEdit ? nameBuf[k] : nameChar(selected_slot, k));
    if (nameEdit) display.drawFastHLine(44 + (nameEdit - 1) * 12, 31, 10, WHITE);  // letter being edited
    textAt(0, 35);
    if (nameEdit) display.print(F("Name: turn, click"));
    else if (selected_menu == MENU_SAVE) display.print(used ? F("long PRESS Overwrite") : F("long PRESS Save"));
  }
  if (selected_menu == MENU_LOAD) {
    textAt(0, 35);
    display.print(F("long PRESS Load"));
  }
  // While held, a line fills under the text; when full, releasing saves or loads
  if (!nameEdit && encoder.isPressed()) {
    uint16_t d = encoder.currentDuration() >> 2;  // in 4 ms units
    if (d > LONG_CLICK_MS / 4) d = LONG_CLICK_MS / 4;
    display.drawFastHLine(0, 46, d + d / 4, WHITE);  // 375 ms -> ~116 px
  }
}

void drawTempo() {
  int16_t x1 = 10, y1 = 10;
  uint16_t w = 108, h = 44;  // Increased width and height
  uint16_t b = 4;
  uint16_t b2 = 8;

  // Clear screen underneath and draw the rectangle
  //display.fillRect(x1 - b, y1 - b, w + b2, h + b2, BLACK);
  //display.drawRoundRect(x1, y1, w, h, 2, WHITE);

  if (internalClock) {
    textAt(0, 0);
    display.print(F("Dial in Tempo"));

    // Calculate the width of the filled portion based on the tempo
    int filledWidth = (long)(tempo - 40) * (w - 22) / (240 - 40);  // margin on each side (no float: saves flash)

    // Draw the rounded rectangle
    display.drawRoundRect(x1 + 10, y1 + h / 2 - 5, w - 20, 10, 2, WHITE);

    // Draw the filled portion
    display.fillRoundRect(x1 + 11, y1 + h / 2 - 3, filledWidth, 6, 2, WHITE);

    numAt(12, 45);
    display.print(F("40"));

    numAt(50, 45);
    display.print(tempo);

    numAt(95, 45);
    display.print(F("240"));

  } else {
    if (externalBPM == 0) {  // no signal at CLK
      textAt(0, 0);
      display.print(F("Patch CLK or"));
      textAt(0, 13);
      display.print(F("long press for"));
      textAt(0, 26);
      display.print(F("INT CLK"));
    } else {
      textAt(0, 0);
      display.print(F("Ext BPM"));
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
