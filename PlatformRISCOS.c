/**
 * PlatformRISCOS - RISC OS Wimp (desktop) implementation
 *
 * Displays the game in a 640x400 Wimp window, scaled 2x from a 320x200
 * 8-bit chunky buffer using OS_SpriteOp PutSpriteScaled (SWI reason 52).
 * Keys are polled with OS_Byte 121 (single-key test) using internal key
 * numbers. Sound effects are played through OS_Sound; music is played
 * through MODPlay, ticked once per 50 Hz frame and steered onto the
 * RISC OS voices with Sound_Control.
 *
 * Files are read with fopen() using '.' directory separators (RISC OS
 * convention). The application must be run with its current directory
 * set so that the "Amiga", "Sounds" and "Music" directories are found.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"

#include "palette.h"
#include "Platform.h"

#include "modplay.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define SCREEN_WIDTH_IN_CHARS (SCREEN_WIDTH / 8)
#define SCREEN_HEIGHT_IN_CHARS (SCREEN_HEIGHT / 8)
#define PLANES 4
#define FONT_SIZE (256 * 8)
#define TILE_PIXEL_SIZE (24 * 24)
#define NUM_TILES 256
#define ANIM_TILES 17
#define SPRITE_COUNT 77

// RISC OS SWI numbers (OS SWIs use low numbers; Wimp/ColourTrans use &4xxxxx)
#define OS_Byte 0x06
#define OS_Sound 0x1c
#define OS_SpriteOp 0x2e
#define OS_ReadPalette 0x2f
#define OS_ReadVduVariables 0x31
#define OS_ReadMonotonicTime 0x42

#define Wimp_Initialise 0x400c0
#define Wimp_CreateWindow 0x400c1
#define Wimp_CloseWindow 0x400c6
#define Wimp_OpenWindow 0x400c5
#define Wimp_Poll 0x400c7
#define Wimp_RedrawWindow 0x400c8
#define Wimp_GetRectangle 0x400ca
#define Wimp_GetWindowState 0x400cb
#define Wimp_ForceRedraw 0x400d1
#define Wimp_PollIdle 0x400e1
#define Wimp_CloseDown 0x400dd
#define Wimp_SetMode 0x400e2

// Asset paths
static const char* FONT_PATH = "Amiga/C64Font.raw";
static const char* TILES_PATH = "Amiga/Tiles.raw";
static const char* ANIM_TILES_PATH = "Amiga/AnimTiles.raw";
static const char* FACES_PATH = "Amiga/Faces.raw";
static const char* ITEMS_PATH = "Amiga/Items.raw";
static const char* KEYS_PATH = "Amiga/Keys.raw";
static const char* HEALTH_PATH = "Amiga/Health.raw";
static const char* SPRITES_PATH = "Amiga/Sprites.raw";
static const char* SPRITES_MASK_PATH = "Amiga/SpritesMask.raw";
static const char* TILESET_PATH = "tileset.amiga";

static const char* IMAGE_PATHS[] = {
    "Amiga/Data/IntroScreen.raw",
    "Amiga/Data/GameScreen.raw",
    "Amiga/Data/GameOver.raw"
};

static const char* MODULE_PATHS[] = {
    "Music/mod.metal heads",
    "Music/mod.win",
    "Music/mod.lose",
    "Music/mod.metallic bop amiga",
    "Music/mod.get psyched",
    "Music/mod.robot attack",
    "Music/mod.rushin in",
    "Music/mod.soundfx"
};

static const char* SFX_PATHS[] = {
    "Sounds/sounds_dsbarexp.raw",
    "Sounds/SOUND_BEEP.raw",
    "Sounds/SOUND_MEDKIT.raw",
    "Sounds/SOUND_EMP.raw",
    "Sounds/SOUND_MAGNET2.raw",
    "Sounds/SOUND_SHOCK.raw",
    "Sounds/SOUND_MOVE.raw",
    "Sounds/SOUND_PLASMA_FASTER.raw",
    "Sounds/sounds_dspistol.raw",
    "Sounds/SOUND_FOUND_ITEM.raw",
    "Sounds/SOUND_ERROR.raw",
    "Sounds/SOUND_CYCLE_WEAPON.raw",
    "Sounds/SOUND_CYCLE_ITEM.raw",
    "Sounds/SOUND_DOOR_FASTER.raw",
    "Sounds/SOUND_BEEP2.raw",
    "Amiga/SquareWave.raw"
};

static char notEnoughMemoryError[] = "Not enough memory to run\n";
static char unableToInitDisplay[] = "Unable to initialize display\n";
static char unableToLoadData[] = "Unable to load data\n";

// RISC OS virtual key codes.
//   letters/digits     -> ASCII
//   space 0x20, return 0x0d, tab 0x09, escape 0x1b, backspace 0x08
//   cursor up/down/left/right -> 0x01..0x04
//   F1..F12            -> 0x10..0x1b
//   shift (when held) ORs in 0x80 for every code
static uint8_t standardControls[] = {
    'w', 's', 'a', 'd',      // move up/down/left/right
    'i', 'k', 'j', 'l',      // fire up/down/left/right
    'q', 'e',                // cycle weapons/items
    'u',                     // use
    'f',                     // search
    'o',                     // move object
    'm', 'v',                // live map / live map robots
    'p',                     // pause
    'm' | 0x80,              // music (shift-M)
    'c' | 0x80,              // cheat (shift-C)
    0x01, 0x02, 0x03, 0x04,  // cursor up/down/left/right
    0x20,                    // space
    0x0d,                    // return
    'y', 'n'                 // yes / no
};

// Anim tile map (matches Amiga version)
static int8_t animTileMapDefault[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16,
    -1, -1, -1, -1,  4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,  8, 10, -1, -1, 12, 14, -1, -1, 20, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

// Sprite tile map (matches Amiga version)
static int8_t spriteTileMap[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1, 49, 50, 57, 58, 59, 60, -1, -1, -1, -1, -1, -1, -1, 48,
    -1, -1, -1, 73, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  0,  3, -1, 53, 54, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, 76, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static uint16_t blackPalette[16] = { 0 };

static char MAPNAME[] = "Amiga/Data/level-a";

// Tileset data loaded from tileset.amiga
static uint8_t tilesetData[514];

// Address map for screen character -> pixel offset
static uint32_t addressMap[SCREEN_WIDTH_IN_CHARS * SCREEN_HEIGHT_IN_CHARS];

// Internal key numbers (HdrSrc hdr/Keyboard) -> RISC OS virtual key code
struct KeyMapEntry {
    uint8_t virt;
    int internal;
};

static const struct KeyMapEntry keyMap[] = {
    {
    16, 'q' 
}, { 33, 'w' }, { 34, 'e' }, { 51, 'r' }, { 35, 't' },
    { 68, 'y' }, { 53, 'u' }, { 37, 'i' }, { 54, 'o' }, { 55, 'p' },
    { 65, 'a' }, { 81, 's' }, { 50, 'd' }, { 67, 'f' }, { 83, 'g' },
    { 84, 'h' }, { 69, 'j' }, { 70, 'k' }, { 86, 'l' }, { 97, 'z' },
    { 66, 'x' }, { 82, 'c' }, { 99, 'v' }, { 100, 'b' }, { 85, 'n' },
    { 101, 'm' },
    { 48, '1' }, { 49, '2' }, { 17, '3' }, { 18, '4' }, { 19, '5' },
    { 52, '6' }, { 36, '7' }, { 21, '8' }, { 38, '9' }, { 39, '0' },
    { 57, 0x01 }, { 41, 0x02 }, { 25, 0x03 }, { 121, 0x04 },   // cursors
    {
    73, 0x0d 
}, { 98, 0x20 }, { 96, 0x09 }, { 112, 0x1b },
    { 47, 0x08 }, { 105, 0x08 },
    { 113, 0x10 }, { 114, 0x11 }, { 115, 0x12 }, { 20, 0x13 },
    { 116, 0x14 }, { 117, 0x15 }, { 22, 0x16 }, { 118, 0x17 },
    { 119, 0x18 }, { 30, 0x19 }, { 28, 0x1a }, { 29, 0x1b },
    { 32, 0x1c },
    { 23, 0x2d }, { 120, 0x5c }
};
static const int keyMapSize = (int)(sizeof(keyMap) / sizeof(keyMap[0]));

// ============================================================
// Planar (4-bitplane interleaved) to 8-bit chunky conversion
// ============================================================
static void planarToChunky(const uint8_t* src, uint8_t* dst, int width, int height)
{
    int bytesPerRow = (width + 7) / 8;
    {
    int y;
    for (y = 0; y < height; y++) {
    {
    int x;
    for (x = 0; x < width; x++) {
    uint8_t color = 0;
    {
    int plane;
    for (plane = 0; plane < PLANES; plane++) {
    int byteOffset;
    int bit;
    byteOffset = (y * bytesPerRow * PLANES) + (plane * bytesPerRow) + (x / 8);
    bit = 7 - (x & 7);
    if (src[byteOffset] & (1 << bit)) {
    color |= (1 << plane);
}
} 







}
    dst[y * width + x] = color;
} 



}
} 

}
}

// Translate "/" directory separators to "." (RISC OS convention)
static void translatePath(char* out, int outSize, const char* in)
{
    int i = 0;
    for (; i < outSize - 1 && in[i]; i++) {
    out[i] = (in[i] == '/') ? '.' : in[i];
}
    out[i] = 0;
}

// ============================================================
// PlatformRISCOS implementation
// ============================================================
// PlatformRISCOS implementation (plain C)
// ============================================================

// Platform state (previously the C++ member variables)
static int framesPerSecond_ = 50;
static int taskHandle_ = -1;
static int windowHandle_ = -1;
static bool windowCalibrated_ = false;
static bool windowOpened_ = false;
static uint8_t* createBlock_ = 0;
static uint8_t* openBlock_ = 0;
static uint8_t* pollBlock_ = 0;
static uint8_t* sprite_ = 0;
static uint8_t* pixtrans_ = 0;
static int screenLog2BPP_ = 0;
static int screenLog2BPC_ = 0;
static int screenOSWidth_ = 0;
static int screenOSHeight_ = 0;
static bool trueColour_ = false;
static uint8_t* screenMemory = 0;
static uint8_t* chunkyBuffer = 0;
static uint8_t* fontData = 0;
static uint8_t* tileChunky[256];
static bool tileHasTransparency[256];
static int8_t animTileMap[256];
static uint8_t* itemChunky = 0;
static uint16_t itemWidth = 0;
static uint16_t itemHeight = 0;
static uint16_t itemCount = 0;
static uint8_t* keyChunky = 0;
static uint16_t keyWidth = 0;
static uint16_t keyHeight = 0;
static uint16_t keyCount = 0;
static uint8_t* healthChunky = 0;
static uint16_t healthWidth = 0;
static uint16_t healthHeight = 0;
static uint16_t healthCount = 0;
static uint8_t* faceChunky = 0;
static uint16_t faceWidth = 0;
static uint16_t faceHeight = 0;
static uint16_t faceCount = 0;
static uint8_t* spriteChunky = 0;
static uint16_t spriteWidth = 0;
static uint16_t spriteHeight = 0;
static uint16_t spriteCount = 0;
static uint8_t* animTileChunky = 0;
static uint8_t* tilesMask = 0;
static uint8_t tileLiveMap[256];
static uint8_t liveMapBuffer[128 * 64];
static int cursorX_ = 0;
static int cursorY_ = 0;
static bool cursorVisible_ = false;
static CursorShape cursorShape_ = ShapeUse;
static uint8_t cursorData[28 * 32];
static Module loadedModule = ModuleSoundFX;
static bool audioInitialized_ = false;
static void* modStatus_ = 0;
static uint8_t* musicBuffer_ = 0;
static uint32_t musicBufferSize_ = 0;
static uint32_t musicLen_ = 0;
static bool modActive_ = false;
static bool modPaused_ = false;
static uint8_t shakeStep_ = 0;
static int16_t shakeOffsetX_ = 0;
static uint16_t fadeIntensity_ = 15;
static uint8_t keyToReturn_ = 0xff;
static uint8_t downKey_ = 0xff;
static uint8_t shift_ = 0;
static int lastHeldInternal_ = -1;
static void (*interrupt_)(void) = 0;
static uint32_t clock_ = 0;
static uint32_t frameCount_ = 0;
static struct Palette* palette = 0;
static uint16_t amigaPalette[16];
static uint8_t* loadBuffer_ = 0;
static uint32_t loadBufferSize_ = 0;
static bool preloaded_ = false;
static uint8_t* preloadedModuleData_[8];
static uint32_t preloadedModuleLengths_[8];

int platformInit(void)
{
    memset(tileChunky, 0, sizeof(tileChunky));
    memset(tileHasTransparency, 0, sizeof(tileHasTransparency));
    memcpy(animTileMap, animTileMapDefault, sizeof(animTileMap));
    memset(amigaPalette, 0, sizeof(amigaPalette));
    memset(preloadedModuleData_, 0, sizeof(preloadedModuleData_));
    memset(preloadedModuleLengths_, 0, sizeof(preloadedModuleLengths_));
    paletteInitialize();
    palette = paletteCreate(blackPalette, 16, 0, 0);
    {
    int y, i;
    for (y = 0, i = 0; y < SCREEN_HEIGHT_IN_CHARS; y++) {
    {
    int x;
    for (x = 0; x < SCREEN_WIDTH_IN_CHARS; x++, i++) {
    addressMap[i] = y * SCREEN_WIDTH * 8 + x * 8;
} 



}
} 

}
    platformInitWimp();
    if (taskHandle_ < 0) {
    return 0;
}
    {
    int vars[5] = { 9, 4, 5, 11, 12 };
    int vals[5] = { 0, 0, 0, 0, 0 };
    _kernel_swi_regs in, out;
    in.r[0] = 5;
    in.r[1] = (int)vars;
    in.r[2] = (int)vals;
    _kernel_swi(OS_ReadVduVariables, &in, &out);
    screenLog2BPP_ = vals[0];
    screenOSWidth_ = (vals[3] + 1) << vals[1];
    screenOSHeight_ = (vals[4] + 1) << vals[2];
    if (screenOSWidth_ < 640) screenOSWidth_ = 1280;
    if (screenOSHeight_ < 400) screenOSHeight_ = 960;
}
    sprite_ = (uint8_t*)malloc(44 + SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!sprite_) {
    printf("%s", notEnoughMemoryError);
    return 0;
}
    memset(sprite_, 0, 44 + SCREEN_WIDTH * SCREEN_HEIGHT);
    memset(sprite_, 0, 44);
    strncpy((char*)sprite_, "PETSCIIRobots", 12);
    ((int*)sprite_)[3] = SCREEN_WIDTH / 4 - 1;
    ((int*)sprite_)[4] = SCREEN_HEIGHT - 1;
    ((int*)sprite_)[5] = 0;
    ((int*)sprite_)[6] = 7;
    ((int*)sprite_)[7] = 21;
    ((int*)sprite_)[8] = 44;
    chunkyBuffer = sprite_ + 44;
    pixtrans_ = (uint8_t*)malloc(256);
    if (!pixtrans_) {
    printf("%s", notEnoughMemoryError);
    return 0;
}
    memset(pixtrans_, 0, 256);
    {
    int i;
    for (i = 0; i < NUM_TILES; i++) {
    tileChunky[i] = 0;
} 

}
    createBlock_ = (uint8_t*)malloc(100);
    openBlock_ = (uint8_t*)malloc(100);
    pollBlock_ = (uint8_t*)malloc(512);
    if (!createBlock_ || !openBlock_ || !pollBlock_) {
    printf("%s", notEnoughMemoryError);
    return 0;
}
    memset(createBlock_, 0, 100);
    memset(openBlock_, 0, 100);
    memset(pollBlock_, 0, 512);
    memset(createBlock_, 0, 100);
    ((int*)createBlock_)[7] = (1 << 1) | 0x86000000;
    createBlock_[35] = 0;
    ((int*)createBlock_)[10] = 0;
    ((int*)createBlock_)[11] = -10000;
    ((int*)createBlock_)[12] = 640;
    ((int*)createBlock_)[13] = 0;
    createBlock_[68] = 640 & 0xff;
    createBlock_[69] = (640 >> 8) & 0xff;
    createBlock_[70] = 500 & 0xff;
    createBlock_[71] = (500 >> 8) & 0xff;
    strncpy((char*)(createBlock_ + 72), "PETSCII Robots", 12);
    ((int*)createBlock_)[21] = 0;
    {
    _kernel_swi_regs in, out;
    _kernel_oserror * err;
    in.r[1] = (int)createBlock_;
    err = _kernel_swi(Wimp_CreateWindow, &in, &out);
    if (err) {
    printf("%s", unableToInitDisplay);
    return 0;
}
    windowHandle_ = out.r[0] & 0xffff;
}
    ((int*)createBlock_)[0] = windowHandle_;
    platformOpenGameWindow();
    fontData = (uint8_t*)malloc(FONT_SIZE);
    if (!fontData) {
    printf("%s", notEnoughMemoryError);
    return 0;
}
    platformLoadRawFile(FONT_PATH, fontData, FONT_SIZE);
    loadBufferSize_ = 200000;
    loadBuffer_ = malloc(loadBufferSize_);
    if (!loadBuffer_) {
    printf("%s", notEnoughMemoryError);
    return 0;
}
    platformInitAudio();
    platformLoadAssets();
    platformQuit = 0;
    return 1;
}

void platformShutdown(void)
{
    platformStopModule();
    platformCleanupAudio();
    free(musicBuffer_);
    musicBuffer_ = 0;
    {
    int i;
    for (i = 0; i < NUM_TILES; i++) {
    free(tileChunky[i]);
} 

}
    free(animTileChunky);
    free(itemChunky);
    free(keyChunky);
    free(healthChunky);
    free(faceChunky);
    free(spriteChunky);
    free(tilesMask);
    paletteDestroy(palette);
    free(screenMemory);
    free(fontData);
    free(loadBuffer_);
    free(sprite_);
    free(pixtrans_);
    free(createBlock_);
    free(openBlock_);
    free(pollBlock_);
    if (windowOpened_) {
    _kernel_swi_regs in, out;
    in.r[1] = windowHandle_;
    _kernel_swi(Wimp_CloseWindow, &in, &out);
    windowOpened_ = false;
}
    if (taskHandle_ >= 0) {
    _kernel_swi_regs in, out;
    in.r[0] = taskHandle_;
    in.r[1] = 0x4b534154;
    _kernel_swi(Wimp_CloseDown, &in, &out);
    taskHandle_ = -1;
}
}

// ============================================================
// Wimp / display helpers
// ============================================================

void platformInitWimp()
{
    _kernel_oserror * err;
    _kernel_swi_regs in, out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.r[0] = 310;
    in.r[1] = 0x4b534154;
    in.r[2] = (int)"PETSCII Robots";
    in.r[3] = 0;
    err = _kernel_swi(Wimp_Initialise, &in, &out);
    if (err) {
    return;
}
    taskHandle_ = out.r[1];
}

uint32_t platformMonotonicTime()
{
    _kernel_swi_regs in, out;
    memset(&in, 0, sizeof(in));
    in.r[0] = 0;
    _kernel_swi(OS_ReadMonotonicTime, &in, &out);
    return (uint32_t)out.r[0];
}

void platformOpenGameWindow()
{
    int winWidth;
    int titleGuess;
    int winHeight;
    int x0;
    int y1;
    int y0;
    _kernel_swi_regs in, out;
    if (windowHandle_ < 0) return;
    winWidth = 640;
    titleGuess = 48;
    winHeight = 400 + titleGuess;
    x0 = (int)((screenOSWidth_ - winWidth) / 2);
    if (x0 < 0) x0 = 0;
    y1 = screenOSHeight_ - 8;
    if (y1 < winHeight) y1 = winHeight + 8;
    y0 = y1 - winHeight;
    ((int*)openBlock_)[0] = windowHandle_;
    ((int*)openBlock_)[1] = x0;
    ((int*)openBlock_)[2] = y0;
    ((int*)openBlock_)[3] = x0 + winWidth;
    ((int*)openBlock_)[4] = y1;
    ((int*)openBlock_)[5] = 0;
    ((int*)openBlock_)[6] = 0;
    ((int*)openBlock_)[7] = -1;
    ((int*)openBlock_)[8] = (1 << 1) | 0x86000000;
    ((int*)openBlock_)[9] = 0;
    ((int*)openBlock_)[10] = -10000;
    ((int*)openBlock_)[11] = 640;
    ((int*)openBlock_)[12] = 0;
    in.r[1] = (int)openBlock_;
    _kernel_swi(Wimp_OpenWindow, &in, &out);
    windowOpened_ = true;
    windowCalibrated_ = false;
}

void platformBuildPixtrans()
{
    int nColours = 1 << screenLog2BPP_;
    if (nColours < 2) nColours = 2;
    if (nColours > 256) nColours = 256;
    {
    int c;
    for (c = 0; c < 16; c++) {
    int tb;
    int best;
    long bestDist;
    _kernel_swi_regs in, out;
    int tr;
    int tg;
    tr = (amigaPalette[c] >> 8) & 0x0f;
    tg = (amigaPalette[c] >> 4) & 0x0f;
    tb = amigaPalette[c] & 0x0f;
    tr = (tr << 4) | tr;
    tg = (tg << 4) | tg;
    tb = (tb << 4) | tb;
    best = 0;
    bestDist = 0x7fffffffL;
    {
    int col;
    for (col = 0; col < nColours; col++) {
    int sb;
    long d;
    uint32_t word;
    int sr;
    int sg;
    in.r[0] = col;
    in.r[1] = 16;
    _kernel_swi(OS_ReadPalette, &in, &out);
    word = (uint32_t)out.r[2];
    sr = (word >> 8) & 0xff;
    sg = (word >> 16) & 0xff;
    sb = (word >> 24) & 0xff;
    d = (long)(tr - sr) * (tr - sr) +
                     (long)(tg - sg) * (tg - sg) +
                     (long)(tb - sb) * (tb - sb);
    if (d < bestDist) {
    bestDist = d;
    best = col;
}
} 



}
    pixtrans_[c] = (uint8_t)best;
} 

}
    {
    int c;
    for (c = 16; c < 256; c++) {
    pixtrans_[c] = (uint8_t)c;
} 

}
}

void platformPlotGameImage()
{
    static const int scaleFactors[4] = { 2, 2, 1, 1 };
    _kernel_swi_regs in, out;
    if (!sprite_) return;
    in.r[0] = 52 + 512;
    in.r[1] = 0;
    in.r[2] = (int)sprite_;
    in.r[3] = shakeOffsetX_;
    in.r[4] = -400;
    in.r[5] = 0;
    in.r[6] = (int)scaleFactors;
    in.r[7] = (int)pixtrans_;
    _kernel_swi(OS_SpriteOp, &in, &out);
}

void platformProcessRedraw(uint8_t* pollBlock)
{
    int more;
    _kernel_swi_regs in, out;
    in.r[1] = (int)pollBlock;
    _kernel_swi(Wimp_RedrawWindow, &in, &out);
    more = out.r[0];
    while (more) {
    if (!windowCalibrated_) {
    int y0;
    int x1;
    int y1;
    int visH;
    int delta;
    int x0 = ((int*)pollBlock)[7];
    y0 = ((int*)pollBlock)[8];
    x1 = ((int*)pollBlock)[9];
    y1 = ((int*)pollBlock)[10];
    (void)x0;
    (void)x1;
    visH = y1 - y0;
    delta = 400 - visH;
    if (delta < -2 || delta > 2) {
    int newH;
    _kernel_swi_regs oin, oout;
    int curH = ((int*)openBlock_)[4] - ((int*)openBlock_)[2];
    newH = curH + delta;
    if (newH < 440) newH = 440;
    if (newH > 1000) newH = 1000;
    ((int*)openBlock_)[2] = ((int*)openBlock_)[4] - newH;
    oin.r[1] = (int)openBlock_;
    _kernel_swi(Wimp_OpenWindow, &oin, &oout);
    windowCalibrated_ = false;
} else {
    windowCalibrated_ = true;
}
}
    platformPlotGameImage();
    _kernel_swi(Wimp_GetRectangle, &in, &out);
    more = out.r[0];
}
}

void platformProcessEvents(int event, uint8_t* pollBlock)
{
    switch (event) {
    case 1:   // Redraw_Window_Request
            platformProcessRedraw(pollBlock);
    break;
    case 2:   // Open_Window_Request
            // Accept: re-open the window as requested (keeps it consistent).
            {
    _kernel_swi_regs in, out;
    in.r[1] = (int)pollBlock;
    _kernel_swi(Wimp_OpenWindow, &in, &out);
}
            break;
    case 3:   // Close_Window_Request (close icon clicked)
            platformQuit = true;
    break;
    default:
            break;
}
}

void platformRenderFrame(bool waitForNextFrame)
{
    uint32_t now;
    uint32_t targetClock;
    uint32_t pollTimeout;
    if (!sprite_ || !chunkyBuffer || !windowOpened_) return;
    now = platformMonotonicTime();
    targetClock = clock_ ? (clock_ + 2) : (now + 1);
    if (targetClock < now + 1) targetClock = now + 1;
    clock_ = now;
    {
    int forceRect[4] = { 0, -400, 640, 0 };
    _kernel_swi_regs in, out;
    in.r[1] = windowHandle_;
    in.r[2] = (int)forceRect;
    _kernel_swi(Wimp_ForceRedraw, &in, &out);
}
    pollTimeout = waitForNextFrame ? targetClock : (now + 1);
    for (;;) {
    int event;
    _kernel_swi_regs in, out;
    in.r[0] = (1 << 4) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 11) | (1 << 12);
    in.r[1] = (int)pollBlock_;
    in.r[2] = (int)pollTimeout;
    _kernel_swi(Wimp_PollIdle, &in, &out);
    event = out.r[0];
    platformProcessEvents(event, pollBlock_);
    if (event == 0 || platformQuit) {
    break;
}
}

    // Execute interrupt callback if set
    if (interrupt_) {
    interrupt_();
}

    // Drive the MOD music player once per frame (50 Hz)
    platformAudioTick();
    frameCount_++;
}

void platformWaitForScreenMemoryAccess()
{
}

void platformSetHighlightedMenuRow(uint16_t row)
{
}

// ============================================================
// File loading
// ============================================================

uint32_t platformLoadFile(const char* filename, uint8_t* destination, uint32_t size)
{
    FILE * file;
    char gzPath[300];
    FILE * gz;
    char path[300];
    translatePath(path, sizeof(path), filename);
    file = fopen(path, "rb");
    if (file) {
    size_t bytesRead = fread(destination, 1, size, file);
    fclose(file);
    if (bytesRead > 0) return (uint32_t)bytesRead;
}
    snprintf(gzPath, sizeof(gzPath), "%s.gz", path);
    gz = fopen(gzPath, "rb");
    if (gz) {
    size_t bytesRead = fread(destination, 1, size, gz);
    fclose(gz);
    if (bytesRead > 0) return (uint32_t)bytesRead;
}
    printf("%s", unableToLoadData);
    return 0;
}

void platformLoadRawFile(const char* filename, uint8_t* destination, uint32_t size)
{
    platformLoadFile(filename, destination, size);
}

void platformLoadAssets()
{
    screenMemory = (uint8_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32);
    if (screenMemory) {
    platformLoadFile(IMAGE_PATHS[0], screenMemory, SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32);
}
    {
    int i;
    for (i = 0; i < 8; i++) {
    preloadedModuleData_[i] = 0;
    preloadedModuleLengths_[i] = 0;
} 

}
}

void platformInitAudio()
{
    audioInitialized_ = true;
}

void platformCleanupAudio()
{
    audioInitialized_ = false;
}

// ============================================================
// Platform interface methods
// ============================================================

uint8_t* platformStandardControls(void)
{
    return standardControls;
}

void platformSetInterrupt(void (*interrupt)(void))
{
    interrupt_ = interrupt;
}

void platformShow()
{
    if (windowOpened_) {
    _kernel_swi_regs in, out;
    in.r[1] = (int)openBlock_;
    _kernel_swi(Wimp_OpenWindow, &in, &out);
}
}

int platformFramesPerSecond()
{
    return framesPerSecond_;
}

// ============================================================
// Keyboard
// ============================================================

int platformInternalKeyPressed(int internalKey)
{
    return _kernel_osbyte(121, internalKey ^ 0x80, 0) == 0xff;
}

static bool prevKeysDown[64];

void platformScanKeyboard()
{
    int held;
    uint8_t virt;
    int pressedIdx;
    uint8_t code;
    bool newShift = platformInternalKeyPressed(3) || platformInternalKeyPressed(6);
    held = -1;
    virt = 0xff;
    pressedIdx = -1;
    {
    int i;
    for (i = 0; i < keyMapSize && i < 64; i++) {
    bool down = platformInternalKeyPressed(keyMap[i].internal) != 0;
    if (down && !prevKeysDown[i] && pressedIdx < 0) {
    pressedIdx = i;
}
    if (down && held < 0) {
    held = keyMap[i].internal;
    virt = keyMap[i].virt;
}
    prevKeysDown[i] = down;
} 

}
    code = (held >= 0) ? (virt | (newShift ? 0x80 : 0)) : 0xff;
    if (pressedIdx >= 0) {
    keyToReturn_ = keyMap[pressedIdx].virt | (newShift ? 0x80 : 0);
}
    downKey_ = code;
    shift_ = newShift ? 0x80 : 0;
}

uint8_t platformReadKeyboard()
{
    uint8_t result;
    platformScanKeyboard();
    result = keyToReturn_;
    keyToReturn_ = 0xff;
    return result;
}

void platformKeyRepeat()
{
    keyToReturn_ = downKey_;
}

void platformClearKeyBuffer()
{
    keyToReturn_ = 0xff;
    downKey_ = 0xff;
    lastHeldInternal_ = -1;
    shift_ = 0;
}

bool platformIsKeyOrJoystickPressed(bool gamepad)
{
    return downKey_ != 0xff;
}

uint16_t platformReadJoystick(bool gamepad)
{
    return 0;
}

void platformRumble(uint8_t strength)
{
}

// ============================================================
// Images, maps and tiles
// ============================================================

void platformDisplayImage(Image image)
{
    uint32_t imageSize;
    uint16_t * paletteData;
    if (!screenMemory) return;
    imageSize = SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8;
    platformLoadFile(IMAGE_PATHS[image], screenMemory, imageSize + 32);
    paletteData = (uint16_t*)(screenMemory + imageSize);
    {
    int i;
    for (i = 0; i < 16; i++) {
    amigaPalette[i] = paletteData[i];
} 

}
    planarToChunky(screenMemory, chunkyBuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    platformBuildPixtrans();
}

void platformLoadMap(Map map, uint8_t* destination)
{
    MAPNAME[17] = 'a' + map;
    platformLoadFile(MAPNAME, destination, 8960);
}

uint8_t* platformLoadTileset()
{
    platformLoadRawFile(TILESET_PATH, tilesetData, 514);
    return tilesetData;
}

// ============================================================
// Tile generation (ported verbatim from the MorphOS version)
// ============================================================

void platformGenerateTiles(uint8_t* tileData, uint8_t* tileAttributes)
{
    int transparentTiles[] = {130, 134, 240, 241, 244, 245, 246, 248, 249, 250, 251, 252, -1};
    static uint8_t defaultTileLiveMap[] = {
         0,13, 1, 1, 1, 1, 1, 1, 1, 5, 1, 1, 1, 1,13, 1,
         1, 1, 1, 1, 1, 1, 1, 2, 8, 1, 1, 1, 1, 6,14,14,
        15,13,14,15,15,13, 5,15, 6,13,13,12, 6,13,13,12,
         1, 1, 1,12, 1, 9, 9, 6, 1, 9,15, 6,10,10, 1, 1,
         1, 1, 7,13, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
         2, 2,13,13,13,13,13,13, 1, 1, 1,13, 4, 4, 4, 0,
         1, 1, 4,13, 4, 4, 4, 1, 1, 1, 2, 2, 2, 2, 2, 2,
         1, 1,13,10, 1, 1,13, 9, 1, 1, 4, 5,13,13,13, 4,
         1, 1, 1, 1,15, 5,15,15, 1, 1, 6, 6,15,15, 6, 5,
         2, 2, 2, 5,13,13, 1, 7, 5, 3, 1, 7, 4, 4, 4,13,
         1, 1, 1, 1, 1, 4, 4, 2, 3, 3, 3, 1, 3, 2, 3, 3,
         3, 3, 3, 1, 4, 4, 9, 9, 4, 4, 2, 2, 5,11,12,12,
         8, 8, 8, 9, 3, 3, 2,10, 1, 1, 1, 9, 1, 1, 1,10,
         1, 1, 1,13, 8, 8,13,13, 8, 8,13,13, 3, 3,13,13,
        13,13, 5,13,13,13,13,13,13,13,13,13,13,13,13,13
    };
    uint8_t * tilesPlanar;
    int tileWidth;
    int tileHeight;
    int srcBytesPerRow;
    uint32_t animFileSize;
    uint8_t * animPlanar;
    uint32_t spriteFileSize;
    uint8_t * spritePlanar;
    uint8_t * spriteMaskPlanar;
    int itemW, itemH, itemN;
    int itemBytesPerRow;
    uint32_t itemFileSize;
    uint8_t * itemPlanar;
    int keyW, keyH, keyN;
    int keyBytesPerRow;
    uint32_t keyFileSize;
    uint8_t * keyPlanar;
    int healthW, healthH, healthN;
    int healthBytesPerRow;
    uint32_t healthFileSize;
    uint8_t * healthPlanar;
    int faceW, faceH, faceN;
    int faceBytesPerRow;
    uint32_t faceFileSize;
    uint8_t * facePlanar;
    uint32_t tilesFileSize = 97152;
    tilesPlanar = (uint8_t*)malloc(tilesFileSize);
    if (!tilesPlanar) return;
    platformLoadRawFile(TILES_PATH, tilesPlanar, tilesFileSize);
    tileWidth = 32;
    tileHeight = 24;
    srcBytesPerRow = (tileWidth / 8) * PLANES;
    {
    int t;
    for (t = 0; t < 253; t++) {
    uint8_t * src;
    uint8_t * chunky;
    tileChunky[t] = (uint8_t*)malloc(TILE_PIXEL_SIZE);
    if (!tileChunky[t]) continue;
    chunky = tileChunky[t];
    src = tilesPlanar + t * tileHeight * srcBytesPerRow;
    {
    int y;
    for (y = 0; y < tileHeight; y++) {
    {
    int x;
    for (x = 0; x < tileWidth; x++) {
    uint8_t color = 0;
    {
    int p;
    for (p = 0; p < PLANES; p++) {
    int bit;
    int byteOffset;
    bit = 7 - (x & 7);
    byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
    if (src[byteOffset] & (1 << bit)) {
    color |= (1 << p);
}
} 















}
    chunky[y * tileWidth + x] = color;
} 







}
} 



}
} 

}
    free(tilesPlanar);
    animFileSize = 9216;
    animPlanar = (uint8_t*)malloc(animFileSize);
    if (animPlanar) {
    platformLoadRawFile(ANIM_TILES_PATH, animPlanar, animFileSize);
    animTileChunky = (uint8_t*)malloc(17 * TILE_PIXEL_SIZE);
    if (animTileChunky) {
    {
    int t;
    for (t = 0; t < 17; t++) {
    uint8_t * chunky;
    uint8_t * src;
    chunky = animTileChunky + t * TILE_PIXEL_SIZE;
    src = animPlanar + t * tileHeight * srcBytesPerRow;
    {
    int y;
    for (y = 0; y < tileHeight; y++) {
    {
    int x;
    for (x = 0; x < tileWidth; x++) {
    uint8_t color = 0;
    {
    int p;
    for (p = 0; p < PLANES; p++) {
    int bit;
    int byteOffset;
    bit = 7 - (x & 7);
    byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
    if (src[byteOffset] & (1 << bit)) {
    color |= (1 << p);
}
} 















}
    chunky[y * tileWidth + x] = color;
} 







}
} 



}
} 

}
}
    free(animPlanar);
}
    spriteFileSize = 31872;
    spritePlanar = (uint8_t*)malloc(spriteFileSize);
    spriteMaskPlanar = (uint8_t*)malloc(spriteFileSize);
    if (spritePlanar && spriteMaskPlanar) {
    int spritesPerRow;
    platformLoadRawFile(SPRITES_PATH, spritePlanar, spriteFileSize);
    platformLoadRawFile(SPRITES_MASK_PATH, spriteMaskPlanar, spriteFileSize);
    spritesPerRow = spriteFileSize / (tileHeight * srcBytesPerRow);
    spriteChunky = (uint8_t*)malloc(spritesPerRow * TILE_PIXEL_SIZE);
    tilesMask = (uint8_t*)malloc(spritesPerRow * TILE_PIXEL_SIZE);
    if (spriteChunky && tilesMask) {
    {
    int t;
    for (t = 0; t < spritesPerRow; t++) {
    uint8_t * src;
    uint8_t * srcMask;
    uint8_t * chunky;
    uint8_t * mask;
    chunky = spriteChunky + t * TILE_PIXEL_SIZE;
    mask = tilesMask + t * TILE_PIXEL_SIZE;
    src = spritePlanar + t * tileHeight * srcBytesPerRow;
    srcMask = spriteMaskPlanar + t * tileHeight * srcBytesPerRow;
    {
    int y;
    for (y = 0; y < tileHeight; y++) {
    {
    int x;
    for (x = 0; x < tileWidth; x++) {
    uint8_t color;
    uint8_t maskVal;
    color = 0;
    maskVal = 0;
    {
    int p;
    for (p = 0; p < PLANES; p++) {
    int bit;
    int byteOffset;
    bit = 7 - (x & 7);
    byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
    if (src[byteOffset] & (1 << bit)) {
    color |= (1 << p);
}
    if (srcMask[byteOffset] & (1 << bit)) {
    maskVal |= (1 << p);
}
} 















}
    chunky[y * tileWidth + x] = color;
    mask[y * tileWidth + x] = maskVal;
} 







}
} 



}
} 

}
}
}
    free(spritePlanar);
    free(spriteMaskPlanar);
    itemW = 48;
    itemH = 21;
    itemN = 6;
    itemBytesPerRow = (itemW / 8) * PLANES;
    itemFileSize = itemN * itemH * itemBytesPerRow;
    itemPlanar = (uint8_t*)malloc(itemFileSize);
    if (itemPlanar) {
    platformLoadRawFile(ITEMS_PATH, itemPlanar, itemFileSize);
    itemChunky = (uint8_t*)malloc(itemN * itemW * itemH);
    if (itemChunky) {
    {
    int t;
    for (t = 0; t < itemN; t++) {
    uint8_t * chunky;
    uint8_t * src;
    chunky = itemChunky + t * itemW * itemH;
    src = itemPlanar + t * itemH * itemBytesPerRow;
    planarToChunky(src, chunky, itemW, itemH);
} 

}
}
    free(itemPlanar);
}
    itemWidth = itemW;
    itemHeight = itemH;
    itemCount = itemN;
    keyW = 16;
    keyH = 14;
    keyN = 3;
    keyBytesPerRow = (keyW / 8) * PLANES;
    keyFileSize = keyN * keyH * keyBytesPerRow;
    keyPlanar = (uint8_t*)malloc(keyFileSize);
    if (keyPlanar) {
    platformLoadRawFile(KEYS_PATH, keyPlanar, keyFileSize);
    keyChunky = (uint8_t*)malloc(keyN * keyW * keyH);
    if (keyChunky) {
    {
    int t;
    for (t = 0; t < keyN; t++) {
    uint8_t * chunky;
    uint8_t * src;
    chunky = keyChunky + t * keyW * keyH;
    src = keyPlanar + t * keyH * keyBytesPerRow;
    planarToChunky(src, chunky, keyW, keyH);
} 

}
}
    free(keyPlanar);
}
    keyWidth = keyW;
    keyHeight = keyH;
    keyCount = keyN;
    healthW = 48;
    healthH = 51;
    healthN = 3;
    healthBytesPerRow = (healthW / 8) * PLANES;
    healthFileSize = healthN * healthH * healthBytesPerRow;
    healthPlanar = (uint8_t*)malloc(healthFileSize);
    if (healthPlanar) {
    platformLoadRawFile(HEALTH_PATH, healthPlanar, healthFileSize);
    healthChunky = (uint8_t*)malloc(healthN * healthW * healthH);
    if (healthChunky) {
    {
    int t;
    for (t = 0; t < healthN; t++) {
    uint8_t * chunky;
    uint8_t * src;
    chunky = healthChunky + t * healthW * healthH;
    src = healthPlanar + t * healthH * healthBytesPerRow;
    planarToChunky(src, chunky, healthW, healthH);
} 

}
}
    free(healthPlanar);
}
    healthWidth = healthW;
    healthHeight = healthH;
    healthCount = healthN;
    faceW = 16;
    faceH = 24;
    faceN = 3;
    faceBytesPerRow = (faceW / 8) * PLANES;
    faceFileSize = faceN * faceH * faceBytesPerRow;
    facePlanar = (uint8_t*)malloc(faceFileSize);
    if (facePlanar) {
    platformLoadRawFile(FACES_PATH, facePlanar, faceFileSize);
    faceChunky = (uint8_t*)malloc(faceN * faceW * faceH);
    if (faceChunky) {
    {
    int t;
    for (t = 0; t < faceN; t++) {
    uint8_t * chunky;
    uint8_t * src;
    chunky = faceChunky + t * faceW * faceH;
    src = facePlanar + t * faceH * faceBytesPerRow;
    planarToChunky(src, chunky, faceW, faceH);
} 

}
}
    free(facePlanar);
}
    faceWidth = faceW;
    faceHeight = faceH;
    faceCount = faceN;
    {
    int i;
    for (i = 0; i < 256; i++) {
    if (spriteTileMap[i] >= 0) {
    tileHasTransparency[i] = true;
}
} 

}
    {
    int i;
    for (i = 0; transparentTiles[i] >= 0; i++) {
    tileHasTransparency[transparentTiles[i]] = true;
} 

}
    memcpy(tileLiveMap, defaultTileLiveMap, 256);
}

// ============================================================
// Chunky buffer rendering primitives (ported from MorphOS)
// ============================================================

void platformChunkyBlit(const uint8_t* source, uint16_t dx, uint16_t dy,
                                uint16_t w, uint16_t h, bool transparent, uint8_t transparentColor)
{
    {
    int y;
    for (y = 0; y < h; y++) {
    {
    int x;
    for (x = 0; x < w; x++) {
    uint8_t pixel = source[y * w + x];
    if (!transparent || pixel != transparentColor) {
    int dstX;
    int dstY;
    dstX = dx + x;
    dstY = dy + y;
    if (dstX >= 0 && dstX < SCREEN_WIDTH && dstY >= 0 && dstY < SCREEN_HEIGHT) {
    chunkyBuffer[dstY * SCREEN_WIDTH + dstX] = pixel;
}
}
} 



}
} 

}
}

void platformRenderTile(uint8_t tile, uint16_t x, uint16_t y, uint8_t variant, bool transparent)
{
    int tileW, tileH;
    uint16_t destX;
    uint16_t destY;
    bool useTransparency;
    uint8_t* src;
    tileW = 32;
    tileH = 24;
    if (transparent && spriteTileMap[tile] >= 0) {
    int spriteIdx = spriteTileMap[tile] + variant;
    if (spriteChunky) {
    src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
} else {
    return;
}
} else if (animTileMap[tile] >= 0) {
    int animIdx = animTileMap[tile] + variant;
    if (animTileChunky) {
    src = animTileChunky + animIdx * TILE_PIXEL_SIZE;
} else {
    src = tileChunky[tile];
    if (!src) return;
}
} else {
    src = tileChunky[tile];
    if (!src) return;
}
    destX = x * 8;
    destY = y * 8;
    useTransparency = transparent || tileHasTransparency[tile];
    {
    int row;
    for (row = 0; row < tileH; row++) {
    {
    int col;
    for (col = 0; col < tileW; col++) {
    uint8_t pixel = src[row * tileW + col];
    if (!useTransparency || pixel != 0) {
    int px;
    int py;
    px = destX + col;
    py = destY + row;
    if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
    chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
}
}
} 



}
} 

}
}

void platformRenderTiles(uint8_t backgroundTile, uint8_t foregroundTile,
                                 uint16_t x, uint16_t y,
                                 uint8_t backgroundVariant, uint8_t foregroundVariant)
{
    platformRenderTile(backgroundTile, x, y, backgroundVariant, false);
    if (spriteTileMap[foregroundTile] >= 0) {
    int spriteIdx = spriteTileMap[foregroundTile] + foregroundVariant;
    if (spriteChunky) {
    uint16_t destY;
    uint8_t * src;
    uint16_t destX = x * 8;
    destY = y * 8;
    src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
    {
    int row;
    for (row = 0; row < 24; row++) {
    {
    int col;
    for (col = 0; col < 32; col++) {
    uint8_t pixel = src[row * 32 + col];
    if (pixel != 0) {
    int px;
    int py;
    px = destX + col;
    py = destY + row;
    if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
    chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
}
}
} 



}
} 

}
}
} else {
    platformRenderTile(foregroundTile, x, y, foregroundVariant, true);
}
}

void platformRenderItem(uint8_t item, uint16_t x, uint16_t y)
{
    uint8_t * src;
    if (!itemChunky || item >= itemCount) return;
    src = itemChunky + item * itemWidth * itemHeight;
    platformChunkyBlit(src, x, y, itemWidth, itemHeight, true, 0);
}

void platformRenderKey(uint8_t key, uint16_t x, uint16_t y)
{
    uint8_t * src;
    if (!keyChunky || key >= keyCount) return;
    src = keyChunky + key * keyWidth * keyHeight;
    platformChunkyBlit(src, x, y, keyWidth, keyHeight, true, 0);
}

void platformRenderHealth(uint8_t health, uint16_t x, uint16_t y)
{
    uint8_t * src;
    if (!healthChunky || health >= healthCount) return;
    src = healthChunky + health * healthWidth * healthHeight;
    platformChunkyBlit(src, x, y, healthWidth, healthHeight, true, 0);
}

void platformRenderFace(uint8_t face, uint16_t x, uint16_t y)
{
    uint8_t * src;
    if (!faceChunky || face >= faceCount) return;
    src = faceChunky + face * faceWidth * faceHeight;
    platformChunkyBlit(src, x, y, faceWidth, faceHeight, true, 0);
}

// ============================================================
// Live map
// ============================================================

void platformRenderLiveMap(uint8_t* map)
{
    platformClearRect(0, 0, SCREEN_WIDTH - 56, SCREEN_HEIGHT - 32);
    {
    int my;
    for (my = 0; my < 64; my++) {
    {
    int mx;
    for (mx = 0; mx < 128; mx++) {
    uint8_t tile;
    uint8_t color;
    int px;
    int py;
    tile = tileLiveMap[map[my * 128 + mx]];
    color = tile & 0x0f;
    px = mx * 2;
    py = 20 + my * 2;
    if (py >= 0 && py + 1 < SCREEN_HEIGHT && px >= 0 && px + 1 < SCREEN_WIDTH) {
    chunkyBuffer[py * SCREEN_WIDTH + px] = color;
    chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
}
} 



}
} 

}
}

void platformRenderLiveMapTile(uint8_t* map, uint8_t mx, uint8_t my)
{
    uint8_t color;
    int px;
    int py;
    uint8_t tile = tileLiveMap[map[(my << 7) + mx]];
    color = tile & 0x0f;
    px = mx * 2;
    py = 20 + my * 2;
    if (py >= 0 && py + 1 < SCREEN_HEIGHT && px >= 0 && px + 1 < SCREEN_WIDTH) {
    chunkyBuffer[py * SCREEN_WIDTH + px] = color;
    chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
}
}

void platformRenderLiveMapUnits(uint8_t* map, uint8_t* unitTypes,
                                        uint8_t* unitX, uint8_t* unitY,
                                        uint8_t playerColor, bool showRobots)
{
    {
    int i;
    for (i = 0; i < 48; i++) {
    if (i == 0 || (unitTypes[i] != 255 && unitTypes[i] != 0)) {
    if (i == 0 || showRobots || unitTypes[i] == 22) {
    int px;
    int py;
    uint8_t color;
    int mx;
    int my;
    mx = unitX[i];
    my = unitY[i];
    px = mx * 2;
    py = 20 + my * 2;
    color = (i == 0) ? playerColor : 15;
    if (py >= 0 && py + 1 < SCREEN_HEIGHT && px >= 0 && px + 1 < SCREEN_WIDTH) {
    chunkyBuffer[py * SCREEN_WIDTH + px] = color;
    chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
}
}
}
} 

}
}

// ============================================================
// Cursor
// ============================================================

void platformShowCursor(uint16_t x, uint16_t y)
{
    int px;
    int py;
    uint8_t color;
    cursorX_ = x;
    cursorY_ = y;
    cursorVisible_ = true;
    px = x * 8 + 8;
    py = y * 8 + 8;
    color = 15;
    {
    int i;
    for (i = -1; i <= 1; i++) {
    if (px + i >= 0 && px + i < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT)
            chunkyBuffer[py * SCREEN_WIDTH + px + i] = color;
    if (px >= 0 && px < SCREEN_WIDTH && py + i >= 0 && py + i < SCREEN_HEIGHT)
            chunkyBuffer[(py + i) * SCREEN_WIDTH + px] = color;
} 

}
}

void platformHideCursor()
{
    cursorVisible_ = false;
}

void platformSetCursorShape(CursorShape shape)
{
    cursorShape_ = shape;
}

// ============================================================
// Rect operations
// ============================================================

void platformCopyRect(uint16_t sourceX, uint16_t sourceY,
                              uint16_t destinationX, uint16_t destinationY,
                              uint16_t width, uint16_t height)
{
    {
    int y;
    for (y = 0; y < height; y++) {
    {
    int x;
    for (x = 0; x < width; x++) {
    int sx;
    int sy;
    int dx;
    int dy;
    sx = sourceX + x;
    sy = sourceY + y;
    dx = destinationX + x;
    dy = destinationY + y;
    if (sx < SCREEN_WIDTH && sy < SCREEN_HEIGHT &&
                dx < SCREEN_WIDTH && dy < SCREEN_HEIGHT) {
    chunkyBuffer[dy * SCREEN_WIDTH + dx] = chunkyBuffer[sy * SCREEN_WIDTH + sx];
}
} 



}
} 

}
}

void platformClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    {
    int row;
    for (row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
    {
    int col;
    for (col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
    chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = 0;
} 



}
} 

}
}

void platformFillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color)
{
    {
    int row;
    for (row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
    {
    int col;
    for (col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
    chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = color;
} 



}
} 

}
}

// ============================================================
// Screen shake
// ============================================================

void platformStartShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

void platformShakeScreen()
{
    shakeStep_++;
    if (shakeStep_ > 4) {
    shakeStep_ = 1;
}
    if (shakeStep_ < 3) {
    shakeOffsetX_ = 4;
} else {
    shakeOffsetX_ = -4;
}
}

void platformStopShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

// ============================================================
// Fade effects
// ============================================================

void platformStartFadeScreen(uint16_t color, uint16_t intensity)
{
    if (!palette) return;
    paletteSetFadeBaseColor(palette, color);
    paletteSetFade(palette, intensity);
}

void platformFadeScreen(uint16_t intensity, bool immediate)
{
    uint16_t fade;
    if (!palette) return;
    fade = paletteFade(palette);
    if (fade != intensity) {
    if (immediate) {
    paletteSetFade(palette, intensity);
} else {
    int16_t fadeDelta = intensity > fade ? 1 : -1;
    do {
    uint32_t t0;
    fade += fadeDelta;
    paletteSetFade(palette, fade);
    t0 = platformMonotonicTime();
    while ((platformMonotonicTime() - t0) < 1) {
}
} while (fade != intensity);
}
}
}

void platformStopFadeScreen()
{
    if (!palette) return;
    paletteSetFade(palette, 15);
}

// ============================================================
// Screen memory access (character-based text output)
// ============================================================

void platformWriteToScreenMemory(address_t address, uint8_t value,
                                         uint8_t color, uint8_t yOffset)
{
    bool reverse;
    uint8_t charIdx;
    uint8_t * glyph;
    uint16_t startX;
    uint16_t startY;
    if (!fontData) return;
    reverse = value > 127;
    charIdx = value & 127;
    glyph = fontData + charIdx * 8;
    startX = addressMap[address] % SCREEN_WIDTH;
    startY = addressMap[address] / SCREEN_WIDTH + yOffset;
    {
    int row;
    for (row = 0; row < 8 && (startY + row) < SCREEN_HEIGHT; row++) {
    uint8_t fontByte = reverse ? ~glyph[row] : glyph[row];
    {
    int col;
    for (col = 0; col < 8 && (startX + col) < SCREEN_WIDTH; col++) {
    if (fontByte & (1 << (7 - col))) {
    chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = color;
} else {
    chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = 0;
}
} 



}
} 

}
}

// ============================================================
// Audio
// ============================================================
// Sound effects are played through OS_Sound (SWI &1C).
// R0 = sound code: bits 0-3 waveform, bits 4-7 amplitude, bits 8-11 channel.
// R1 = pitch, R2 = duration (centiseconds).

void platformPlayNote(uint8_t note)
{
}

void platformStopNote()
{
}

// ============================================================
// MOD music via MODPlay (thirdparty/modplay.c)
// ============================================================
// The game's modules ship "packed" after the standard 1084-byte MOD
// header: the magic at offset 1080 is !PM! instead of M.K. and the
// sample data is delta coded. Both are undone in place here, matching
// the Amiga port's platformUndeltaSamples(). Music is then ticked once per
// 50 Hz frame from renderFrame() and the resulting per-channel Paula
// notes are steered onto the RISC OS Sound_Control voices 1-4.

// Index order matches MorphOS: (module - 1), SoundFX maps to index 7.
//  0 InGame1  1 Win  2 Lose  3 InGame2  4 InGame3
//  5 InGame4  6 Intro  7 SoundFX
// (MODULE_PATHS defined above with the other asset paths.)

#define MUSIC_BUFFER_SIZE 200000
#define SAMPLE_RATE 22050
#define Sound_Control 0x40189

void platformUndeltaSamples(uint8_t* module, uint32_t moduleSize)
{
    int8_t * samplesStart;
    int8_t * samplesEnd;
    int8_t sample;
    uint8_t numPatterns = 0;
    {
    int i;
    for (i = 0; i < module[950]; i++) {
    uint8_t p = module[952 + i];
    if (p > numPatterns) numPatterns = p;
} 

}
    numPatterns++;
    samplesStart = (int8_t*)(module + 1084 + (numPatterns << 10));
    samplesEnd = (int8_t*)(module + moduleSize);
    sample = 0;
    { int8_t* data; for (data = samplesStart; data < samplesEnd; data++) {
    sample += *data;
    *data = sample;
} }

}

// Convert a MODPlay Paula sample period (16.16, relative to paularate
// at SAMPLE_RATE) into a RISC OS Sound_Control octave/fraction pitch.
// paularate = (3546895 / SAMPLE_RATE) << 16, so the original ProTracker
// note period is paularate / (period / 65536). Integer log2 only.
static int log2scale(long value)   // round(log2(value) * 4096), integer
{
    int frac;
    long m;
    int whole = 0;
    while (value >= 2) {
    value >>= 1;
    whole += 4096;
}
    frac = 0;
    m = value << 12;
    {
    int bit;
    for (bit = 0; bit < 12; bit++) {
    m = (m * m) >> 12;
    if (m >= 8192) {
    m >>= 1;
    frac |= 1 << (11 - bit);
}
} 

}
    return whole + frac;
}

static int periodToPitch(int32_t period)
{
    long long rawLL;
    long raw;
    int exp12;
    int pitch;
    if (period <= 0) return 0;
    rawLL = (long long)10541824 * 65536 / period;
    if (rawLL <= 32) rawLL = 32;
    if (rawLL > 1712) rawLL = 1712;
    raw = (long)rawLL;
    exp12 = log2scale(428) - log2scale(raw);
    pitch = (6 << 12) + exp12;
    if (pitch < 0) pitch = 0;
    if (pitch > 0x7fff) pitch = 0x7fff;
    return pitch;
}

static void setVoice(int voice, int amplitude, int pitch, bool gateOn)
{
    _kernel_swi_regs in, out;
    in.r[0] = voice;
    in.r[1] = (gateOn ? 0x100 : 0) | 0x80 | (amplitude & 0x7f);
    in.r[2] = pitch & 0x7fff;
    in.r[3] = 20;
    _kernel_swi(Sound_Control, &in, &out);
}

void platformAudioTick()
{
    ModPlayerStatus_t * status;
    if (!modStatus_ || !modActive_ || modPaused_) return;
    status = (ModPlayerStatus_t*)modStatus_;
    ProcessMOD();
    {
    int c;
    for (c = 0; c < 4; c++) {
    int amplitude;
    int voice;
    const PaulaChannel_t* paula = &status->ch[c].samplegen;
    voice = c + 1;
    if (!paula->sample || paula->period == 0) {
    setVoice(voice, 0, 0, false);
    continue;
}
    amplitude = paula->volume * 2;
    if (amplitude > 127) amplitude = 127;
    setVoice(voice, amplitude, periodToPitch(paula->period), true);
} 

}
}

void platformLoadModule(Module module)
{
    int idx = (module == ModuleSoundFX) ? 7 : ((int)module - 1);
    if (idx < 0 || idx >= 8) return;
    if (module == loadedModule && musicBuffer_ && musicLen_ > 0) return;
    if (!musicBuffer_) {
    musicBuffer_ = (uint8_t*)malloc(MUSIC_BUFFER_SIZE);
}
    if (!musicBuffer_) return;
    musicLen_ = platformLoadFile(MODULE_PATHS[idx], musicBuffer_, MUSIC_BUFFER_SIZE);
    if (musicLen_ == 0) {
    loadedModule = module;
    return;
}
    if (musicLen_ >= 1084 && memcmp(musicBuffer_ + 1080, "!PM!", 4) == 0) {
    memcpy(musicBuffer_ + 1080, "M.K.", 4);
    platformUndeltaSamples(musicBuffer_, musicLen_);
}
    loadedModule = module;
}

void platformPlayModule(Module module)
{
    platformLoadModule(module);
    platformStopModule();
    if (module != loadedModule || !musicBuffer_ || musicLen_ == 0) return;
    modStatus_ = InitMOD(musicBuffer_, SAMPLE_RATE);
    if (!modStatus_) return;
    modActive_ = true;
    modPaused_ = false;
}

void platformPauseModule()
{
    modPaused_ = true;
    {
    int c;
    for (c = 0; c < 4; c++) {
    setVoice(c + 1, 0, 0, false);
} 

}
}

void platformStopModule()
{
    modActive_ = false;
    modPaused_ = false;
    {
    int c;
    for (c = 0; c < 4; c++) {
    setVoice(c + 1, 0, 0, false);
} 

}
}

void platformPlaySample(uint8_t sample)
{
    _kernel_swi_regs in, out;
    int wave;
    if (sample >= 16) return;
    wave = (sample % 8) + 8;
    in.r[0] = (1 << 8) | (12 << 4) | (wave & 0x0f);
    in.r[1] = sample;
    in.r[2] = 2;
    _kernel_swi(OS_Sound, &in, &out);
}

void platformStopSample()
{
}

void platformLoadSamples()
{
}