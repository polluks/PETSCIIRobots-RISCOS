/**
 * PlatformRISCOS - RISC OS Wimp (desktop) implementation
 *
 * Displays the game in a 640x400 Wimp window, scaled 2x from a 320x200
 * 8-bit chunky buffer using OS_SpriteOp PutSpriteScaled (SWI reason 52).
 * Keys are polled with OS_Byte 121 (single-key test) using internal key
 * numbers. Sound effects are played through OS_Sound; MOD music is not
 * played on RISC OS.
 *
 * Files are read with fopen() using '.' directory separators (RISC OS
 * convention). The application must be run with its current directory
 * set so that the "Amiga", "Sounds" and "Music" directories are found.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"

#include "Palette.h"
#include "PlatformRISCOS.h"

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
    int internal;   // internal key number
    uint8_t virt;   // virtual key code
};

static const struct KeyMapEntry keyMap[] = {
    { 16, 'q' }, { 33, 'w' }, { 34, 'e' }, { 51, 'r' }, { 35, 't' },
    { 68, 'y' }, { 53, 'u' }, { 37, 'i' }, { 54, 'o' }, { 55, 'p' },
    { 65, 'a' }, { 81, 's' }, { 50, 'd' }, { 67, 'f' }, { 83, 'g' },
    { 84, 'h' }, { 69, 'j' }, { 70, 'k' }, { 86, 'l' }, { 97, 'z' },
    { 66, 'x' }, { 82, 'c' }, { 99, 'v' }, { 100, 'b' }, { 85, 'n' },
    { 101, 'm' },
    { 48, '1' }, { 49, '2' }, { 17, '3' }, { 18, '4' }, { 19, '5' },
    { 52, '6' }, { 36, '7' }, { 21, '8' }, { 38, '9' }, { 39, '0' },
    { 57, 0x01 }, { 41, 0x02 }, { 25, 0x03 }, { 121, 0x04 },   // cursors
    { 73, 0x0d }, { 98, 0x20 }, { 96, 0x09 }, { 112, 0x1b },
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
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t color = 0;
            for (int plane = 0; plane < PLANES; plane++) {
                int byteOffset = (y * bytesPerRow * PLANES) + (plane * bytesPerRow) + (x / 8);
                int bit = 7 - (x & 7);
                if (src[byteOffset] & (1 << bit)) {
                    color |= (1 << plane);
                }
            }
            dst[y * width + x] = color;
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

PlatformRISCOS::PlatformRISCOS() :
    framesPerSecond_(50),
    taskHandle_(-1),
    windowHandle_(-1),
    windowCalibrated_(false),
    windowOpened_(false),
    createBlock_(0),
    openBlock_(0),
    pollBlock_(0),
    sprite_(0),
    pixtrans_(0),
    screenLog2BPP_(0),
    screenLog2BPC_(0),
    screenOSWidth_(0),
    screenOSHeight_(0),
    trueColour_(false),
    screenMemory(0),
    chunkyBuffer(0),
    fontData(0),
    itemChunky(0),
    itemWidth(0),
    itemHeight(0),
    itemCount(0),
    keyChunky(0),
    keyWidth(0),
    keyHeight(0),
    keyCount(0),
    healthChunky(0),
    healthWidth(0),
    healthHeight(0),
    healthCount(0),
    faceChunky(0),
    faceWidth(0),
    faceHeight(0),
    faceCount(0),
    spriteChunky(0),
    spriteWidth(0),
    spriteHeight(0),
    spriteCount(0),
    animTileChunky(0),
    tilesMask(0),
    cursorX_(0),
    cursorY_(0),
    cursorVisible_(false),
    cursorShape_(ShapeUse),
    loadedModule(ModuleSoundFX),
    audioInitialized_(false),
    shakeStep_(0),
    shakeOffsetX_(0),
    fadeIntensity_(15),
    keyToReturn_(0xff),
    downKey_(0xff),
    shift_(0),
    lastHeldInternal_(-1),
    interrupt_(0),
    clock_(0),
    frameCount_(0),
    palette(0),
    loadBuffer_(0),
    loadBufferSize_(0),
    preloaded_(false)
{
    memset(tileChunky, 0, sizeof(tileChunky));
    memset(tileHasTransparency, 0, sizeof(tileHasTransparency));
    memcpy(animTileMap, animTileMapDefault, sizeof(animTileMap));
    memset(amigaPalette, 0, sizeof(amigaPalette));
    memset(preloadedModuleData_, 0, sizeof(preloadedModuleData_));
    memset(preloadedModuleLengths_, 0, sizeof(preloadedModuleLengths_));

    // Initialize palette system
    Palette::initialize();
    palette = new Palette(blackPalette, 16, 0);

    // Build address map
    for (int y = 0, i = 0; y < SCREEN_HEIGHT_IN_CHARS; y++) {
        for (int x = 0; x < SCREEN_WIDTH_IN_CHARS; x++, i++) {
            addressMap[i] = y * SCREEN_WIDTH * 8 + x * 8;
        }
    }

    // Set up Wimp task and window
    initWimp();
    if (taskHandle_ < 0) {
        return;
    }

    // Read mode information (screen size for window placement)
    {
        _kernel_swi_regs in, out;
        int vars[5] = { 9, 4, 5, 11, 12 };   // Log2BPP, XEig, YEig, XWindLimit, YWindLimit
        int vals[5] = { 0, 0, 0, 0, 0 };
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

    // Build the sprite (game image 320x200 8bpp).
    // The chunky buffer is overlaid on the sprite's pixel data so the game
    // writes directly into the sprite image with no per-frame copy.
    sprite_ = (uint8_t*)malloc(44 + SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!sprite_) {
        printf("%s", notEnoughMemoryError);
        return;
    }
    memset(sprite_, 0, 44 + SCREEN_WIDTH * SCREEN_HEIGHT);
    memset(sprite_, 0, 44);
    strncpy((char*)sprite_, "PETSCIIRobots", 12);
    ((int*)sprite_)[3] = SCREEN_WIDTH / 4 - 1;    // word width - 1
    ((int*)sprite_)[4] = SCREEN_HEIGHT - 1;       // height - 1
    ((int*)sprite_)[5] = 0;                       // first bit used
    ((int*)sprite_)[6] = 7;                       // last bit used
    ((int*)sprite_)[7] = 21;                      // mode word: 8bpp, 2 x 2
    ((int*)sprite_)[8] = 44;                      // mask offset = 44 (no mask)
    chunkyBuffer = sprite_ + 44;

    // Pixel translation table (256 byte entries)
    pixtrans_ = (uint8_t*)malloc(256);
    if (!pixtrans_) {
        printf("%s", notEnoughMemoryError);
        return;
    }
    memset(pixtrans_, 0, 256);

    // Allocate chunky tile pointers
    for (int i = 0; i < NUM_TILES; i++) {
        tileChunky[i] = 0;
    }

    // Allocate poll blocks
    createBlock_ = (uint8_t*)malloc(100);
    openBlock_ = (uint8_t*)malloc(100);
    pollBlock_ = (uint8_t*)malloc(512);
    if (!createBlock_ || !openBlock_ || !pollBlock_) {
        printf("%s", notEnoughMemoryError);
        return;
    }
    memset(createBlock_, 0, 100);
    memset(openBlock_, 0, 100);
    memset(pollBlock_, 0, 512);

    // Create the window
    memset(createBlock_, 0, 100);
    // +28 window flags
    ((int*)createBlock_)[7] = (1 << 1) | 0x86000000;   // movable, system icons (title + close)
    // +32..39 colours: work area background = logical colour 0
    createBlock_[35] = 0;
    // +40..+52 work area extent: min (0,-10000), max (640,0)
    ((int*)createBlock_)[10] = 0;
    ((int*)createBlock_)[11] = -10000;
    ((int*)createBlock_)[12] = 640;
    ((int*)createBlock_)[13] = 0;
    // +68 min window width, +70 min window height (2 bytes each)
    createBlock_[68] = 640 & 0xff;
    createBlock_[69] = (640 >> 8) & 0xff;
    createBlock_[70] = 500 & 0xff;
    createBlock_[71] = (500 >> 8) & 0xff;
    // +72 title data
    strncpy((char*)(createBlock_ + 72), "PETSCII Robots", 12);
    // +84 number of icons
    ((int*)createBlock_)[21] = 0;

    {
        _kernel_swi_regs in, out;
        in.r[1] = (int)createBlock_;
        _kernel_oserror* err = _kernel_swi(Wimp_CreateWindow, &in, &out);
        if (err) {
            printf("%s", unableToInitDisplay);
            return;
        }
        windowHandle_ = out.r[0] & 0xffff;
    }
    ((int*)createBlock_)[0] = windowHandle_;

    // Open the window: place near the top-left of the screen.
    openGameWindow();

    // Load font data
    fontData = (uint8_t*)malloc(FONT_SIZE);
    if (!fontData) {
        printf("%s", notEnoughMemoryError);
        return;
    }
    loadRawFile(FONT_PATH, fontData, FONT_SIZE);

    // Allocate load buffer
    loadBufferSize_ = 200000; // Large enough for biggest asset
    loadBuffer_ = new uint8_t[loadBufferSize_];
    if (!loadBuffer_) {
        printf("%s", notEnoughMemoryError);
        return;
    }

    // Initialize audio
    initAudio();

    // Preload assets
    loadAssets();

    platform = this;
}

PlatformRISCOS::~PlatformRISCOS()
{
    // Stop audio
    stopModule();
    cleanupAudio();

    // Free chunky tile data
    for (int i = 0; i < NUM_TILES; i++) {
        free(tileChunky[i]);
    }
    free(animTileChunky);
    free(itemChunky);
    free(keyChunky);
    free(healthChunky);
    free(faceChunky);
    free(spriteChunky);
    free(tilesMask);
    delete palette;
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
        in.r[1] = 0x4b534154;   // 'TASK'
        _kernel_swi(Wimp_CloseDown, &in, &out);
        taskHandle_ = -1;
    }
}

// ============================================================
// Wimp / display helpers
// ============================================================

void PlatformRISCOS::initWimp()
{
    _kernel_swi_regs in, out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.r[0] = 310;                    // Wimp version x 100 (RISC OS 3+)
    in.r[1] = 0x4b534154;             // 'TASK'
    in.r[2] = (int)"PETSCII Robots";  // short description
    in.r[3] = 0;                      // no messages important

    _kernel_oserror* err = _kernel_swi(Wimp_Initialise, &in, &out);
    if (err) {
        return;
    }
    taskHandle_ = out.r[1];
}

uint32_t PlatformRISCOS::monotonicTime()
{
    _kernel_swi_regs in, out;
    memset(&in, 0, sizeof(in));
    in.r[0] = 0;
    _kernel_swi(OS_ReadMonotonicTime, &in, &out);
    return (uint32_t)out.r[0];
}

void PlatformRISCOS::openGameWindow()
{
    if (windowHandle_ < 0) return;

    // Work area is 640 x 400; allow space for the title bar.
    int winWidth = 640;
    int titleGuess = 48;   // standard title bar height in OS units (approx)
    int winHeight = 400 + titleGuess;

    int x0 = (int)((screenOSWidth_ - winWidth) / 2);
    if (x0 < 0) x0 = 0;
    int y1 = screenOSHeight_ - 8;                       // near the top of the screen
    if (y1 < winHeight) y1 = winHeight + 8;
    int y0 = y1 - winHeight;

    // Window handle first
    ((int*)openBlock_)[0] = windowHandle_;
    ((int*)openBlock_)[1] = x0;       // visible x0
    ((int*)openBlock_)[2] = y0;       // visible y0
    ((int*)openBlock_)[3] = x0 + winWidth;
    ((int*)openBlock_)[4] = y1;
    ((int*)openBlock_)[5] = 0;        // scroll x
    ((int*)openBlock_)[6] = 0;        // scroll y
    ((int*)openBlock_)[7] = -1;       // open behind: top
    ((int*)openBlock_)[8] = (1 << 1) | 0x86000000;   // same flags as created
    ((int*)openBlock_)[9] = 0;        // work extent min x
    ((int*)openBlock_)[10] = -10000;
    ((int*)openBlock_)[11] = 640;
    ((int*)openBlock_)[12] = 0;

    _kernel_swi_regs in, out;
    in.r[1] = (int)openBlock_;
    _kernel_swi(Wimp_OpenWindow, &in, &out);
    windowOpened_ = true;
    windowCalibrated_ = false;
}

void PlatformRISCOS::buildPixtrans()
{
    // Map each of the 16 game colours to the nearest logical colour on the
    // desktop by reading the screen palette. Rebuilt whenever the game
    // changes its palette (displayImage).
    int nColours = 1 << screenLog2BPP_;
    if (nColours < 2) nColours = 2;
    if (nColours > 256) nColours = 256;

    for (int c = 0; c < 16; c++) {
        int tr = (amigaPalette[c] >> 8) & 0x0f;   // 12-bit 0RGB
        int tg = (amigaPalette[c] >> 4) & 0x0f;
        int tb = amigaPalette[c] & 0x0f;
        tr = (tr << 4) | tr;
        tg = (tg << 4) | tg;
        tb = (tb << 4) | tb;

        int best = 0;
        long bestDist = 0x7fffffffL;
        _kernel_swi_regs in, out;
        for (int col = 0; col < nColours; col++) {
            in.r[0] = col;
            in.r[1] = 16;   // normal colour
            _kernel_swi(OS_ReadPalette, &in, &out);
            uint32_t word = (uint32_t)out.r[2];
            int sr = (word >> 8) & 0xff;
            int sg = (word >> 16) & 0xff;
            int sb = (word >> 24) & 0xff;
            long d = (long)(tr - sr) * (tr - sr) +
                     (long)(tg - sg) * (tg - sg) +
                     (long)(tb - sb) * (tb - sb);
            if (d < bestDist) {
                bestDist = d;
                best = col;
            }
        }
        pixtrans_[c] = (uint8_t)best;
    }
    for (int c = 16; c < 256; c++) {
        pixtrans_[c] = (uint8_t)c;
    }
}

void PlatformRISCOS::plotGameImage()
{
    static const int scaleFactors[4] = { 2, 2, 1, 1 };
    if (!sprite_) return;

    _kernel_swi_regs in, out;
    in.r[0] = 52 + 512;   // OS_SpriteOp 52 with R2 = pointer to sprite
    in.r[1] = 0;
    in.r[2] = (int)sprite_;
    in.r[3] = shakeOffsetX_;
    in.r[4] = -400;
    in.r[5] = 0;                       // plot action: replace
    in.r[6] = (int)scaleFactors;
    in.r[7] = (int)pixtrans_;
    _kernel_swi(OS_SpriteOp, &in, &out);
}

void PlatformRISCOS::processRedraw(uint8_t* pollBlock)
{
    // pollBlock from the poll: +0 window handle, +4..+19 visible area,
    // +20/+24 scroll, +28..+43 graphics rectangle (screen coords).
    _kernel_swi_regs in, out;
    in.r[1] = (int)pollBlock;

    _kernel_swi(Wimp_RedrawWindow, &in, &out);
    int more = out.r[0];

    while (more) {
        // Graphics origin is the work-area origin; clip is the rectangle.
        if (!windowCalibrated_) {
            // The redraw rectangle covers the whole visible work area, so its
            // height (in OS units) is the visible work area height.
            int x0 = ((int*)pollBlock)[7];
            int y0 = ((int*)pollBlock)[8];
            int x1 = ((int*)pollBlock)[9];
            int y1 = ((int*)pollBlock)[10];
            (void)x0; (void)x1;
            int visH = y1 - y0;
            int delta = 400 - visH;

            if (delta < -2 || delta > 2) {
                int curH = ((int*)openBlock_)[4] - ((int*)openBlock_)[2];
                int newH = curH + delta;
                if (newH < 440) newH = 440;
                if (newH > 1000) newH = 1000;
                ((int*)openBlock_)[2] = ((int*)openBlock_)[4] - newH;
                _kernel_swi_regs oin, oout;
                oin.r[1] = (int)openBlock_;
                _kernel_swi(Wimp_OpenWindow, &oin, &oout);
                windowCalibrated_ = false;
            } else {
                windowCalibrated_ = true;
            }
        }

        plotGameImage();

        _kernel_swi(Wimp_GetRectangle, &in, &out);
        more = out.r[0];
    }
}

void PlatformRISCOS::processEvents(int event, uint8_t* pollBlock)
{
    switch (event) {
        case 1:   // Redraw_Window_Request
            processRedraw(pollBlock);
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
            quit = true;
            break;
        default:
            break;
    }
}

void PlatformRISCOS::renderFrame(bool waitForNextFrame)
{
    if (!sprite_ || !chunkyBuffer || !windowOpened_) return;

    uint32_t now = monotonicTime();

    // Pace the loop to 50 fps: next frame at least 2 centiseconds on.
    uint32_t targetClock = clock_ ? (clock_ + 2) : (now + 1);
    if (targetClock < now + 1) targetClock = now + 1;
    clock_ = now;

    // Request a redraw of our window.
    {
        int forceRect[4] = { 0, -400, 640, 0 };
        _kernel_swi_regs in, out;
        in.r[1] = windowHandle_;
        in.r[2] = (int)forceRect;
        _kernel_swi(Wimp_ForceRedraw, &in, &out);
    }

    // Poll until we have an event (or the frame timer elapses), handling
    // window events as they arrive. Further redraws are serviced on the
    // next renderFrame call.
    uint32_t pollTimeout = waitForNextFrame ? targetClock : (now + 1);

    for (;;) {
        _kernel_swi_regs in, out;
        in.r[0] = (1 << 4) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 11) | (1 << 12);
        in.r[1] = (int)pollBlock_;
        in.r[2] = (int)pollTimeout;
        _kernel_swi(Wimp_PollIdle, &in, &out);

        int event = out.r[0];
        processEvents(event, pollBlock_);

        if (event == 0 || quit) {
            break;
        }
    }

    // Execute interrupt callback if set
    if (interrupt_) {
        interrupt_();
    }

    frameCount_++;
}

void PlatformRISCOS::waitForScreenMemoryAccess()
{
    // No hardware blitter to wait for under the Wimp.
}

void PlatformRISCOS::setHighlightedMenuRow(uint16_t row)
{
}

// ============================================================
// File loading
// ============================================================

uint32_t PlatformRISCOS::loadFile(const char* filename, uint8_t* destination, uint32_t size)
{
    char path[300];
    translatePath(path, sizeof(path), filename);

    FILE* file = fopen(path, "rb");
    if (file) {
        size_t bytesRead = fread(destination, 1, size, file);
        fclose(file);
        if (bytesRead > 0) return (uint32_t)bytesRead;
    }

    // Fallback: try .gz (if a gzipped copy is supplied for FTP users)
    char gzPath[300];
    snprintf(gzPath, sizeof(gzPath), "%s.gz", path);
    FILE* gz = fopen(gzPath, "rb");
    if (gz) {
        size_t bytesRead = fread(destination, 1, size, gz);
        fclose(gz);   // gzip not decoded; caller ignores failure
        if (bytesRead > 0) return (uint32_t)bytesRead;
    }

    printf("%s", unableToLoadData);
    return 0;
}

void PlatformRISCOS::loadRawFile(const char* filename, uint8_t* destination, uint32_t size)
{
    loadFile(filename, destination, size);
}

void PlatformRISCOS::loadAssets()
{
    // Preload images
    screenMemory = (uint8_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32);
    if (screenMemory) {
        loadFile(IMAGE_PATHS[0], screenMemory, SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32);
    }

    // Preload modules (sizes probed so the data is captured)
    for (int i = 0; i < 8; i++) {
        preloadedModuleData_[i] = 0;
        preloadedModuleLengths_[i] = 0;
    }
}

void PlatformRISCOS::initAudio()
{
    // Sounds are played through OS_Sound; nothing to initialise.
    audioInitialized_ = true;
}

void PlatformRISCOS::cleanupAudio()
{
    audioInitialized_ = false;
}

// ============================================================
// Platform interface methods
// ============================================================

uint8_t* PlatformRISCOS::standardControls() const
{
    return ::standardControls;
}

void PlatformRISCOS::setInterrupt(void (*interrupt)(void))
{
    interrupt_ = interrupt;
}

void PlatformRISCOS::show()
{
    if (windowOpened_) {
        _kernel_swi_regs in, out;
        in.r[1] = (int)openBlock_;
        _kernel_swi(Wimp_OpenWindow, &in, &out);
    }
}

int PlatformRISCOS::framesPerSecond()
{
    return framesPerSecond_;
}

// ============================================================
// Keyboard
// ============================================================

int PlatformRISCOS::internalKeyPressed(int internalKey)
{
    // OS_Byte 121 single-key test:
    //   R1 = internal key number EOR &80
    //   returns &FF if the key is down, 0 if not.
    return _kernel_osbyte(121, internalKey ^ 0x80, 0) == 0xff;
}

static bool prevKeysDown[64];

void PlatformRISCOS::scanKeyboard()
{
    bool newShift = internalKeyPressed(3) || internalKeyPressed(6);

    int held = -1;
    uint8_t virt = 0xff;
    int pressedIdx = -1;   // first key newly pressed since the last scan

    for (int i = 0; i < keyMapSize && i < 64; i++) {
        bool down = internalKeyPressed(keyMap[i].internal) != 0;
        if (down && !prevKeysDown[i] && pressedIdx < 0) {
            pressedIdx = i;
        }
        if (down && held < 0) {
            held = keyMap[i].internal;
            virt = keyMap[i].virt;
        }
        prevKeysDown[i] = down;
    }

    uint8_t code = (held >= 0) ? (virt | (newShift ? 0x80 : 0)) : 0xff;

    // A newly pressed key becomes the code to return on the next read.
    if (pressedIdx >= 0) {
        keyToReturn_ = keyMap[pressedIdx].virt | (newShift ? 0x80 : 0);
    }
    // Down key tracks the currently held key (level), updated on any change.
    downKey_ = code;
    shift_ = newShift ? 0x80 : 0;
}

uint8_t PlatformRISCOS::readKeyboard()
{
    scanKeyboard();

    uint8_t result = keyToReturn_;
    keyToReturn_ = 0xff;
    return result;
}

void PlatformRISCOS::keyRepeat()
{
    keyToReturn_ = downKey_;
}

void PlatformRISCOS::clearKeyBuffer()
{
    keyToReturn_ = 0xff;
    downKey_ = 0xff;
    lastHeldInternal_ = -1;
    shift_ = 0;
}

bool PlatformRISCOS::isKeyOrJoystickPressed(bool gamepad)
{
    return downKey_ != 0xff;
}

uint16_t PlatformRISCOS::readJoystick(bool gamepad)
{
    // No joystick/gamepad support on RISC OS in this port.
    return 0;
}

void PlatformRISCOS::rumble(uint8_t strength)
{
    // No rumble support.
}

// ============================================================
// Images, maps and tiles
// ============================================================

void PlatformRISCOS::displayImage(Image image)
{
    if (!screenMemory) return;

    // Load the image (planar format: 320*200*4/8 = 32000 bytes + 32 bytes palette)
    uint32_t imageSize = SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8;
    loadFile(IMAGE_PATHS[image], screenMemory, imageSize + 32);

    // Extract palette (stored after the pixel data)
    uint16_t* paletteData = (uint16_t*)(screenMemory + imageSize);
    for (int i = 0; i < 16; i++) {
        amigaPalette[i] = paletteData[i];
    }

    // Convert planar image data to chunky framebuffer
    planarToChunky(screenMemory, chunkyBuffer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Rebuild the pixel translation table for the new palette
    buildPixtrans();
}

void PlatformRISCOS::loadMap(Map map, uint8_t* destination)
{
    MAPNAME[17] = 'a' + map;
    loadFile(MAPNAME, destination, 8960);
}

uint8_t* PlatformRISCOS::loadTileset()
{
    // Read tileset from file
    loadRawFile(TILESET_PATH, tilesetData, 514);
    return tilesetData;
}

// ============================================================
// Tile generation (ported verbatim from the MorphOS version)
// ============================================================

void PlatformRISCOS::generateTiles(uint8_t* tileData, uint8_t* tileAttributes)
{
    // Load planar tile bitmaps
    // Tiles.raw is 4-bitplane interleaved, 32x24 pixels per tile, 253 tiles
    // File size: 97152 bytes = 32 * 24 * 4 / 8 * 253 = 384 * 253 = 97152
    uint32_t tilesFileSize = 97152;
    uint8_t* tilesPlanar = (uint8_t*)malloc(tilesFileSize);
    if (!tilesPlanar) return;
    loadRawFile(TILES_PATH, tilesPlanar, tilesFileSize);

    // Convert each tile from planar to chunky
    int tileWidth = 32;
    int tileHeight = 24;
    int srcBytesPerRow = (tileWidth / 8) * PLANES; // 16 bytes per row

    for (int t = 0; t < 253; t++) {
        tileChunky[t] = (uint8_t*)malloc(TILE_PIXEL_SIZE);
        if (!tileChunky[t]) continue;

        uint8_t* chunky = tileChunky[t];
        uint8_t* src = tilesPlanar + t * tileHeight * srcBytesPerRow;

        for (int y = 0; y < tileHeight; y++) {
            for (int x = 0; x < tileWidth; x++) {
                uint8_t color = 0;
                for (int p = 0; p < PLANES; p++) {
                    int bit = 7 - (x & 7);
                    int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                    if (src[byteOffset] & (1 << bit)) {
                        color |= (1 << p);
                    }
                }
                chunky[y * tileWidth + x] = color;
            }
        }
    }

    free(tilesPlanar);

    // Load animated tiles
    uint32_t animFileSize = 9216;
    uint8_t* animPlanar = (uint8_t*)malloc(animFileSize);
    if (animPlanar) {
        loadRawFile(ANIM_TILES_PATH, animPlanar, animFileSize);
        animTileChunky = (uint8_t*)malloc(17 * TILE_PIXEL_SIZE);
        if (animTileChunky) {
            for (int t = 0; t < 17; t++) {
                uint8_t* chunky = animTileChunky + t * TILE_PIXEL_SIZE;
                uint8_t* src = animPlanar + t * tileHeight * srcBytesPerRow;
                for (int y = 0; y < tileHeight; y++) {
                    for (int x = 0; x < tileWidth; x++) {
                        uint8_t color = 0;
                        for (int p = 0; p < PLANES; p++) {
                            int bit = 7 - (x & 7);
                            int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                            if (src[byteOffset] & (1 << bit)) {
                                color |= (1 << p);
                            }
                        }
                        chunky[y * tileWidth + x] = color;
                    }
                }
            }
        }
        free(animPlanar);
    }

    // Load and convert sprite data
    uint32_t spriteFileSize = 31872;
    uint8_t* spritePlanar = (uint8_t*)malloc(spriteFileSize);
    uint8_t* spriteMaskPlanar = (uint8_t*)malloc(spriteFileSize);
    if (spritePlanar && spriteMaskPlanar) {
        loadRawFile(SPRITES_PATH, spritePlanar, spriteFileSize);
        loadRawFile(SPRITES_MASK_PATH, spriteMaskPlanar, spriteFileSize);

        int spritesPerRow = spriteFileSize / (tileHeight * srcBytesPerRow);
        spriteChunky = (uint8_t*)malloc(spritesPerRow * TILE_PIXEL_SIZE);
        tilesMask = (uint8_t*)malloc(spritesPerRow * TILE_PIXEL_SIZE);
        if (spriteChunky && tilesMask) {
            for (int t = 0; t < spritesPerRow; t++) {
                uint8_t* chunky = spriteChunky + t * TILE_PIXEL_SIZE;
                uint8_t* mask = tilesMask + t * TILE_PIXEL_SIZE;
                uint8_t* src = spritePlanar + t * tileHeight * srcBytesPerRow;
                uint8_t* srcMask = spriteMaskPlanar + t * tileHeight * srcBytesPerRow;
                for (int y = 0; y < tileHeight; y++) {
                    for (int x = 0; x < tileWidth; x++) {
                        uint8_t color = 0;
                        uint8_t maskVal = 0;
                        for (int p = 0; p < PLANES; p++) {
                            int bit = 7 - (x & 7);
                            int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                            if (src[byteOffset] & (1 << bit)) {
                                color |= (1 << p);
                            }
                            if (srcMask[byteOffset] & (1 << bit)) {
                                maskVal |= (1 << p);
                            }
                        }
                        chunky[y * tileWidth + x] = color;
                        mask[y * tileWidth + x] = maskVal;
                    }
                }
            }
        }
    }
    free(spritePlanar);
    free(spriteMaskPlanar);

    // Load and convert HUD elements
    // Items.raw: 4-bitplane, 48x21 per item, 6 items
    int itemW = 48, itemH = 21, itemN = 6;
    int itemBytesPerRow = (itemW / 8) * PLANES;
    uint32_t itemFileSize = itemN * itemH * itemBytesPerRow;
    uint8_t* itemPlanar = (uint8_t*)malloc(itemFileSize);
    if (itemPlanar) {
        loadRawFile(ITEMS_PATH, itemPlanar, itemFileSize);
        itemChunky = (uint8_t*)malloc(itemN * itemW * itemH);
        if (itemChunky) {
            for (int t = 0; t < itemN; t++) {
                uint8_t* chunky = itemChunky + t * itemW * itemH;
                uint8_t* src = itemPlanar + t * itemH * itemBytesPerRow;
                planarToChunky(src, chunky, itemW, itemH);
            }
        }
        free(itemPlanar);
    }
    itemWidth = itemW; itemHeight = itemH; itemCount = itemN;

    // Keys.raw: 4-bitplane, 16x14 per key, 3 keys
    int keyW = 16, keyH = 14, keyN = 3;
    int keyBytesPerRow = (keyW / 8) * PLANES;
    uint32_t keyFileSize = keyN * keyH * keyBytesPerRow;
    uint8_t* keyPlanar = (uint8_t*)malloc(keyFileSize);
    if (keyPlanar) {
        loadRawFile(KEYS_PATH, keyPlanar, keyFileSize);
        keyChunky = (uint8_t*)malloc(keyN * keyW * keyH);
        if (keyChunky) {
            for (int t = 0; t < keyN; t++) {
                uint8_t* chunky = keyChunky + t * keyW * keyH;
                uint8_t* src = keyPlanar + t * keyH * keyBytesPerRow;
                planarToChunky(src, chunky, keyW, keyH);
            }
        }
        free(keyPlanar);
    }
    keyWidth = keyW; keyHeight = keyH; keyCount = keyN;

    // Health.raw: 4-bitplane, 48x51 per health, 3 health states
    int healthW = 48, healthH = 51, healthN = 3;
    int healthBytesPerRow = (healthW / 8) * PLANES;
    uint32_t healthFileSize = healthN * healthH * healthBytesPerRow;
    uint8_t* healthPlanar = (uint8_t*)malloc(healthFileSize);
    if (healthPlanar) {
        loadRawFile(HEALTH_PATH, healthPlanar, healthFileSize);
        healthChunky = (uint8_t*)malloc(healthN * healthW * healthH);
        if (healthChunky) {
            for (int t = 0; t < healthN; t++) {
                uint8_t* chunky = healthChunky + t * healthW * healthH;
                uint8_t* src = healthPlanar + t * healthH * healthBytesPerRow;
                planarToChunky(src, chunky, healthW, healthH);
            }
        }
        free(healthPlanar);
    }
    healthWidth = healthW; healthHeight = healthH; healthCount = healthN;

    // Faces.raw: 4-bitplane, 16x24 per face, 3 faces
    int faceW = 16, faceH = 24, faceN = 3;
    int faceBytesPerRow = (faceW / 8) * PLANES;
    uint32_t faceFileSize = faceN * faceH * faceBytesPerRow;
    uint8_t* facePlanar = (uint8_t*)malloc(faceFileSize);
    if (facePlanar) {
        loadRawFile(FACES_PATH, facePlanar, faceFileSize);
        faceChunky = (uint8_t*)malloc(faceN * faceW * faceH);
        if (faceChunky) {
            for (int t = 0; t < faceN; t++) {
                uint8_t* chunky = faceChunky + t * faceW * faceH;
                uint8_t* src = facePlanar + t * faceH * faceBytesPerRow;
                planarToChunky(src, chunky, faceW, faceH);
            }
        }
        free(facePlanar);
    }
    faceWidth = faceW; faceHeight = faceH; faceCount = faceN;

    // Determine transparency for tiles
    for (int i = 0; i < 256; i++) {
        if (spriteTileMap[i] >= 0) {
            tileHasTransparency[i] = true;
        }
    }
    // Additional transparent tiles (explosions, bombs, etc.)
    int transparentTiles[] = {130, 134, 240, 241, 244, 245, 246, 248, 249, 250, 251, 252, -1};
    for (int i = 0; transparentTiles[i] >= 0; i++) {
        tileHasTransparency[transparentTiles[i]] = true;
    }

    // Build live map tile mapping
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
    memcpy(tileLiveMap, defaultTileLiveMap, 256);
}

// ============================================================
// Chunky buffer rendering primitives (ported from MorphOS)
// ============================================================

void PlatformRISCOS::chunkyBlit(const uint8_t* source, uint16_t dx, uint16_t dy,
                                uint16_t w, uint16_t h, bool transparent, uint8_t transparentColor)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t pixel = source[y * w + x];
            if (!transparent || pixel != transparentColor) {
                int dstX = dx + x;
                int dstY = dy + y;
                if (dstX >= 0 && dstX < SCREEN_WIDTH && dstY >= 0 && dstY < SCREEN_HEIGHT) {
                    chunkyBuffer[dstY * SCREEN_WIDTH + dstX] = pixel;
                }
            }
        }
    }
}

void PlatformRISCOS::renderTile(uint8_t tile, uint16_t x, uint16_t y, uint8_t variant, bool transparent)
{
    uint8_t* src;
    int tileW = 32, tileH = 24;

    // Determine source data
    if (transparent && spriteTileMap[tile] >= 0) {
        // Sprite-based tile
        int spriteIdx = spriteTileMap[tile] + variant;
        if (spriteChunky) {
            src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
        } else {
            return;
        }
    } else if (animTileMap[tile] >= 0) {
        // Animated tile
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

    // Calculate destination in chunky buffer
    uint16_t destX = x * 8;
    uint16_t destY = y * 8;

    // Check if transparency masking is needed
    bool useTransparency = transparent || tileHasTransparency[tile];

    // Blit to chunky buffer
    for (int row = 0; row < tileH; row++) {
        for (int col = 0; col < tileW; col++) {
            uint8_t pixel = src[row * tileW + col];
            if (!useTransparency || pixel != 0) {
                int px = destX + col;
                int py = destY + row;
                if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
                    chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
                }
            }
        }
    }
}

void PlatformRISCOS::renderTiles(uint8_t backgroundTile, uint8_t foregroundTile,
                                 uint16_t x, uint16_t y,
                                 uint8_t backgroundVariant, uint8_t foregroundVariant)
{
    // Render background tile first
    renderTile(backgroundTile, x, y, backgroundVariant, false);

    // Render foreground tile with transparency
    if (spriteTileMap[foregroundTile] >= 0) {
        int spriteIdx = spriteTileMap[foregroundTile] + foregroundVariant;
        if (spriteChunky) {
            uint16_t destX = x * 8;
            uint16_t destY = y * 8;
            uint8_t* src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
            for (int row = 0; row < 24; row++) {
                for (int col = 0; col < 32; col++) {
                    uint8_t pixel = src[row * 32 + col];
                    if (pixel != 0) {
                        int px = destX + col;
                        int py = destY + row;
                        if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
                            chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
                        }
                    }
                }
            }
        }
    } else {
        renderTile(foregroundTile, x, y, foregroundVariant, true);
    }
}

void PlatformRISCOS::renderItem(uint8_t item, uint16_t x, uint16_t y)
{
    if (!itemChunky || item >= itemCount) return;
    uint8_t* src = itemChunky + item * itemWidth * itemHeight;
    chunkyBlit(src, x, y, itemWidth, itemHeight, true, 0);
}

void PlatformRISCOS::renderKey(uint8_t key, uint16_t x, uint16_t y)
{
    if (!keyChunky || key >= keyCount) return;
    uint8_t* src = keyChunky + key * keyWidth * keyHeight;
    chunkyBlit(src, x, y, keyWidth, keyHeight, true, 0);
}

void PlatformRISCOS::renderHealth(uint8_t health, uint16_t x, uint16_t y)
{
    if (!healthChunky || health >= healthCount) return;
    uint8_t* src = healthChunky + health * healthWidth * healthHeight;
    chunkyBlit(src, x, y, healthWidth, healthHeight, true, 0);
}

void PlatformRISCOS::renderFace(uint8_t face, uint16_t x, uint16_t y)
{
    if (!faceChunky || face >= faceCount) return;
    uint8_t* src = faceChunky + face * faceWidth * faceHeight;
    chunkyBlit(src, x, y, faceWidth, faceHeight, true, 0);
}

// ============================================================
// Live map
// ============================================================

void PlatformRISCOS::renderLiveMap(uint8_t* map)
{
    // Clear map area
    clearRect(0, 0, SCREEN_WIDTH - 56, SCREEN_HEIGHT - 32);

    // Draw the map: 128x64 tiles, each tile is 2x2 pixels
    for (int my = 0; my < 64; my++) {
        for (int mx = 0; mx < 128; mx++) {
            uint8_t tile = tileLiveMap[map[my * 128 + mx]];
            uint8_t color = tile & 0x0f;
            int px = mx * 2;
            int py = 20 + my * 2;
            if (py >= 0 && py + 1 < SCREEN_HEIGHT && px >= 0 && px + 1 < SCREEN_WIDTH) {
                chunkyBuffer[py * SCREEN_WIDTH + px] = color;
                chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
                chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
                chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
            }
        }
    }
}

void PlatformRISCOS::renderLiveMapTile(uint8_t* map, uint8_t mx, uint8_t my)
{
    uint8_t tile = tileLiveMap[map[(my << 7) + mx]];
    uint8_t color = tile & 0x0f;
    int px = mx * 2;
    int py = 20 + my * 2;
    if (py >= 0 && py + 1 < SCREEN_HEIGHT && px >= 0 && px + 1 < SCREEN_WIDTH) {
        chunkyBuffer[py * SCREEN_WIDTH + px] = color;
        chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
        chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
        chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
    }
}

void PlatformRISCOS::renderLiveMapUnits(uint8_t* map, uint8_t* unitTypes,
                                        uint8_t* unitX, uint8_t* unitY,
                                        uint8_t playerColor, bool showRobots)
{
    // Render unit dots on live map
    for (int i = 0; i < 48; i++) {
        if (i == 0 || (unitTypes[i] != 255 && unitTypes[i] != 0)) {
            if (i == 0 || showRobots || unitTypes[i] == 22) {
                int mx = unitX[i];
                int my = unitY[i];
                int px = mx * 2;
                int py = 20 + my * 2;
                uint8_t color = (i == 0) ? playerColor : 15;
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

// ============================================================
// Cursor
// ============================================================

void PlatformRISCOS::showCursor(uint16_t x, uint16_t y)
{
    cursorX_ = x;
    cursorY_ = y;
    cursorVisible_ = true;

    // Draw a simple crosshair cursor
    int px = x * 8 + 8;
    int py = y * 8 + 8;
    uint8_t color = 15; // white

    for (int i = -1; i <= 1; i++) {
        if (px + i >= 0 && px + i < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT)
            chunkyBuffer[py * SCREEN_WIDTH + px + i] = color;
        if (px >= 0 && px < SCREEN_WIDTH && py + i >= 0 && py + i < SCREEN_HEIGHT)
            chunkyBuffer[(py + i) * SCREEN_WIDTH + px] = color;
    }
}

void PlatformRISCOS::hideCursor()
{
    cursorVisible_ = false;
}

void PlatformRISCOS::setCursorShape(CursorShape shape)
{
    cursorShape_ = shape;
}

// ============================================================
// Rect operations
// ============================================================

void PlatformRISCOS::copyRect(uint16_t sourceX, uint16_t sourceY,
                              uint16_t destinationX, uint16_t destinationY,
                              uint16_t width, uint16_t height)
{
    // Simple blit within chunky buffer
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx = sourceX + x;
            int sy = sourceY + y;
            int dx = destinationX + x;
            int dy = destinationY + y;
            if (sx < SCREEN_WIDTH && sy < SCREEN_HEIGHT &&
                dx < SCREEN_WIDTH && dy < SCREEN_HEIGHT) {
                chunkyBuffer[dy * SCREEN_WIDTH + dx] = chunkyBuffer[sy * SCREEN_WIDTH + sx];
            }
        }
    }
}

void PlatformRISCOS::clearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    for (int row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
            chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = 0;
        }
    }
}

void PlatformRISCOS::fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color)
{
    for (int row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
            chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = color;
        }
    }
}

// ============================================================
// Screen shake
// ============================================================

void PlatformRISCOS::startShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

void PlatformRISCOS::shakeScreen()
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

void PlatformRISCOS::stopShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

// ============================================================
// Fade effects
// ============================================================

void PlatformRISCOS::startFadeScreen(uint16_t color, uint16_t intensity)
{
    if (!palette) return;
    palette->setFadeBaseColor(color);
    palette->setFade(intensity);
}

void PlatformRISCOS::fadeScreen(uint16_t intensity, bool immediate)
{
    if (!palette) return;
    uint16_t fade = palette->fade();
    if (fade != intensity) {
        if (immediate) {
            palette->setFade(intensity);
        } else {
            int16_t fadeDelta = intensity > fade ? 1 : -1;
            do {
                fade += fadeDelta;
                palette->setFade(fade);
                // Small delay so step fades are visible.
                uint32_t t0 = monotonicTime();
                while ((monotonicTime() - t0) < 1) {
                }
            } while (fade != intensity);
        }
    }
}

void PlatformRISCOS::stopFadeScreen()
{
    if (!palette) return;
    palette->setFade(15);
}

// ============================================================
// Screen memory access (character-based text output)
// ============================================================

void PlatformRISCOS::writeToScreenMemory(address_t address, uint8_t value)
{
    writeToScreenMemory(address, value, 10, 0);
}

void PlatformRISCOS::writeToScreenMemory(address_t address, uint8_t value,
                                         uint8_t color, uint8_t yOffset)
{
    if (!fontData) return;

    bool reverse = value > 127;
    uint8_t charIdx = value & 127;
    uint8_t* glyph = fontData + charIdx * 8;

    uint16_t startX = addressMap[address] % SCREEN_WIDTH;
    uint16_t startY = addressMap[address] / SCREEN_WIDTH + yOffset;

    for (int row = 0; row < 8 && (startY + row) < SCREEN_HEIGHT; row++) {
        uint8_t fontByte = reverse ? ~glyph[row] : glyph[row];
        for (int col = 0; col < 8 && (startX + col) < SCREEN_WIDTH; col++) {
            if (fontByte & (1 << (7 - col))) {
                chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = color;
            } else {
                chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = 0;
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

void PlatformRISCOS::playNote(uint8_t note)
{
}

void PlatformRISCOS::stopNote()
{
}

void PlatformRISCOS::loadModule(Module module)
{
    loadedModule = module;
}

void PlatformRISCOS::playModule(Module module)
{
    // MOD music is not played on RISC OS.
    loadedModule = module;
}

void PlatformRISCOS::pauseModule()
{
}

void PlatformRISCOS::stopModule()
{
}

void PlatformRISCOS::playSample(uint8_t sample)
{
    if (sample >= 16) return;

    _kernel_swi_regs in, out;
    // Channel 1, amplitude 12, waveform selected by sound effect index.
    int wave = (sample % 8) + 8;
    in.r[0] = (1 << 8) | (12 << 4) | (wave & 0x0f);
    in.r[1] = sample;                 // pitch varies with the effect
    in.r[2] = 2;                      // duration: 2 centiseconds
    _kernel_swi(OS_Sound, &in, &out);
}

void PlatformRISCOS::stopSample()
{
}

void PlatformRISCOS::loadSamples()
{
    // Raw samples are not cached; playSample() synthesises via OS_Sound.
}