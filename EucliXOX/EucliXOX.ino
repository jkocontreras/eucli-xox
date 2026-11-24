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
#include "Font4x7.h"

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

// Channels (selected_menu always holds the selected channel).
enum TopMenu {
  MENU_CH_1,
  MENU_CH_2,
  MENU_CH_3,
  MENU_CH_4,
  MENU_CH_5,
  MENU_CH_6,
  MENU_LAST
};

// Enum for individual channel settings.
enum Setting {
  SETTING_TOP_MENU,
  SETTING_HITS,
  SETTING_OFFSET,
  SETTING_LIMIT,
  SETTING_RESET,
  SETTING_PROB,
  SETTING_COND,  // COND: in which bars of the 4 bar cycle the channel plays
  SETTING_XOX,  // XOX mode: turn moves the cursor, click toggles the step, long click leaves
  SETTING_LAST
};

// Navigation: one cyclic chain of items. Click goes to the next, double click to the previous.
// Long click opens the closed items (XOX, RNDM, MUTE, SYST), runs RSET, and closes an open mode.
// Turning changes the number of the item (the channel in CH); on the closed items it does nothing.
// Press and turn changes channel.
enum Nav : byte {
  NAV_CH,  // the selected channel (turn changes channel)
  NAV_HITS,
  NAV_OFFS,
  NAV_LIMI,
  NAV_RESET,
  NAV_PROB,
  NAV_COND,
  NAV_XOX,   // closed: XOX step programming
  NAV_RNDM,  // closed: timed random
  NAV_MUTE,  // closed: mute mode
  NAV_RSET,  // runs the reset of all channels
  NAV_SYST,  // closed: SAVE, LOAD, CLOCK, BPM
  NAV_LAST
};
#define MULTI_CLICK_MS 220  // double click window; inside the modes it is 20 ms so that clicks answer at once

// For debug / UI
// EUCLID11: compact slot format with XOX patterns. Slots of any other format are not converted.
#define FIRMWARE_MAGIC "EUCLID11"
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

// COND: a bar is 16 clocks (one turn of the ring) and the cycle is 4 bars. Each channel plays a block of
// consecutive bars. barCond = index into barMask, in the order shown when turning:
// 0000, then by number of hits: X000 0X00 00X0 000X, XX00 0XX0 00XX X0X0 0X0X X00X, XXX0 0XXX XX0X X0XX, XXXX
// (bit n of the mask = plays in bar n+1).
// Saved in the high nibble of offset[] (OFFS is 0..15), XORed with 15 so that old slots read as XXXX.
const byte barMask[16] PROGMEM = { 0, 1, 2, 4, 8, 3, 6, 12, 5, 10, 9, 7, 14, 11, 13, 15 };
#define BAR_ALL 15
byte barCond[6] = { BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL, BAR_ALL };
byte barClock = 15;  // clock count inside the bar; 15 so that the next clock starts bar 1
byte barIdx = 3;     // bar being played, 0..3

// display Menu and UI
// Select settings menu and channel menu
TopMenu selected_menu = MENU_CH_1;
Setting selected_setting = SETTING_TOP_MENU;
byte nav = NAV_CH;
bool xoxMode = false;  // XOX open
bool systMode = false;  // SYST open (showOverlay = an entry of SYST is open)
byte sysItem = 0;       // 0 SAVE, 1 LOAD, 2 CLOCK, 3 BPM
byte selected_slot = 0;
bool disp_refresh = true;
bool force_refresh = true;
unsigned long last_refresh = 0;
// Global cut, inside the mute mode: press and turn to the right mutes all, to the left restores the previous mix.
byte cutMask = 0;  // channels muted by the cut (0 = no active cut)
// Mute mode: turn chooses a channel, click mutes/unmutes, long click leaves
bool muteMode = false;
byte muteCursor = 0;

// RNDM mode on RNDM: timed random. Each channel has a COND-like combination of 4 bars (index into barMask):
// in the bars that are filled, the channel changes its hits and offset at random at the start of the bar.
// 0 = never (the default). Turn changes the combination of the channel, press and turn changes channel,
// long click leaves. Saved in the memory slots and in the autosave, as COND.
bool rndmMode = false;
byte rndmCursor = 0;
byte rndmCond[6] = { 0, 0, 0, 0, 0, 0 };  // one per channel
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
#define LONG_CLICK_MS 300  // long click duration
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

// Presets written once into slots 1 to 9 (only where the slot holds the initial rhythm). From simple to complex: EUC, BOS and CUB in eucli mode (16 steps, they show HITS
// and OFFS); DEM and BRK drawn in XOX (BRK mixes eucli channels), CMB mostly in eucli; TER (12 steps) and POL
// (different lengths) show LIMT.
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
  // CMB: classic Colombian cumbia (by JKO): channels 1 to 5 in eucli mode with hand picked offsets, channel 6 drawn in XOX
  { { 4, 2, 12, 4, 4, 3 }, { 4, 14, 15, 12, 0, 10 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x20, { 0x0101, 0x4444, 0x5555, 0xDDDD, 0x40C0, 0x0181 }, 95, 2 },
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


byte autosaveCheck = 0;

// Boot splash, EUCLID + XOX, 72x39 px, 351 bytes. Generated from JKO's drawing (images/splash-nuevo.png, 128x64 canvas)
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

// Boot splash: the image keeps its position from the original 128x64 canvas
void drawAnimation() {
  display.clearDisplay();
  display.drawBitmap(31, 13, Splash_Logo, logoWidth, logoHeight, WHITE);
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

// Changes the hits and the offset of one channel at random, to any value that could be set by hand
// (hits 1 to 16, offset 0 to 15) and different from the current one. It does not touch the mute.
void Random_change(byte k) {
  byte h;
  do {
    h = random(1, MAX_PATTERNS);
  } while (h == currentConfig.hits[k]);
  xoxMask &= ~(1 << k);  // changing HITS returns the channel to eucli
  currentConfig.hits[k] = h;
  byte o;
  do {
    o = random(0, MAX_STEPS);
  } while (o == currentConfig.offset[k]);
  currentConfig.offset[k] = o;
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
    if (++barClock >= 16) {
      barClock = 0;
      barIdx = (barIdx + 1) & 3;
      bool changed = false;
      for (byte k = 0; k < MAX_CHANNELS; k++) {  // start of a bar: the channels whose RNDM combination has it filled
        if ((pgm_read_byte(&barMask[rndmCond[k]]) >> barIdx) & 1) {
          Random_change(k);
          changed = true;
        }
      }
      if (changed) updateRythm();
    }

    // Output gate signal
    outputGateSignals();

    disp_refresh = true;  // Updates the display where the trigger was entered.
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
  if (selected_setting == SETTING_XOX) {
    static bool blinkPhase = false;
    bool phase = millis() & 512;
    if (phase != blinkPhase) {
      blinkPhase = phase;
      disp_refresh = true;
    }
  }

  // Redrawing the screen takes ~25 ms and nothing can be switched off meanwhile:
  // wait until the LED flashes end, so they really last LED_FLASH_MS.
  if (millis() - gate_timer >= LED_FLASH_MS) OLED_display();

  autosaveStep();  // autosave
}

void setupEncoder() {
  encoder.setDebounceInterval(2);  // Increase debounce interval
  encoder.setMultiClickInterval(MULTI_CLICK_MS);
  encoder.setRateLimit(100);
  encoder.setIdleTimeout(5000);
  encoder.setIdleHandler(onOverlayTimeout);
  encoder.setLongClickDuration(LONG_CLICK_MS);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setDoubleClickHandler(onEncoderDoubleClicked);
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
  display.setFont(&Font4x7);
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
  return xoxMode || muteMode || rndmMode || systMode;
}

// After any change of nav or of a mode: the setting that the drawing and the turning use, and the double click window.
void syncNav() {
  selected_setting = nav <= NAV_COND ? static_cast<Setting>(nav) : (nav == NAV_XOX && xoxMode ? SETTING_XOX : SETTING_TOP_MENU);
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
  disp_refresh = true;  // just in case

  if (nameEdit) {
    if (++nameEdit > 3) finishName();  // click: next letter; after the third, close
  } else if (systMode) {
    if (!showOverlay) {
      showOverlay = true;  // open the entry
    } else if (sysItem == 0) {  // SAVE: confirm, then the slot is named
      bool used = slotUsed(selected_slot);
      saveToEEPROM(selected_slot);
      for (byte k = 0; k < 3; k++) nameBuf[k] = used ? nameChar(selected_slot, k) : '-';
      nameEdit = 1;
    } else {  // LOAD: confirm; CLOCK and BPM: close
      if (sysItem == 1) {
        loadFromEEPROM(selected_slot);
        leaveSyst();  // after loading, go straight to the first item to play
      }
      showOverlay = false;
    }
  } else if (xoxMode) {
    toggleXoxStep(selected_menu);  // in XOX a click toggles the step under the cursor
  } else if (muteMode) {
    currentConfig.mute[muteCursor] = !currentConfig.mute[muteCursor];
    cutMask = 0;  // the mix stored by the cut no longer applies
  } else if (rndmMode) {
    // a click inside RNDM does nothing: turn changes the combination, long click leaves
  } else {
    nav = (nav + 1) % NAV_LAST;
    syncNav();
  }
}

void onEncoderDoubleClicked(EncoderButton &eb) {
  if (inMode()) {  // inside a mode a double click counts as two clicks
    onEncoderClicked(eb);
    onEncoderClicked(eb);
    return;
  }
  markChange();
  nav = (nav + NAV_LAST - 1) % NAV_LAST;
  syncNav();
}

// Long click: closes what is open, or opens the closed item that is selected.
void onEncoderLongClicked(EncoderButton &eb) {
  markChange();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;

  if (nameEdit) {
    finishName();  // long click while naming: close
  } else if (systMode) {
    if (showOverlay) showOverlay = false;  // leave the entry without confirming
    else systMode = false;
  } else if (xoxMode) {
    xoxMode = false;
  } else if (muteMode) {
    muteMode = false;
  } else if (rndmMode) {
    rndmMode = false;
  } else {
    switch (nav) {
      case NAV_XOX: xoxMode = true; break;
      case NAV_RNDM: rndmMode = true; break;
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

// Turning inside SYST: choose the entry, or change the value of the open entry.
void sysTurn(int increment, int accelerated) {
  if (!showOverlay) {
    sysItem = (sysItem + (increment > 0 ? 1 : 3)) & 3;
  } else if (sysItem < 2) {
    if (nameEdit) {  // turning changes the letter
      char *c = &nameBuf[nameEdit - 1];
      byte i = (const char *)strchr_P(NAME_CHARS, *c) - NAME_CHARS;
      *c = pgm_read_byte(&NAME_CHARS[(i + increment + NUM_NAME_CHARS) % NUM_NAME_CHARS]);
    } else {
      selected_slot = (selected_slot + increment + NUM_MEMORY_SLOTS) % NUM_MEMORY_SLOTS;
    }
  } else if (sysItem == 2) {  // CLOCK: turn switches between INT and EXT
    internalClock = !internalClock;
    currentConfig.internalClock = internalClock;
    if (internalClock) period = 60000 / tempo / 4;
  } else if (internalClock) {  // BPM: 40 to 240
    tempo = constrain(tempo + accelerated, 40, 240);
    period = 60000 / tempo / 4;
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
  if (rndmMode) {  // in RNDM mode, turning goes through the 16 combinations of the channel
    rndmCond[rndmCursor] = (rndmCond[rndmCursor] + (increment > 0 ? 1 : 15)) & 15;
    return;
  }

  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  if (systMode) {
    sysTurn(increment, acceleratedIncrement);
  } else if (nav == NAV_CH) {
    changeChannel(increment);
  } else if (nav < NAV_XOX || xoxMode) {
    handleSettingNavigation(acceleratedIncrement);  // the value of the setting (in XOX, the cursor)
  }
  // on the closed items (XOX, RNDM, MUTE, RSET, SYST) there is no number: turning does nothing
}

void onEncoderPressedRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;
  markChange();
  disp_refresh = true;

  if (muteMode) {  // global cut: to the right mutes all, to the left restores the previous mix
    if (increment > 0) {
      if (!cutMask) {
        cutMask = ~mutedMask() & 0x3F;
        setMutes(0x3F);
      }
    } else if (cutMask) {
      setMutes(mutedMask() & ~cutMask);
      cutMask = 0;
    } else {
      setMutes(0);  // no stored mix: unmute all
    }
  } else if (rndmMode) {  // in RNDM mode, press and turn changes channel
    rndmCursor = (rndmCursor + (increment > 0 ? 1 : MAX_CHANNELS - 1)) % MAX_CHANNELS;
  } else if (!systMode) {  // everywhere else: the selected channel (in XOX, the channel that is edited)
    changeChannel(increment);
  }
}


//---------------------------- XOX ----------------------------

bool isXox(byte ch) {
  return xoxMask & (1 << ch);
}

// Cursor position within the channel length (LIMT)
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
  // COND goes in the high nibble of offset (XORed with 15, so slots without it read as XXXX).
  // RNDM (4 bits per channel) goes in the 3 free high bits of hits and the free top bit of probability
  // (slots without it read as 0000, never).
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
    case SETTING_PROB:  // Set probability
      currentConfig.probability[selected_menu] = (currentConfig.probability[selected_menu] + changeDirection + 101) % 101;
      break;
    case SETTING_COND:
      barCond[selected_menu] = (barCond[selected_menu] + (changeDirection > 0 ? 1 : 15)) & 15;
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


// Write the presets once, without overwriting slots with the user's own rhythms
// (only slots holding the initial rhythm are replaced)
void writePresets() {
  if (EEPROM.read(PRESETS_FLAG_ADDRESS) == PRESETS_MARK) return;
  // first time: the name area may hold leftovers of the old format; all names become dashes
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

// Letter of a stored name; anything outside NAME_CHARS (unwritten EEPROM) shows as a dash
char nameChar(byte slot, byte k) {
  char c = EEPROM.read(NAMES_ADDRESS + slot * 3 + k);
  return strchr_P(NAME_CHARS, c) && c ? c : '-';
}

// Closes SYST and puts the cursor on the first item of the chain.
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
  leaveSyst();  // after saving, go straight to the first item to play
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

// Vertical side labels: system font (fixed width keeps the letters aligned).
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

// One glyph of the right column; inverted (white box, black letter) when it is the cursor.
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

// Right column: the channel number, H and O. The inverted one is the cursor (CH, HITS, OFFS).
// From LIMT on there is no initial to mark: the name of the item is on the left.
// RNDM and MUTE show only the number of the channel of their cursor; RSET and SYST show nothing.
void drawRightColumn() {
  if (showOverlay || systMode) return;
  if (muteMode || rndmMode) {
    rightGlyph(0, '1' + (muteMode ? muteCursor : rndmCursor), true);
    return;
  }
  if (nav >= NAV_RNDM) return;
  rightGlyph(0, '1' + selected_menu, nav == NAV_CH);
  rightGlyph(1, 'H', nav == NAV_HITS);
  rightGlyph(2, 'O', nav == NAV_OFFS);
}

// Left column: the name of the item. The items that open with a long click (XOX, RNDM, MUTE, SYST, RSET)
// are drawn inverted (white strip, black letters). On CH the BPM is shown.
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
    case NAV_COND: leftName('C', 'O', 'N', 'D', false); break;
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

  initializeDefaultRhythms();  // EEPROM without a known format: basic rhythm in every slot
  EEPROM.update(AUTOSAVE_ADDRESS, 0);  // no valid autosave yet
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
    if (nav == NAV_COND) drawBarStickers(barCond, selected_menu);  // COND and RNDM: a small square on each ring
    else if (nav == NAV_RNDM && !showOverlay) drawBarStickers(rndmCond, rndmMode ? rndmCursor : -1);
    if (muteMode) {  // mute mode cursor
      if (currentConfig.mute[muteCursor]) {
        display.drawCircle(graph_x[muteCursor] + 15, graph_y[muteCursor] + 15, 11, WHITE);  // thin ring around the sticker
      } else {
        drawSticker(muteCursor, false, 'M');  // outlined sticker over the shape
      }
    }
  }
}

// COND and RNDM stickers: a square divided in 4, one quarter per bar of the 4 bar cycle (reading order:
// top left = bar 1, top right = bar 2, bottom left = bar 3, bottom right = bar 4). A filled quarter is a bar
// where the channel plays (COND) or changes at random (RNDM); an empty one is only outlined.
// It is small (11 px) and sits at the center of each ring over a black backing, so that the rhythm stays
// visible behind. The channel of the cursor gets a thin ring, like the mute cursor.
#ifndef STICKER_Q
#define STICKER_Q 3  // side of a quarter in pixels: the sticker is 2 * STICKER_Q + 1 pixels wide (3 = 7 px, 4 = 9 px, 5 = 11 px)
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
  if (!systMode) return;
  if (sysItem < 2) drawSaveLoadSelection();
  else if (sysItem == 2) drawClock();
  else drawTempo();
}

// SYST list: the four entries in the normal text size, the selected one inverted, with their current value
void drawSystList() {
  for (byte i = 0; i < 4; i++) {
    int y = 9 + i * 12;
    bool sel = i == sysItem;
    if (sel) display.fillRect(8, y - 2, 112, 11, WHITE);
    display.setTextColor(sel ? BLACK : WHITE);
    textAt(14, y);
    switch (i) {
      case 0: display.print(F("SAVE")); break;
      case 1: display.print(F("LOAD")); break;
      case 2: display.print(F("CLOCK")); break;
      case 3: display.print(F("BPM")); break;
    }
    if (i >= 2) {
      textAt(86, y);
      if (i == 2) display.print(internalClock ? F("INT") : F("EXT"));
      else display.print(internalClock ? tempo : externalBPM);
    }
  }
  display.setTextColor(WHITE);
}

// CLOCK entry: turn switches between the internal and the external clock
void drawClock() {
  textAt(0, 0);
  display.print(F("CLOCK"));
  textAt(0, 14, 3);
  display.print(internalClock ? F("INT") : F("EXT"));
  textAt(0, 44);
  display.print(F("TURN INT OR EXT"));
  textAt(0, 55);
  display.print(F("CLICK CLOSE"));
}

void drawEuclideanRhythms() {
  if (showOverlay) return;  // Exit
  // draw hits line : 2~16hits if not muted
  // Drawn point to point without copying points to arrays (saves 34 bytes of RAM for the stack).
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] || selected_setting == SETTING_LIMIT) continue;  // in LIMT the shape is not drawn
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

  // Muted channel: filled white sticker with a black M (in LIMT the step count is shown instead)
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] && selected_setting != SETTING_LIMIT && nav != NAV_RNDM && nav != NAV_COND)
      drawSticker(k, true, 'M');
  }

  // draw channel info in edit mode, should be helpful while editing.
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;


    // From 5 hits on, counting the corners gets slow: show the number at the center, as in LIMT.
    // In XOX it counts the active steps of the drawing.
    byte nHits = isXox(ch) ? __builtin_popcount(xoxPattern[ch]) : currentConfig.hits[ch];
    if (currentConfig.mute[ch] == 0 && nHits > 4 && selected_setting != SETTING_LIMIT && nav != NAV_RNDM && nav != NAV_COND) {
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
void drawSticker(byte ch, bool filled, char letter) {
  int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
  display.fillCircle(cx, cy, 9, filled ? WHITE : BLACK);
  display.drawCircle(cx, cy, 9, WHITE);
  display.setTextColor(filled ? BLACK : WHITE);
  textAt(cx - 2, cy - 3);
  display.print(letter);
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
        if (d > 196 || d <= 144) continue;  // only the ring between radius 12 and 14 (the hole fits "100" with room)
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
    display.drawCircle(cx, cy, 12, WHITE);
    if (ch == selected_menu) display.drawCircle(cx, cy, 16, WHITE);  // selected channel
    numAt(cx - (p >= 100 ? 9 : p >= 10 ? 6 : 3), cy - 3);
    display.print(p);
  }
}

// A field (width or xAdvance) of the glyph of a character in Font4x7.
byte glyphField(char c, byte field) {
  return pgm_read_byte((const byte *)&Font4x7Glyphs[c - 0x20] + field);
}

//  Display selected slot
void drawSaveLoadSelection() {
  textAt(0, 0);
  display.print(sysItem == 0 ? F("SAVE TO SLOT") : F("LOAD FROM SLOT"));
  {
    numAt(0, 14, 2);
    display.print(selected_slot + 1, DEC);
    // slot with something saved: filled circle; initial rhythm: outline only
    bool used = slotUsed(selected_slot);
    if (used) display.fillCircle(32, 20, 4, WHITE);
    else display.drawCircle(32, 20, 4, WHITE);
    textAt(44, 14, 2);
    for (byte k = 0; k < 3; k++) display.print(nameEdit ? nameBuf[k] : nameChar(selected_slot, k));
    if (nameEdit) {  // underline the letter being edited, using the real width of each letter
      byte x = 44;
      for (byte k = 0; k < nameEdit - 1; k++) x += 2 * glyphField(nameBuf[k], offsetof(GFXglyph, xAdvance));
      display.drawFastHLine(x, 31, 2 * glyphField(nameBuf[nameEdit - 1], offsetof(GFXglyph, width)), WHITE);
    }
    textAt(0, 35);
    if (nameEdit) display.print(F("NAME TURN CLICK"));
    else if (sysItem == 0) display.print(used ? F("CLICK TO OVERWRITE") : F("CLICK TO SAVE"));
  }
  if (sysItem == 1) {
    textAt(0, 35);
    display.print(F("CLICK TO LOAD"));
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
    display.print(F("DIAL IN TEMPO"));

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
