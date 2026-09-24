#ifndef _PALETTE_H
#define _PALETTE_H

#include "Platform.h"

struct Palette {
    uint16_t* sourcePalette;
    uint16_t* currentPalette;
    uint16_t colorCount_;
    uint16_t fade_;
    uint16_t fadeBaseColor;
};

void paletteInitialize(void);
struct Palette* paletteCreate(const uint16_t* palette, uint16_t colorCount, uint16_t fade, uint16_t fadeBaseColor);
void paletteDestroy(struct Palette* pal);
void paletteSetPalette(struct Palette* pal, const uint16_t* palette, uint16_t colorCount);
void paletteSetFade(struct Palette* pal, uint16_t fade);
uint16_t paletteFade(const struct Palette* pal);
void paletteSetFadeBaseColor(struct Palette* pal, uint16_t fadeBaseColor);
uint16_t* paletteGet(struct Palette* pal);

#endif