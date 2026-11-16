#ifndef JARA_NUMEROS_H
#define JARA_NUMEROS_H

#include <Adafruit_GFX.h>

// JARA (pixel font by Joaquín Contreras) @ 8 px: digits 0-9 only, taken from the full
// font file (Lttjara.h). Adafruit GFX format.
// Used for numbers only; the full font took ~1 KB of flash.

const uint8_t JaraNumerosBitmaps[] PROGMEM = {
  0x69, 0x99, 0x99, 0x60, 0x59, 0x24, 0xB8, 0x69, 0x12, 0x48, 0xF0, 0x69, 0x16, 0x19, 0x60, 0x11, 0x95, 0x2F, 0x88, 0x40, 0xF8, 0x8E, 0x11, 0xE0, 0x68, 0x8E, 0x99, 0x60, 0xF1, 0x12, 0x44, 0x40, 0x69, 0x96, 0x99, 0x60, 0x69, 0x97, 0x11, 0x60
};

const GFXglyph JaraNumerosGlyphs[] PROGMEM = {
  {    0, 4, 7, 6, 1, -6 },  // '0'
  {    4, 3, 7, 5, 1, -6 },  // '1'
  {    7, 4, 7, 6, 1, -6 },  // '2'
  {   11, 4, 7, 6, 1, -6 },  // '3'
  {   15, 5, 7, 7, 1, -6 },  // '4'
  {   20, 4, 7, 6, 1, -6 },  // '5'
  {   24, 4, 7, 6, 1, -6 },  // '6'
  {   28, 4, 7, 6, 1, -6 },  // '7'
  {   32, 4, 7, 6, 1, -6 },  // '8'
  {   36, 4, 7, 6, 1, -6 },  // '9'
};

const GFXfont JaraNumeros PROGMEM = {
  (uint8_t *)JaraNumerosBitmaps,
  (GFXglyph *)JaraNumerosGlyphs,
  0x30, 0x39, 9
};

#endif  // JARA_NUMEROS_H
