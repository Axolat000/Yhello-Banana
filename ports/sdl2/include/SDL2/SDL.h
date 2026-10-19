/*
 * Minimal SDL2 API headers for Banana OS port.
 * Standard types, constants, and function declarations.
 */
#ifndef SDL_H
#define SDL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t   Sint8;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;

typedef enum {
    SDL_FALSE = 0,
    SDL_TRUE = 1
} SDL_bool;

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_EVERYTHING     0x0000FFFFu

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);

const char* SDL_GetError(void);
void SDL_SetError(const char* fmt, ...);
void SDL_ClearError(void);

/* Video & Windowing */
typedef struct SDL_Window SDL_Window;

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000u
#define SDL_WINDOWPOS_CENTERED  0x2FFF0000u

typedef enum {
    SDL_WINDOW_FULLSCREEN = 0x00000001,
    SDL_WINDOW_OPENGL = 0x00000002,
    SDL_WINDOW_SHOWN = 0x00000004,
    SDL_WINDOW_HIDDEN = 0x00000008,
    SDL_WINDOW_BORDERLESS = 0x00000010,
    SDL_WINDOW_RESIZABLE = 0x00000020,
    SDL_WINDOW_MINIMIZED = 0x00000040,
    SDL_WINDOW_MAXIMIZED = 0x00000080,
    SDL_WINDOW_INPUT_GRABBED = 0x00000100,
    SDL_WINDOW_INPUT_FOCUS = 0x00000200,
    SDL_WINDOW_MOUSE_FOCUS = 0x00000400,
    SDL_WINDOW_FOREIGN = 0x00000800
} SDL_WindowFlags;

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

typedef struct SDL_Point {
    int x;
    int y;
} SDL_Point;

typedef struct SDL_Color {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} SDL_Color;

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color* colors;
    Uint32 version;
    int refcount;
} SDL_Palette;

#define SDL_PIXELFORMAT_UNKNOWN      0
#define SDL_PIXELFORMAT_RGB888       0x16161804u
#define SDL_PIXELFORMAT_ARGB8888     0x16362004u
#define SDL_PIXELFORMAT_RGBA8888     0x16462004u

typedef struct SDL_PixelFormat {
    Uint32 format;
    SDL_Palette* palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 padding[2];
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
    Uint8 Rloss;
    Uint8 Gloss;
    Uint8 Bloss;
    Uint8 Aloss;
    Uint8 Rshift;
    Uint8 Gshift;
    Uint8 Bshift;
    Uint8 Ashift;
    int refcount;
    struct SDL_PixelFormat* next;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat* format;
    int w, h;
    int pitch;
    void* pixels;
    void* userdata;
    int locked;
    void* list_blit;
    SDL_Rect clip_rect;
    void* map;
    int refcount;
} SDL_Surface;

SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window* window);
Uint32 SDL_GetWindowID(SDL_Window* window);
SDL_Window* SDL_GetWindowFromID(Uint32 id);
Uint32 SDL_GetWindowFlags(SDL_Window* window);
void SDL_SetWindowTitle(SDL_Window* window, const char* title);
const char* SDL_GetWindowTitle(SDL_Window* window);
void SDL_SetWindowSize(SDL_Window* window, int w, int h);
void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
void SDL_ShowWindow(SDL_Window* window);
void SDL_HideWindow(SDL_Window* window);

SDL_Surface* SDL_GetWindowSurface(SDL_Window* window);
int SDL_UpdateWindowSurface(SDL_Window* window);
int SDL_UpdateWindowSurfaceRects(SDL_Window* window, const SDL_Rect* rects, int numrects);

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface* surface);
int SDL_FillRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color);
int SDL_BlitSurface(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, SDL_Rect* dstrect);

/* Mouse & Keyboard */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,
    SDL_SCANCODE_B = 5,
    SDL_SCANCODE_C = 6,
    SDL_SCANCODE_D = 7,
    SDL_SCANCODE_E = 8,
    SDL_SCANCODE_F = 9,
    SDL_SCANCODE_G = 10,
    SDL_SCANCODE_H = 11,
    SDL_SCANCODE_I = 12,
    SDL_SCANCODE_J = 13,
    SDL_SCANCODE_K = 14,
    SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16,
    SDL_SCANCODE_N = 17,
    SDL_SCANCODE_O = 18,
    SDL_SCANCODE_P = 19,
    SDL_SCANCODE_Q = 20,
    SDL_SCANCODE_R = 21,
    SDL_SCANCODE_S = 22,
    SDL_SCANCODE_T = 23,
    SDL_SCANCODE_U = 24,
    SDL_SCANCODE_V = 25,
    SDL_SCANCODE_W = 26,
    SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28,
    SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_1 = 30,
    SDL_SCANCODE_2 = 31,
    SDL_SCANCODE_3 = 32,
    SDL_SCANCODE_4 = 33,
    SDL_SCANCODE_5 = 34,
    SDL_SCANCODE_6 = 35,
    SDL_SCANCODE_7 = 36,
    SDL_SCANCODE_8 = 37,
    SDL_SCANCODE_9 = 38,
    SDL_SCANCODE_0 = 39,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226,
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef Sint32 SDL_Keycode;

#define SDLK_UNKNOWN   0
#define SDLK_RETURN    '\r'
#define SDLK_ESCAPE    '\033'
#define SDLK_BACKSPACE '\b'
#define SDLK_TAB       '\t'
#define SDLK_SPACE     ' '
#define SDLK_a         'a'
#define SDLK_b         'b'
#define SDLK_c         'c'
#define SDLK_d         'd'
#define SDLK_e         'e'
#define SDLK_f         'f'
#define SDLK_g         'g'
#define SDLK_h         'h'
#define SDLK_i         'i'
#define SDLK_j         'j'
#define SDLK_k         'k'
#define SDLK_l         'l'
#define SDLK_m         'm'
#define SDLK_n         'n'
#define SDLK_o         'o'
#define SDLK_p         'p'
#define SDLK_q         'q'
#define SDLK_r         'r'
#define SDLK_s         's'
#define SDLK_t         't'
#define SDLK_u         'u'
#define SDLK_v         'v'
#define SDLK_w         'w'
#define SDLK_x         'x'
#define SDLK_y         'y'
#define SDLK_z         'z'
#define SDLK_UP        (1 << 30 | 82)
#define SDLK_DOWN      (1 << 30 | 81)
#define SDLK_LEFT      (1 << 30 | 80)
#define SDLK_RIGHT     (1 << 30 | 79)

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

#define SDL_RELEASED 0
#define SDL_PRESSED  1

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_LMASK  (1 << (SDL_BUTTON_LEFT - 1))
#define SDL_BUTTON_MMASK  (1 << (SDL_BUTTON_MIDDLE - 1))
#define SDL_BUTTON_RMASK  (1 << (SDL_BUTTON_RIGHT - 1))

int SDL_SetRelativeMouseMode(SDL_bool enabled);
SDL_bool SDL_GetRelativeMouseMode(void);
Uint32 SDL_GetMouseState(int* x, int* y);
Uint32 SDL_GetRelativeMouseState(int* x, int* y);

/* Events */
typedef enum {
    SDL_FIRSTEVENT     = 0,
    SDL_QUIT           = 0x100,
    SDL_WINDOWEVENT    = 0x200,
    SDL_KEYDOWN        = 0x300,
    SDL_KEYUP          = 0x301,
    SDL_TEXTINPUT      = 0x303,
    SDL_MOUSEMOTION    = 0x400,
    SDL_MOUSEBUTTONDOWN= 0x401,
    SDL_MOUSEBUTTONUP  = 0x402,
    SDL_MOUSEWHEEL     = 0x403,
    SDL_LASTEVENT      = 0xFFFF
} SDL_EventType;

typedef enum {
    SDL_WINDOWEVENT_NONE,
    SDL_WINDOWEVENT_SHOWN,
    SDL_WINDOWEVENT_HIDDEN,
    SDL_WINDOWEVENT_EXPOSED,
    SDL_WINDOWEVENT_MOVED,
    SDL_WINDOWEVENT_RESIZED,
    SDL_WINDOWEVENT_SIZE_CHANGED,
    SDL_WINDOWEVENT_MINIMIZED,
    SDL_WINDOWEVENT_MAXIMIZED,
    SDL_WINDOWEVENT_RESTORED,
    SDL_WINDOWEVENT_ENTER,
    SDL_WINDOWEVENT_LEAVE,
    SDL_WINDOWEVENT_FOCUS_GAINED,
    SDL_WINDOWEVENT_FOCUS_LOST,
    SDL_WINDOWEVENT_CLOSE
} SDL_WindowEventID;

typedef struct SDL_CommonEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_CommonEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint32 data1;
    Sint32 data2;
} SDL_WindowEvent;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x;
    Sint32 y;
    Sint32 xrel;
    Sint32 yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    Sint32 x;
    Sint32 y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x;
    Sint32 y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_QuitEvent quit;
    Uint8 padding[56];
} SDL_Event;

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event* event);
int SDL_WaitEvent(SDL_Event* event);
int SDL_WaitEventTimeout(SDL_Event* event, int timeout);

/* Time */
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
void SDL_Delay(Uint32 ms);

/* Audio */
typedef Uint16 SDL_AudioFormat;
#define AUDIO_U8     0x0008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

typedef void (*SDL_AudioCallback)(void* userdata, Uint8* stream, int len);

typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    SDL_AudioCallback callback;
    void* userdata;
} SDL_AudioSpec;

typedef Uint32 SDL_AudioDeviceID;

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_H */
