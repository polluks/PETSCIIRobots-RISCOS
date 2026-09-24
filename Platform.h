#ifndef _PLATFORM_H
#define _PLATFORM_H

#define SCREEN_WIDTH_IN_CHARACTERS (PLATFORM_SCREEN_WIDTH / 8)
#define SCREEN_HEIGHT_IN_CHARACTERS (PLATFORM_SCREEN_HEIGHT / 8)

#ifndef PLATFORM_MAP_COUNT
#define PLATFORM_MAP_COUNT 14
#endif

#ifndef PLATFORM_INTRO_OPTIONS
#define PLATFORM_INTRO_OPTIONS 4
#endif

#include <stdint.h>
typedef unsigned char bool;
#define true 1
#define false 0
#ifndef INT32_MAX
#define INT32_MAX 0x7fffffff
#endif
#ifndef INT32_MIN
#define INT32_MIN 0x80000000
#endif
#ifndef INT16_MAX
#define INT16_MAX 0x7fff
#endif
#ifndef INT16_MIN
#define INT16_MIN 0x8000
#endif
#ifndef INT8_MAX
#define INT8_MAX 0x7f
#endif
#ifndef INT8_MIN
#define INT8_MIN 0x80
#endif

typedef uint32_t address_t;

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(a) ((a) >= 0 ? (a) : -(a))

enum Map {
    Map01,
    Map02,
    Map03,
    Map04,
    Map05,
    Map06,
    Map07,
    Map08,
    Map09,
    Map10,
    Map11,
    Map12,
    Map13,
    Map14
};

enum Image {
    ImageIntro,
    ImageGame,
    ImageGameOver
};

enum Module {
    ModuleSoundFX,
    ModuleIntro,
    ModuleWin,
    ModuleLose,
    ModuleInGame1,
    ModuleInGame2,
    ModuleInGame3,
    ModuleInGame4
};

enum JoystickBits {
    JoystickBitRight,
    JoystickBitLeft,
    JoystickBitDown,
    JoystickBitUp,
    JoystickBitRed,
    JoystickBitBlue,
    JoystickBitGreen,
    JoystickBitYellow,
    JoystickBitPlay,
    JoystickBitReverse,
    JoystickBitForward,
    JoystickBitExtra
};

enum JoystickMask {
    JoystickRight = (1 << JoystickBitRight),
    JoystickLeft = (1 << JoystickBitLeft),
    JoystickDown = (1 << JoystickBitDown),
    JoystickUp = (1 << JoystickBitUp),
    JoystickRed = (1 << JoystickBitRed),
    JoystickBlue = (1 << JoystickBitBlue),
    JoystickGreen = (1 << JoystickBitGreen),
    JoystickYellow = (1 << JoystickBitYellow),
    JoystickPlay = (1 << JoystickBitPlay),
    JoystickReverse = (1 << JoystickBitReverse),
    JoystickForward = (1 << JoystickBitForward),
    JoystickExtra = (1 << JoystickBitExtra)
};

enum CursorShape {
    ShapeUse,
    ShapeSearch,
    ShapeMove
};

typedef enum Map Map;
typedef enum Image Image;
typedef enum Module Module;
typedef enum CursorShape CursorShape;

// Platform interface (plain C). One shared implementation lives in
// PlatformRISCOS.c; platformInit() must return non-zero on success.
int platformInit(void);
void platformShutdown(void);
extern int platformQuit;

void platformInitWimp(void);
uint32_t platformMonotonicTime(void);
void platformOpenGameWindow(void);
void platformBuildPixtrans(void);
void platformPlotGameImage(void);
void platformProcessRedraw(uint8_t* pollBlock);
void platformProcessEvents(int event, uint8_t* pollBlock);
uint32_t platformLoadFile(const char* filename, uint8_t* destination, uint32_t size);
void platformLoadRawFile(const char* filename, uint8_t* destination, uint32_t size);
void platformLoadAssets(void);
void platformInitAudio(void);
void platformCleanupAudio(void);
int platformInternalKeyPressed(int internalKey);
void platformScanKeyboard(void);
void platformUndeltaSamples(uint8_t* module, uint32_t moduleSize);
void platformAudioTick(void);
void platformChunkyBlit(const uint8_t* source, uint16_t dx, uint16_t dy,
    uint16_t width, uint16_t height, bool updateTiles, uint8_t color);
void platformLoadSamples(void);

uint8_t* platformStandardControls(void);
void platformSetInterrupt(void (*interrupt)(void));
void platformShow(void);
int platformFramesPerSecond(void);
void platformChrout(uint8_t c);
uint8_t platformReadKeyboard(void);
void platformKeyRepeat(void);
void platformClearKeyBuffer(void);
bool platformIsKeyOrJoystickPressed(bool gamepad);
uint16_t platformReadJoystick(bool gamepad);
void platformLoadMap(Map map, uint8_t* destination);
uint8_t* platformLoadTileset(void);
void platformDisplayImage(Image image);
void platformGenerateTiles(uint8_t* tileData, uint8_t* tileAttributes);
void platformUpdateTiles(uint8_t* tileData, uint8_t* tiles, uint8_t numTiles);
void platformRenderTile(uint8_t tile, uint16_t x, uint16_t y, uint8_t variant, bool transparent);
void platformRenderTiles(uint8_t backgroundTile, uint8_t foregroundTile, uint16_t x, uint16_t y, uint8_t backgroundVariant, uint8_t foregroundVariant);
void platformRenderItem(uint8_t item, uint16_t x, uint16_t y);
void platformRenderKey(uint8_t key, uint16_t x, uint16_t y);
void platformRenderHealth(uint8_t health, uint16_t x, uint16_t y);
void platformRenderFace(uint8_t face, uint16_t x, uint16_t y);
void platformRenderLiveMap(uint8_t* map);
void platformRenderLiveMapTile(uint8_t* map, uint8_t x, uint8_t y);
void platformRenderLiveMapUnits(uint8_t* map, uint8_t* unitTypes, uint8_t* unitX, uint8_t* unitY, uint8_t playerColor, bool showRobots);
void platformShowCursor(uint16_t x, uint16_t y);
void platformHideCursor(void);
void platformSetCursorShape(CursorShape shape);
void platformCopyRect(uint16_t sourceX, uint16_t sourceY, uint16_t destinationX, uint16_t destinationY, uint16_t width, uint16_t height);
void platformClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void platformFillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);
void platformStartShakeScreen(void);
void platformShakeScreen(void);
void platformStopShakeScreen(void);
void platformStartFadeScreen(uint16_t color, uint16_t intensity);
void platformFadeScreen(uint16_t intensity, bool immediate);
void platformStopFadeScreen(void);
void platformWriteToScreenMemory(address_t address, uint8_t value, uint8_t color, uint8_t yOffset);
void platformPlayNote(uint8_t note);
void platformStopNote(void);
void platformLoadModule(Module module);
void platformPlayModule(Module module);
void platformPauseModule(void);
void platformStopModule(void);
void platformPlaySample(uint8_t sample);
void platformStopSample(void);
void platformRenderFrame(bool waitForNextFrame);
void platformWaitForScreenMemoryAccess(void);
void platformSetHighlightedMenuRow(uint16_t row);
void platformRumble(uint8_t strength);

#endif