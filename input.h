#pragma once
#include <stdbool.h>

struct GLFWwindow;

/*======================================================
=                    CONSTANTS                         =
======================================================*/

#define INPUT_MAX_KEYS        512
#define INPUT_MAX_MOUSE_BTN   8

/*======================================================
=                   KEY CODES (BACKEND-AGNOSTIC)       =
======================================================*/

typedef enum
{
    /* Letters */
    INPUT_KEY_A = 65,
    INPUT_KEY_B = 66,
    INPUT_KEY_C = 67,
    INPUT_KEY_D = 68,
    INPUT_KEY_E = 69,
    INPUT_KEY_F = 70,
    INPUT_KEY_G = 71,
    INPUT_KEY_H = 72,
    INPUT_KEY_I = 73,
    INPUT_KEY_J = 74,
    INPUT_KEY_K = 75,
    INPUT_KEY_L = 76,
    INPUT_KEY_M = 77,
    INPUT_KEY_N = 78,
    INPUT_KEY_O = 79,
    INPUT_KEY_P = 80,
    INPUT_KEY_Q = 81,
    INPUT_KEY_R = 82,
    INPUT_KEY_S = 83,
    INPUT_KEY_T = 84,
    INPUT_KEY_U = 85,
    INPUT_KEY_V = 86,
    INPUT_KEY_W = 87,
    INPUT_KEY_X = 88,
    INPUT_KEY_Y = 89,
    INPUT_KEY_Z = 90,

    /* Numbers */
    INPUT_KEY_0 = 48,
    INPUT_KEY_1 = 49,
    INPUT_KEY_2 = 50,
    INPUT_KEY_3 = 51,
    INPUT_KEY_4 = 52,
    INPUT_KEY_5 = 53,
    INPUT_KEY_6 = 54,
    INPUT_KEY_7 = 55,
    INPUT_KEY_8 = 56,
    INPUT_KEY_9 = 57,

    /* Special */
    INPUT_KEY_SPACE         = 32,
    INPUT_KEY_APOSTROPHE    = 39,
    INPUT_KEY_COMMA         = 44,
    INPUT_KEY_MINUS         = 45,
    INPUT_KEY_PERIOD        = 46,
    INPUT_KEY_SLASH         = 47,
    INPUT_KEY_SEMICOLON     = 59,
    INPUT_KEY_EQUAL         = 61,
    INPUT_KEY_LEFT_BRACKET  = 91,
    INPUT_KEY_BACKSLASH     = 92,
    INPUT_KEY_RIGHT_BRACKET = 93,
    INPUT_KEY_GRAVE_ACCENT  = 96,

    /* Function keys */
    INPUT_KEY_ESCAPE        = 256,
    INPUT_KEY_ENTER         = 257,
    INPUT_KEY_TAB           = 258,
    INPUT_KEY_BACKSPACE     = 259,
    INPUT_KEY_INSERT        = 260,
    INPUT_KEY_DELETE        = 261,
    INPUT_KEY_RIGHT         = 262,
    INPUT_KEY_LEFT          = 263,
    INPUT_KEY_DOWN          = 264,
    INPUT_KEY_UP            = 265,
    INPUT_KEY_PAGE_UP       = 266,
    INPUT_KEY_PAGE_DOWN     = 267,
    INPUT_KEY_HOME          = 268,
    INPUT_KEY_END           = 269,

    /* Modifiers */
    INPUT_KEY_LCTRL         = 341,
    INPUT_KEY_LSHIFT        = 340,
    INPUT_KEY_LALT          = 342,
    INPUT_KEY_LSUPER        = 343,
    INPUT_KEY_RCTRL         = 345,
    INPUT_KEY_RSHIFT        = 344,
    INPUT_KEY_RALT          = 346,
    INPUT_KEY_RSUPER        = 347,

} InputKeyCode;

typedef int KeyCode;

/*======================================================
=                  BUTTON STATE                        =
======================================================*/

typedef struct
{
    bool curr;
    bool prev;
} ButtonState;

/*
    Frame model:

        prev ← last frame
        curr ← this frame

    pressed  = curr && !prev
    released = !curr && prev
*/

/*======================================================
=                   MOUSE STATE                        =
======================================================*/

typedef struct
{
    ButtonState buttons[INPUT_MAX_MOUSE_BTN];

    double x, y;     // absolute position
    double dx, dy;   // delta since last frame

    double scroll_x;
    double scroll_y;

} MouseState;

/*
    ASCII:

        last (x,y)
           │
           ▼
        current (x,y)
           │
           ▼
        delta (dx,dy)
*/

/*======================================================
=                   INPUT STATE                        =
======================================================*/

typedef struct
{
    ButtonState keys[INPUT_MAX_KEYS];
    MouseState  mouse;

    double last_mouse_x;
    double last_mouse_y;

    bool   mouse_initialized;

} InputState;

/*
    PURE STATE CONTAINER

    No actions
    No gameplay
    No interpretation
*/

/*======================================================
=                  CORE FUNCTIONS                      =
======================================================*/

void input_init(InputState* in);
void input_attach(InputState* in, struct GLFWwindow* window);
void input_update(InputState* in, struct GLFWwindow* window);

/*
    Flow:

        OS → GLFW → InputState

    Each frame:
        prev = curr
        curr = new
*/

/*======================================================
=                  KEY QUERIES                         =
======================================================*/

bool input_key_down(const InputState* in, KeyCode key);

bool input_key_pressed(const InputState* in, KeyCode key);

bool input_key_released(const InputState* in, KeyCode key);

/*
    ASCII:

        prev  curr   result
        --------------------
        0     1      pressed
        1     1      down
        1     0      released
*/

/*======================================================
=                 MOUSE QUERIES                        =
======================================================*/

bool input_mouse_down(const InputState* in, int button);

bool input_mouse_pressed(const InputState* in, int button);

bool input_mouse_released(const InputState* in, int button);

double input_mouse_x(const InputState* in);
double input_mouse_y(const InputState* in);

double input_mouse_dx(const InputState* in);
double input_mouse_dy(const InputState* in);

double input_scroll_x(const InputState* in);
double input_scroll_y(const InputState* in);

/*======================================================
=                OPTIONAL HELPERS                      =
======================================================*/

float input_axis_from_keys(
    const InputState* in,
    KeyCode negative,
    KeyCode positive
);

/*
    Converts digital → analog

    Example:

        input_axis_from_keys(&input, INPUT_KEY_A, INPUT_KEY_D)

        INPUT_KEY_A = -1
        INPUT_KEY_D = +1

    ASCII:

       -1        0        +1
      LEFT    CENTER    RIGHT
*/
