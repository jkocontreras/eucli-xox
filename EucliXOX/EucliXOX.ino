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
#include "JaraNumeros.h"  // dígitos de JARA, la tipografía de JKO (pixel, 8 px), formato Adafruit GFX

void textAt(int x, int y, byte size = 1);  // ver initDisplay()
void numAt(int x, int y, byte size = 1);

// Configuration flags
//#define ENCODER_REVERSED  // descomentar (o compilar con -DENCODER_REVERSED) si el encoder gira al revés
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
  SETTING_XOX,  // Modo XOX: girar mueve el cursor, clic enciende/apaga el paso, clic largo sale
  SETTING_LAST
};

// For debug / UI
// EUCLID11: formato compacto de memorias con patrón XOX. Las memorias EUCLID10 se convierten al encender.
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
// Memoria temporal: el último estado se guarda solo, unos segundos después del último cambio.
// Ocupa una marca (1 byte) + StoredConfig + byte de verificación.
#define AUTOSAVE_ADDRESS (LAST_USED_SLOT_ADDRESS + 2)
#define AUTOSAVE_MARK 0xA5
#define AUTOSAVE_DELAY_MS 10000UL  // espera sin tocar el encoder antes de guardar

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
// Corte general: mantener apretado el encoder sobre M (fuera del modo mute).
// 0,4 s mutea todo (o restaura la mezcla si ya había corte), 2 s parpadean todos, 4 s desmutea todo.
byte cutMask = 0;    // canales que muteó el corte (0 = no hay corte activo)
byte blinkMask = 0;  // stickers que parpadean
byte holdStage = 0;
unsigned long cutTime = 0;
// Modo mute del menú M: girar elige canal (marco), clic mutea/desmutea, clic largo sale
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


// En PROGMEM para dejar más RAM a la pila (se leen con pgm_read_byte)
const byte x16[16] PROGMEM = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, y16[16] PROGMEM = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//Sequence variables
constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;  // 250;  // 125;  // Used by fast inputs like encoder rotation to throttle the display refresh. (3 steps)
unsigned long gate_timer = 0;
#define LED_FLASH_MS 5  // destello de los LEDs (canales y clock); más corto se percibe más tenue (original: 30)
#define LONG_CLICK_MS 375  // duración del clic largo
#define TRIG_MS 25      // duración de los triggers de salida en el código. La pantalla se redibuja después
                        // del destello y tarda ~25 ms, así que en la práctica duran ~30 ms, como antes.
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
uint16_t offset_buf[MAX_CHANNELS];  // pasos que suenan por canal (bit j = paso j), ya con OFFS aplicado. 12 bytes de RAM en vez de 96.
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

// Estado del ritmo que suena (en RAM)

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

// Patrón XOX por canal: bit s = paso s encendido (antes de aplicar OFFS).
// Bit i de xoxMask = el canal i toca su patrón XOX en vez del euclidiano.
uint16_t xoxPattern[MAX_CHANNELS];
byte xoxMask = 0;
byte xoxCursor[MAX_CHANNELS];

// Formato compacto en EEPROM (41 bytes).
struct StoredConfig {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  byte probability[MAX_CHANNELS];
  byte muteMask;
  byte xoxMask;
  uint16_t xox[MAX_CHANNELS];
  int tempo;
  byte internalClock;  // 0 externo, 1 interno, 2 = no cambia el reloj al cargar (presets)
};

// Formato EUCLID10 (46 bytes por memoria), solo para convertir memorias antiguas.
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

// Estado del guardado automático
unsigned long lastChangeTime = 0;
bool autosavePending = false;
bool autosaveWriting = false;
byte autosaveIndex = 0;

// Nombres de 3 letras por memoria y marca de presets escritos, después de la memoria temporal
#define NAMES_ADDRESS (AUTOSAVE_ADDRESS + sizeof(StoredConfig) + 2)
#define PRESETS_FLAG_ADDRESS (NAMES_ADDRESS + NUM_MEMORY_SLOTS * 3)
#define PRESETS_MARK 0x5D
const char NAME_CHARS[] PROGMEM = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NUM_NAME_CHARS 38
byte nameEdit = 0;  // 0 = no se está nombrando; 1..3 = letra que se edita
char nameBuf[3];

// Presets que se escriben una vez en las memorias 1 a 9 (solo si están con el ritmo inicial o con el preset
// de fábrica viejo sin cambios). Van de lo simple a lo complejo: EUC, BOS y CUB en modo eucli (16 pasos,
// enseñan HITS y OFFS); DEM, CMB y BRK dibujados en XOX; TER (12 pasos) y POL (largos distintos) enseñan LIMI.
// Orden de canales: 1 bombo, 2 caja/clap, 3 hat cerrado, 4 hat abierto/platillo, 5 percusión, 6 percusión/acento.
// Generados con dev/presets/gen_presets.py.
const StoredConfig presets[] PROGMEM = {
  // EUC: HITS: 2 a 7 golpes, figuras casi regulares
  { { 2, 3, 4, 5, 6, 7 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },
  // BOS: OFFS: clave de bossa (5) y samba (7) rotadas
  { { 4, 5, 8, 4, 7, 9 }, { 0, 12, 0, 2, 0, 10 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 120, 2 },
  // CUB: tresillo (6 en 16) y cinquillo (10 en 16)
  { { 4, 6, 8, 4, 10, 2 }, { 0, 5, 0, 2, 3, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x00, { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }, 100, 2 },
  // DEM: dembow
  { { 4, 4, 8, 4, 6, 4 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x1111, 0x4848, 0x5555, 0x4444, 0x4949, 0x4141 }, 95, 2 },
  // CMB: cumbia colombiana clásica
  { { 2, 4, 8, 12, 3, 3 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x0101, 0x4444, 0x5555, 0xDDDD, 0x40C0, 0x0181 }, 95, 2 },
  // BRK: breakbeat de JKO
  { { 2, 2, 3, 4, 1, 1 }, { 0, 10, 0, 10, 11, 4 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 68 }, 0, 0x1E, { 0, 0x0080, 0x05A5, 0x1511, 0x0001, 0x81C2 }, 132, 2 },
  // TER: 12 pasos, pulso ternario con bembé
  { { 4, 2, 12, 4, 7, 2 }, { 0, 0, 0, 0, 0, 0 }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, 0, 0x3F, { 0x0249, 0x0208, 0x0FFF, 0x0924, 0x0AD5, 0x0041 }, 90, 2 },
  // POL: polimetría con probabilidad
  { { 4, 3, 5, 4, 2, 5 }, { 0, 0, 0, 0, 0, 0 }, { 16, 7, 11, 9, 5, 13 }, { 100, 100, 80, 60, 70, 50 }, 0, 0x3F, { 0x1111, 0x0029, 0x02A9, 0x00A9, 0x0009, 0x0949 }, 120, 2 },
  // DIV: divisor de clock (utilidad)
  { { 16, 8, 4, 2, 1, 4 }, { 0, 0, 0, 0, 0, 2 }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, 0, 0, { 0 }, 120, 2 },
};
const char presetNames[] PROGMEM = "EUCBOSCUBDEMCMBBRKTERPOLDIV";
#define NUM_PRESETS (sizeof(presets) / sizeof(StoredConfig))

// Presets de fábrica del Euclid anterior, que guardaba en las memorias 1 a 9 (golpes, desplazamientos, largos)
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

// Splash de inicio, EUCLID + XOX, 80x36px, 360 bytes. Generado desde el dibujo de JKO (lienzo de 128x64)
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

// Splash de inicio: la imagen va en la misma posición que en el lienzo original de 128x64
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
  
  // BPM del clock externo (pulsos = semicorcheas). Se mide en microsegundos, con un promedio
  // móvil de ~4 pulsos y redondeo, para que un clock estable muestre un número estable.
  // (El original medía en ms enteros y truncaba: a 70 BPM alternaba 69 y 70.)
  static unsigned long lastPulseUs = 0;
  static unsigned long avgUs = 0;  // intervalo promedio entre pulsos, en µs
  unsigned long now = micros();
  unsigned long interval = now - lastPulseUs;
  if (lastPulseUs && interval < 2000000UL) {  // ignora el primer pulso y las pausas de más de 2 s
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
      xoxMask &= ~(1 << k);  // cambiar HITS devuelve el canal a eucli
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

  // Vuelve lo último que sonó (memoria temporal). Si no hay una válida, carga la última memoria.
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

  if (gate_timer + TRIG_MS <= millis()) {  // apaga los triggers de salida
    OUTPUT1::setOutput(0);
    OUTPUT2::setOutput(0);
    OUTPUT3::setOutput(0);
    OUTPUT4::setOutput(0);
    OUTPUT5::setOutput(0);
    OUTPUT6::setOutput(0);
  }
  if (gate_timer + LED_FLASH_MS <= millis()) {  // apaga los LEDs de canal y de clock
    CLK_LED::setOutput(0);
    LED1::setOutput(0);
    LED2::setOutput(0);
    LED3::setOutput(0);
    LED4::setOutput(0);
    LED5::setOutput(0);
    LED6::setOutput(0);
  }

  // En XOX la pantalla se refresca cuando cambia la fase del parpadeo del cursor
  // (y mientras parpadean stickers del corte general, más rápido)
  if (selected_setting == SETTING_XOX || blinkMask) {
    static bool blinkPhase = false;
    bool phase = millis() & (blinkMask ? 128 : 512);
    if (phase != blinkPhase) {
      blinkPhase = phase;
      disp_refresh = true;
    }
  }

  corteGeneral();
  if (showOverlay && encoder.isPressed()) disp_refresh = true;  // línea del clic largo en S y L

  // La pantalla tarda ~25 ms en redibujarse y en ese tiempo no se puede apagar nada:
  // espera a que terminen los destellos de los LEDs para que duren LED_FLASH_MS de verdad.
  if (millis() - gate_timer >= LED_FLASH_MS) OLED_display();

  autoguardar();  // memoria temporal
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

// Textos con la tipografía de sistema (x, y = esquina superior izquierda).
void textAt(int x, int y, byte size) {
  display.setFont(NULL);
  display.setTextSize(size);
  display.setCursor(x, y);
}

// Números con JARA. JARA se dibuja desde la línea base: se suma la altura de los dígitos
// (6 px por tamaño) para que x, y sigan siendo la esquina superior izquierda.
void numAt(int x, int y, byte size) {
  display.setFont(&JaraNumeros);
  display.setTextSize(size);
  display.setCursor(x, y + 6 * size);
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
  marcarCambio();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;  // just in case

  if (nameEdit) {
    if (++nameEdit > 3) finishName();  // clic: siguiente letra; después de la tercera, cierra
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
          toggleXoxStep(selected_menu);  // en XOX el clic enciende/apaga el paso bajo el cursor
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
          muteMode = true;   // entra al modo mute
        } else {
          currentConfig.mute[muteCursor] = !currentConfig.mute[muteCursor];
          cutMask = 0;       // la mezcla guardada por el corte ya no corresponde
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
  marcarCambio();
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;

  // If we're in the parameter settings (Hits, Offset, Limit, etc.) for one of the channels
  if (selected_menu <= MENU_CH_6 && selected_setting == SETTING_XOX) {
    selected_setting = SETTING_TOP_MENU;  // clic largo sale de XOX
  } else if (selected_menu <= MENU_CH_6 && selected_setting != SETTING_TOP_MENU) {
    // Decrement the selected menu to allow back navigation
    selected_setting = static_cast<Setting>((selected_setting - 1 + SETTING_LAST) % SETTING_LAST);
  } else if (showOverlay) {
    // Handle specific overlay actions
    switch (selected_menu) {
      case MENU_SAVE:
        if (nameEdit) {
          finishName();  // clic largo mientras se nombra: cierra
        } else {
          bool used = slotUsed(selected_slot);
          saveToEEPROM(selected_slot);
          for (byte k = 0; k < 3; k++) nameBuf[k] = used ? nameChar(selected_slot, k) : '-';
          nameEdit = 1;  // después de guardar se nombra la memoria
        }
        break;
      case MENU_LOAD:
        loadFromEEPROM(selected_slot);
        showOverlay = false;                         // Turn off the overlay
        break;
    }
  } else if (selected_menu == MENU_ALL_MUTE) {
    if (muteMode) {  // sale del modo mute y salta al canal 1 (el siguiente al girar)
      muteMode = false;
      selected_menu = MENU_CH_1;
    }
    // fuera del modo, el corte general se maneja mientras se mantiene apretado (en loop)
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
  marcarCambio();
  disp_refresh = true;

  if (muteMode) {  // en el modo mute, girar mueve el marco de a un canal
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
        if (nameEdit) {  // girar cambia la letra
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
  marcarCambio();
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
      // Largo del ciclo: índice 0..5 de bar_max = 2, 4, 6, 8, 12, 16 compases.
      // (El original iba de 1 a 6: nunca llegaba a 2 compases y en 6 leía fuera de la tabla.)
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

// Posición del cursor dentro del largo (LIMI) del canal
byte xoxPos(byte ch) {
  byte len = currentConfig.limit[ch];
  if (len == 0 || len > MAX_STEPS) len = MAX_STEPS;
  return xoxCursor[ch] % len;
}

// Vuelve a eucli. HITS parte del número de golpes que tenía el dibujo.
void leaveXox(byte ch) {
  if (!isXox(ch)) return;
  xoxMask &= ~(1 << ch);
  currentConfig.hits[ch] = __builtin_popcount(xoxPattern[ch]);
}

// Enciende o apaga el paso bajo el cursor. La primera vez el canal pasa a XOX
// y el dibujo parte del ritmo euclidiano que está sonando.
void toggleXoxStep(byte ch) {
  if (!isXox(ch)) {
    uint16_t p = 0;
    for (byte s = 0; s < MAX_STEPS; s++) {
      if (pgm_read_byte(&euc16[currentConfig.hits[ch]][s])) p |= 1u << s;
    }
    xoxPattern[ch] = p;
    xoxMask |= 1 << ch;
  }
  // El cursor recorre los pasos tal como suenan; OFFS gira el dibujo.
  xoxPattern[ch] ^= 1u << ((xoxPos(ch) + currentConfig.offset[ch]) % MAX_STEPS);
}

//------------------- Guardado automático ---------------------

byte checksum8(const byte *p, byte n) {
  byte c = 0x5A;
  while (n--) c = ((c << 1) | (c >> 7)) ^ *p++;
  return c;
}

void marcarCambio() {
  lastChangeTime = millis();
  autosavePending = true;
  autosaveWriting = false;  // si estaba escribiendo, empieza de nuevo después de la espera
}

// Se llama en cada vuelta de loop(). Escribe un byte por vuelta y solo si la EEPROM está libre,
// así el ritmo no se detiene. EEPROM.update solo escribe los bytes que cambiaron.
void autoguardar() {
  if (!autosaveWriting) {
    if (!autosavePending || millis() - lastChangeTime < AUTOSAVE_DELAY_MS) return;
    autosaveCheck = 0x5A;  // checksum8 se va calculando byte a byte
    autosaveIndex = 0;
    autosavePending = false;
    autosaveWriting = true;
  }
  if (!eeprom_is_ready()) return;
  byte value;
  if (autosaveIndex == 0) value = AUTOSAVE_MARK;
  else if (autosaveIndex <= sizeof(StoredConfig)) {
    // Sin búfer global (41 bytes de RAM para la pila): se empaca en cada vuelta y se toma un byte.
    // loop() llama a autoguardar() después de dibujar, así que esta copia no se suma a la pila de la pantalla.
    StoredConfig s;
    packConfig(s);
    value = ((const byte *)&s)[autosaveIndex - 1];
    autosaveCheck = ((autosaveCheck << 1) | (autosaveCheck >> 7)) ^ value;
  } else value = autosaveCheck;  // el byte de verificación va al final
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

//------------------ Formato de memorias ----------------------

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

// Convierte las memorias EUCLID10 (46 bytes) al formato compacto (41 bytes).
// Las memorias nuevas empiezan antes que las antiguas, así que cada una se lee completa antes de pisarla.
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
      leaveXox(selected_menu);  // cambiar HITS devuelve el canal a eucli
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
    case SETTING_XOX: {  // girar mueve el cursor de a un paso
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

// Primer encendido sin memorias reconocibles: las 20 memorias empiezan con un ritmo básico
// (4 golpes en 16 pasos por canal, 120 BPM, reloj externo).
void defaultConfig(StoredConfig &s) {
  memset(&s, 0, sizeof(s));
  memset(s.hits, 4, MAX_CHANNELS);
  memset(s.limit, MAX_STEPS, MAX_CHANNELS);
  memset(s.probability, 100, MAX_CHANNELS);
  s.tempo = 120;
}

// Una memoria cuenta como vacía si tiene exactamente el ritmo inicial.
// Compara byte a byte desde la EEPROM, sin copiar la memoria a la pila: se llama mientras se dibuja
// la pantalla de S y L, y dos StoredConfig en la pila alcanzaban el búfer de la pantalla
// (volvía el cuadradito de píxeles animado abajo a la derecha).
bool slotUsed(byte slot) {
  int a = EEPROM_START_ADDRESS + slot * CONFIG_SIZE;
  for (byte i = 0; i < sizeof(StoredConfig); i++) {
    byte e = i < 6 ? 4 : i < 12 ? 0 : i < 18 ? MAX_STEPS : i < 24 ? 100 : i == 38 ? 120 : 0;  // ritmo inicial
    if (EEPROM.read(a + i) != e) return true;
  }
  return false;
}

// La memoria tiene el preset de fábrica viejo de esa posición, sin cambios (viene del firmware anterior)
bool isOldFactory(byte slot) {
  StoredConfig s;
  byte ref[18];
  EEPROM.get(EEPROM_START_ADDRESS + slot * CONFIG_SIZE, s);
  memcpy_P(ref, oldFactory[slot], 18);
  for (byte c = 0; c < MAX_CHANNELS; c++)
    if (s.probability[c] != 100) return false;
  return !s.muteMask && !s.xoxMask && !memcmp(&s, ref, 18);
}

// Escribe los presets una sola vez, sin pisar memorias que tengan algo propio
// (sí reemplaza el ritmo inicial y los presets de fábrica viejos sin cambios)
void writePresets() {
  if (EEPROM.read(PRESETS_FLAG_ADDRESS) == PRESETS_MARK) return;
  // primera vez: la zona de nombres puede tener restos del formato anterior; todos quedan en guion
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

// Letra de un nombre guardado; lo que no está en NAME_CHARS (EEPROM sin escribir) se ve como guion
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
  marcarCambio();
  disp_refresh = true;
}

// Corte general sobre M, por tiempo apretado (el clic largo de la librería solo avisa al soltar)
void corteGeneral() {
  if (selected_menu == MENU_ALL_MUTE && !muteMode && !showOverlay && encoder.isPressed()) {
    unsigned long d = encoder.currentDuration();
    if (holdStage == 0 && d >= 400) {
      if (cutMask) {  // ya había corte: vuelve la mezcla anterior
        setMutes(mutedMask() & ~cutMask);
        cutMask = 0;
        holdStage = 3;
      } else {        // corta: mutea todo y parpadean los recién muteados
        cutMask = ~mutedMask() & 0x3F;
        blinkMask = cutMask;
        cutTime = millis();
        setMutes(0x3F);
        holdStage = 1;
      }
    } else if (holdStage == 1 && d >= 2000) {
      blinkMask = 0x3F;  // sigue apretado: parpadean todos
      holdStage = 2;
    } else if (holdStage == 2 && d >= 4000) {
      setMutes(0);       // y a los 4 s se desmutea todo
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

// Textos verticales de los costados: tipografía de sistema (ancho fijo, las letras quedan alineadas).
// El resto de la pantalla usa JARA.
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
      if (muteMode) {  // dentro del modo mute: MUTE invertido en una franja blanca
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
    migrateLegacySlots();  // memorias del firmware anterior: se convierten
  } else {
    initializeDefaultRhythms();  // EEPROM sin formato conocido: ritmo básico en todas las memorias
  }
  EEPROM.update(AUTOSAVE_ADDRESS, 0);  // todavía no hay memoria temporal válida
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
  // PROB no tiene triángulo: caía fuera de la pantalla (fila 66 de 64)
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
    if (muteMode) {  // cursor del modo mute
      if (currentConfig.mute[muteCursor]) {
        display.drawCircle(graph_x[muteCursor] + 15, graph_y[muteCursor] + 15, 11, WHITE);  // anillo fino alrededor del sticker
      } else {
        drawMuteSticker(muteCursor, false);  // sticker con borde, encima de la figura
      }
    }
  }
}

// Cursor XOX: rombo relleno sobre el paso. Parpadea si el paso está encendido, queda fijo si está apagado.
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
  // Se traza punto a punto, sin copiar los puntos a arreglos (ahorra 34 bytes de RAM para la pila).
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] || selected_setting == SETTING_LIMIT) continue;  // en LIMI no se dibuja la figura
    int8_t first = -1, prev = -1;
    for (byte m = 0; m < MAX_STEPS; m++) {
      if (!stepOn(k, m)) continue;
      if (prev < 0) first = m;
      else display.drawLine(pgm_read_byte(&x16[prev]) + graph_x[k], pgm_read_byte(&y16[prev]) + graph_y[k], pgm_read_byte(&x16[m]) + graph_x[k], pgm_read_byte(&y16[m]) + graph_y[k], WHITE);
      prev = m;
    }
    if (first >= 0) {
      // Cierra la figura. Con un solo paso sonando (eucli o XOX), línea del centro del anillo a ese paso.
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

  // Canal muteado: sticker blanco relleno con la M en negro (en LIMI se ve el número de pasos)
  for (byte k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k] && selected_setting != SETTING_LIMIT)
      drawMuteSticker(k, !((blinkMask >> k) & 1 && (millis() & 128)));  // parpadeo: relleno / borde
  }

  // draw channel info in edit mode, should be helpful while editing.
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;


    // Con más de 8 golpes la figura ya no se distingue: se muestra el número al centro, igual que en LIMI.
    // En XOX cuenta los pasos encendidos del dibujo.
    byte nHits = isXox(ch) ? __builtin_popcount(xoxPattern[ch]) : currentConfig.hits[ch];
    if (currentConfig.mute[ch] == 0 && nHits > 8 && selected_setting != SETTING_LIMIT ) {
      drawRingNumber(ch, nHits);
    }

    // draw selected parameter UI for currently active channel when editing
    if (selected_setting != SETTING_TOP_MENU) {
      switch (selected_setting) {
        case SETTING_HITS: break;
        case SETTING_OFFSET: break;
        case SETTING_LIMIT:
          drawRingNumber(ch, currentConfig.limit[ch]);  // número de pasos al centro del anillo
          break;
      }
    }
  }
}

// Número al centro del anillo de un canal, en JARA. JARA se dibuja desde la línea base:
// los números miden 7 px de alto, así que con la base en y+18 quedan centrados en el anillo (centro y+15).
// (Se probó al doble, 14 px, y quedó muy grande.)
void drawRingNumber(byte ch, byte n) {
  numAt(graph_x[ch] + (n > 9 ? 10 : 13), graph_y[ch] + 12);
  display.print(n);
}

// Sticker de mute: círculo de 19 px al centro del anillo con una M (tipografía de sistema, 5 x 7 px).
// filled = canal muteado (blanco con M negra); si no, borde blanco, interior negro y M blanca.
void drawMuteSticker(byte ch, bool filled) {
  int cx = graph_x[ch] + 15, cy = graph_y[ch] + 15;
  display.fillCircle(cx, cy, 9, filled ? WHITE : BLACK);
  display.drawCircle(cx, cy, 9, WHITE);
  display.setTextColor(filled ? BLACK : WHITE);
  textAt(cx - 2, cy - 3);
  display.print('M');
  display.setTextColor(WHITE);
}

// BPM en vertical en la columna izquierda (vacía cuando estás sobre un canal).
// Reloj interno: el tempo ajustado. Externo: lo medido en CLK ("---" sin señal).
void drawBpmColumn() {
  int bpm = internalClock ? tempo : externalBPM;
  char d[5] = "---";
  if (bpm > 0) itoa(bpm, d, 10);
  leftMenu(d[0], d[1], d[2] ? d[2] : ' ', ' ');
}

void drawProbabilityConfig() {
  if (selected_setting != SETTING_PROB) return;  // Exit early
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int barWidth = 4;
    int maxHeight = 15;
    int margin = 2;

    int bar_x = graph_x[ch] + 12;
    int bar_y = graph_y[ch] + 30;

    int barHeight = map(currentConfig.probability[ch], 0, 100, 0, maxHeight);
    int startY = bar_y - barHeight;

    int outerWidth = barWidth + 2 * margin;
    int outerHeight = maxHeight + 2 * margin;
    int outerX = bar_x - margin;
    int outerY = bar_y - maxHeight - margin;

    int text_x = outerX + (outerWidth / 2) - 6;
    int text_y = outerY - 10;

    text_x = constrain(text_x, 0, 128);
    text_y = constrain(text_y, 0, 64);

    numAt(text_x, text_y);
    display.print(currentConfig.probability[ch]);

    display.drawRoundRect(outerX, outerY, outerWidth, outerHeight, 2, WHITE);
    display.fillRoundRect(bar_x, startY, barWidth, barHeight, 2, WHITE);
  }
}

//  Display selected slot
void drawSaveLoadSelection() {
  textAt(0, 0);
  display.print(selected_menu == MENU_SAVE ? F("Save to Slot") : F("Load from Slot"));
  {
    numAt(0, 14, 2);
    display.print(selected_slot + 1, DEC);
    // memoria con algo guardado: círculo relleno; con el ritmo inicial: solo el borde
    bool used = slotUsed(selected_slot);
    if (used) display.fillCircle(32, 20, 4, WHITE);
    else display.drawCircle(32, 20, 4, WHITE);
    textAt(44, 14, 2);
    for (byte k = 0; k < 3; k++) display.print(nameEdit ? nameBuf[k] : nameChar(selected_slot, k));
    if (nameEdit) display.drawFastHLine(44 + (nameEdit - 1) * 12, 31, 10, WHITE);  // letra que se edita
    textAt(0, 35);
    if (nameEdit) display.print(F("Name: turn, click"));
    else if (selected_menu == MENU_SAVE) display.print(used ? F("long PRESS Overwrite") : F("long PRESS Save"));
  }
  if (selected_menu == MENU_LOAD) {
    textAt(0, 35);
    display.print(F("long PRESS Load"));
  }
  // Mientras se mantiene apretado, una línea se llena bajo el texto; llena = al soltar guarda o carga
  if (!nameEdit && encoder.isPressed()) {
    uint16_t d = encoder.currentDuration() >> 2;  // en unidades de 4 ms
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
    int filledWidth = (long)(tempo - 40) * (w - 22) / (240 - 40);  // margin on each side (sin float: ahorra flash)

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
    if (externalBPM == 0) {  // sin señal en CLK
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
