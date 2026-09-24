#include "palette.h"

#include <stdlib.h>

static uint8_t fadeTable[16][16];

static void paletteUpdate(struct Palette* pal);

void paletteInitialize(void)
{
    int fade;
    int value;
    for (fade = 1; fade < 16; fade++) {
        for (value = 0; value < 16; value++) {
            fadeTable[fade][value] = (uint8_t)((fade * value) / 15);
        }
    }
}

struct Palette* paletteCreate(const uint16_t* palette, uint16_t colorCount, uint16_t fade, uint16_t fadeBaseColor)
{
    struct Palette* pal = (struct Palette*)malloc(sizeof(struct Palette));
    if (!pal) return 0;

    pal->sourcePalette = 0;
    pal->currentPalette = 0;
    pal->fade_ = fade;
    pal->fadeBaseColor = fadeBaseColor;

    if (palette) {
        paletteSetPalette(pal, palette, colorCount);

        if (pal->fade_ != 15) {
            paletteUpdate(pal);
        }
    }
    return pal;
}

void paletteDestroy(struct Palette* pal)
{
    if (!pal) return;
    free(pal->sourcePalette);
    free(pal->currentPalette);
    free(pal);
}

void paletteSetPalette(struct Palette* pal, const uint16_t* palette, uint16_t colorCount)
{
    int i;
    if (!pal) return;

    free(pal->sourcePalette);
    free(pal->currentPalette);

    pal->sourcePalette = (uint16_t*)malloc(colorCount * sizeof(uint16_t));
    pal->currentPalette = (uint16_t*)malloc(colorCount * sizeof(uint16_t));
    pal->colorCount_ = colorCount;

    for (i = 0; i < colorCount; i++) {
        pal->sourcePalette[i] = palette[i];
        pal->currentPalette[i] = palette[i];
    }
}

void paletteSetFade(struct Palette* pal, uint16_t fade)
{
    if (!pal) return;
    pal->fade_ = fade;
    paletteUpdate(pal);
}

uint16_t paletteFade(const struct Palette* pal)
{
    return pal ? pal->fade_ : 15;
}

void paletteSetFadeBaseColor(struct Palette* pal, uint16_t fadeBaseColor)
{
    if (pal) pal->fadeBaseColor = fadeBaseColor;
}

uint16_t* paletteGet(struct Palette* pal)
{
    return pal ? pal->currentPalette : 0;
}

static void paletteUpdate(struct Palette* pal)
{
    uint16_t baseR = pal->fadeBaseColor >> 8;
    uint16_t baseG = (pal->fadeBaseColor & 0x0f0) >> 4;
    uint16_t baseB = pal->fadeBaseColor & 0x00f;
    uint16_t i;
    for (i = 0; i < pal->colorCount_; i++) {
        uint16_t color = pal->sourcePalette[i];
        uint16_t r = color >> 8;
        uint16_t g = (color & 0x0f0) >> 4;
        uint16_t b = color & 0x00f;
        int16_t rDelta = r - baseR;
        int16_t gDelta = g - baseG;
        int16_t bDelta = b - baseB;
        uint16_t fadedR = baseR + (rDelta >= 0 ? fadeTable[pal->fade_][rDelta] : -fadeTable[pal->fade_][-rDelta]);
        uint16_t fadedG = baseG + (gDelta >= 0 ? fadeTable[pal->fade_][gDelta] : -fadeTable[pal->fade_][-gDelta]);
        uint16_t fadedB = baseB + (bDelta >= 0 ? fadeTable[pal->fade_][bDelta] : -fadeTable[pal->fade_][-bDelta]);
        pal->currentPalette[i] = (uint16_t)((fadedR << 8) | (fadedG << 4) | fadedB);
    }
}