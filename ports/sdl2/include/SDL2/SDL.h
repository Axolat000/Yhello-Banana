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

/* OpenGL Context & Attributes */
typedef void* SDL_GLContext;

typedef enum {
    SDL_GL_RED_SIZE,
    SDL_GL_GREEN_SIZE,
    SDL_GL_BLUE_SIZE,
    SDL_GL_ALPHA_SIZE,
    SDL_GL_BUFFER_SIZE,
    SDL_GL_DOUBLEBUFFER,
    SDL_GL_DEPTH_SIZE,
    SDL_GL_STENCIL_SIZE,
    SDL_GL_ACCUM_RED_SIZE,
    SDL_GL_ACCUM_GREEN_SIZE,
    SDL_GL_ACCUM_BLUE_SIZE,
    SDL_GL_ACCUM_ALPHA_SIZE,
    SDL_GL_STEREO,
    SDL_GL_MULTISAMPLEBUFFERS,
    SDL_GL_MULTISAMPLESAMPLES,
    SDL_GL_ACCELERATED_VISUAL,
    SDL_GL_RETAINED_BACKING,
    SDL_GL_CONTEXT_MAJOR_VERSION,
    SDL_GL_CONTEXT_MINOR_VERSION,
    SDL_GL_CONTEXT_EGL,
    SDL_GL_CONTEXT_FLAGS,
    SDL_GL_CONTEXT_PROFILE_MASK,
    SDL_GL_SHARE_WITH_CURRENT_CONTEXT,
    SDL_GL_FRAMEBUFFER_SRGB_CAPABLE,
    SDL_GL_CONTEXT_RELEASE_BEHAVIOR,
    SDL_GL_CONTEXT_RESET_NOTIFICATION,
    SDL_GL_CONTEXT_NO_ERROR
} SDL_GLattr;

typedef enum {
    SDL_GL_CONTEXT_PROFILE_CORE           = 0x0001,
    SDL_GL_CONTEXT_PROFILE_COMPATIBILITY  = 0x0002,
    SDL_GL_CONTEXT_PROFILE_ES             = 0x0004
} SDL_GLprofile;

typedef enum {
    SDL_GL_CONTEXT_DEBUG_FLAG              = 0x0001,
    SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG = 0x0002,
    SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG      = 0x0004,
    SDL_GL_CONTEXT_RESET_ISOLATION_FLAG    = 0x0008
} SDL_GLcontextFlag;

int SDL_GL_SetAttribute(SDL_GLattr attr, int value);
int SDL_GL_GetAttribute(SDL_GLattr attr, int* value);
void SDL_GL_ResetAttributes(void);

SDL_GLContext SDL_GL_CreateContext(SDL_Window* window);
int SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext context);
SDL_GLContext SDL_GL_GetCurrentContext(void);
SDL_Window* SDL_GL_GetCurrentWindow(void);
void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h);
int SDL_GL_SetSwapInterval(int interval);
int SDL_GL_GetSwapInterval(void);
void SDL_GL_SwapWindow(SDL_Window* window);
void SDL_GL_DeleteContext(SDL_GLContext context);
void* SDL_GL_GetProcAddress(const char* proc);
SDL_bool SDL_GL_ExtensionSupported(const char* extension);


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
    SDL_SCANCODE_F1 = 58,
    SDL_SCANCODE_F2 = 59,
    SDL_SCANCODE_F3 = 60,
    SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62,
    SDL_SCANCODE_F6 = 63,
    SDL_SCANCODE_F7 = 64,
    SDL_SCANCODE_F8 = 65,
    SDL_SCANCODE_F9 = 66,
    SDL_SCANCODE_F10 = 67,
    SDL_SCANCODE_F11 = 68,
    SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_HOME = 74,
    SDL_SCANCODE_PAGEUP = 75,
    SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_END = 77,
    SDL_SCANCODE_PAGEDOWN = 78,
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
#define SDLK_HOME      (1 << 30 | 74)
#define SDLK_END       (1 << 30 | 77)
#define SDLK_PAGEUP    (1 << 30 | 75)
#define SDLK_PAGEDOWN  (1 << 30 | 78)
#define SDLK_DELETE    (1 << 30 | 76)
#define SDLK_F1        (1 << 30 | 58)
#define SDLK_F2        (1 << 30 | 59)
#define SDLK_F3        (1 << 30 | 60)
#define SDLK_F4        (1 << 30 | 61)
#define SDLK_F5        (1 << 30 | 62)
#define SDLK_F6        (1 << 30 | 63)
#define SDLK_F7        (1 << 30 | 64)
#define SDLK_F8        (1 << 30 | 65)
#define SDLK_F9        (1 << 30 | 66)
#define SDLK_F10       (1 << 30 | 67)
#define SDLK_F11       (1 << 30 | 68)
#define SDLK_F12       (1 << 30 | 69)

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
    SDL_FIRSTEVENT          = 0,
    SDL_QUIT                = 0x100,
    SDL_WINDOWEVENT         = 0x200,
    SDL_KEYDOWN             = 0x300,
    SDL_KEYUP               = 0x301,
    SDL_TEXTINPUT           = 0x303,
    SDL_MOUSEMOTION         = 0x400,
    SDL_MOUSEBUTTONDOWN     = 0x401,
    SDL_MOUSEBUTTONUP       = 0x402,
    SDL_MOUSEWHEEL          = 0x403,
    SDL_JOYAXISMOTION       = 0x600,
    SDL_JOYBALLMOTION       = 0x601,
    SDL_JOYHATMOTION        = 0x602,
    SDL_JOYBUTTONDOWN       = 0x603,
    SDL_JOYBUTTONUP         = 0x604,
    SDL_JOYDEVICEADDED      = 0x605,
    SDL_JOYDEVICEREMOVED    = 0x606,
    SDL_CONTROLLERAXISMOTION= 0x650,
    SDL_CONTROLLERBUTTONDOWN= 0x651,
    SDL_CONTROLLERBUTTONUP  = 0x652,
    SDL_CONTROLLERDEVICEADDED = 0x653,
    SDL_CONTROLLERDEVICEREMOVED = 0x654,
    SDL_LASTEVENT           = 0xFFFF
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

typedef struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef Sint32 SDL_JoystickID;

typedef struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_JoyHatEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyHatEvent;

typedef struct SDL_ControllerAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_ControllerAxisEvent;

typedef struct SDL_ControllerButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_ControllerButtonEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyHatEvent jhat;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_QuitEvent quit;
    Uint8 padding[56];
} SDL_Event;

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event* event);
int SDL_WaitEvent(SDL_Event* event);
int SDL_WaitEventTimeout(SDL_Event* event, int timeout);
int SDL_PushEvent(SDL_Event* event);

/* Time & Performance Counter */
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);

/* Threads & Synchronization */
typedef struct SDL_Thread SDL_Thread;
typedef unsigned long SDL_threadID;
typedef int (*SDL_ThreadFunction)(void* data);

typedef enum {
    SDL_THREAD_PRIORITY_LOW,
    SDL_THREAD_PRIORITY_NORMAL,
    SDL_THREAD_PRIORITY_HIGH,
    SDL_THREAD_PRIORITY_TIME_CRITICAL
} SDL_ThreadPriority;

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data);
SDL_threadID SDL_GetThreadID(SDL_Thread* thread);
SDL_threadID SDL_ThreadID(void);
int SDL_SetThreadPriority(SDL_ThreadPriority priority);
void SDL_WaitThread(SDL_Thread* thread, int* status);
void SDL_DetachThread(SDL_Thread* thread);

typedef unsigned int SDL_TLSID;
SDL_TLSID SDL_TLSCreate(void);
void* SDL_TLSGet(SDL_TLSID id);
int SDL_TLSSet(SDL_TLSID id, const void* value, void (*destructor)(void*));

typedef struct SDL_mutex SDL_mutex;
SDL_mutex* SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex* mutex);
int SDL_LockMutex(SDL_mutex* mutex);
int SDL_TryLockMutex(SDL_mutex* mutex);
int SDL_UnlockMutex(SDL_mutex* mutex);

#define SDL_mutexP(m) SDL_LockMutex(m)
#define SDL_mutexV(m) SDL_UnlockMutex(m)

typedef struct SDL_sem SDL_sem;
SDL_sem* SDL_CreateSemaphore(Uint32 initial_value);
void SDL_DestroySemaphore(SDL_sem* sem);
int SDL_SemWait(SDL_sem* sem);
int SDL_SemTryWait(SDL_sem* sem);
int SDL_SemWaitTimeout(SDL_sem* sem, Uint32 ms);
int SDL_SemPost(SDL_sem* sem);
Uint32 SDL_SemValue(SDL_sem* sem);

typedef struct SDL_cond SDL_cond;
SDL_cond* SDL_CreateCond(void);
void SDL_DestroyCond(SDL_cond* cond);
int SDL_CondSignal(SDL_cond* cond);
int SDL_CondBroadcast(SDL_cond* cond);
int SDL_CondWait(SDL_cond* cond, SDL_mutex* mutex);
int SDL_CondWaitTimeout(SDL_cond* cond, SDL_mutex* mutex, Uint32 ms);

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
SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device, int iscapture,
                                      const SDL_AudioSpec* desired,
                                      SDL_AudioSpec* obtained,
                                      int allowed_changes);
void SDL_CloseAudio(void);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudio(int pause_on);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_LockAudio(void);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudio(void);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);

/* Joystick & Game Controller */
typedef struct _SDL_Joystick SDL_Joystick;

#define SDL_HAT_CENTERED  0x00
#define SDL_HAT_UP        0x01
#define SDL_HAT_RIGHT     0x02
#define SDL_HAT_DOWN      0x04
#define SDL_HAT_LEFT      0x08
#define SDL_HAT_RIGHTUP   (SDL_HAT_RIGHT|SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT|SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP    (SDL_HAT_LEFT|SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN  (SDL_HAT_LEFT|SDL_HAT_DOWN)

int SDL_NumJoysticks(void);
SDL_Joystick* SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick* joystick);
const char* SDL_JoystickName(SDL_Joystick* joystick);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* joystick);
int SDL_JoystickNumAxes(SDL_Joystick* joystick);
int SDL_JoystickNumButtons(SDL_Joystick* joystick);
int SDL_JoystickNumHats(SDL_Joystick* joystick);
Sint16 SDL_JoystickGetAxis(SDL_Joystick* joystick, int axis);
Uint8 SDL_JoystickGetButton(SDL_Joystick* joystick, int button);
Uint8 SDL_JoystickGetHat(SDL_Joystick* joystick, int hat);
void SDL_JoystickUpdate(void);

typedef struct _SDL_GameController SDL_GameController;

typedef enum {
    SDL_CONTROLLER_BINDTYPE_NONE = 0,
    SDL_CONTROLLER_BINDTYPE_BUTTON,
    SDL_CONTROLLER_BINDTYPE_AXIS,
    SDL_CONTROLLER_BINDTYPE_HAT
} SDL_GameControllerBindType;

typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX,
    SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

SDL_bool SDL_IsGameController(int joystick_index);
SDL_GameController* SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController* gamecontroller);
const char* SDL_GameControllerName(SDL_GameController* gamecontroller);
SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* gamecontroller);
Sint16 SDL_GameControllerGetAxis(SDL_GameController* gamecontroller, SDL_GameControllerAxis axis);
Uint8 SDL_GameControllerGetButton(SDL_GameController* gamecontroller, SDL_GameControllerButton button);
void SDL_GameControllerUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_H */
