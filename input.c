#include "input.h"
#include <GLFW/glfw3.h>
#include <string.h>

/*======================================================
=              INTERNAL STATE MANAGEMENT               =
======================================================*/

static InputState* g_input_state = NULL;

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)window;
    
    if (g_input_state)
    {
        g_input_state->mouse.scroll_x = xoffset;
        g_input_state->mouse.scroll_y = yoffset;
    }
}

/*======================================================
=                   CORE FUNCTIONS                     =
======================================================*/

void input_init(InputState* in)
{
    if (!in)
        return;

    memset(in, 0, sizeof(*in));
    in->mouse_initialized = false;
    g_input_state = in;
}

void input_attach(InputState* in, struct GLFWwindow* window)
{
    if (!in || !window)
        return;

    g_input_state = in;
    glfwSetScrollCallback(window, scroll_callback);
}

void input_update(InputState* in, struct GLFWwindow* window)
{
    if (!in || !window)
        return;

    /*
        Phase 1: Slide current → previous
    */
    for (int i = 0; i < INPUT_MAX_KEYS; i++)
    {
        in->keys[i].prev = in->keys[i].curr;
    }

    for (int i = 0; i < INPUT_MAX_MOUSE_BTN; i++)
    {
        in->mouse.buttons[i].prev = in->mouse.buttons[i].curr;
    }

    /*
        Phase 2: Poll current state from OS
    */
    for (int i = 0; i < INPUT_MAX_KEYS; i++)
    {
        int state = glfwGetKey(window, i);
        in->keys[i].curr = (state == GLFW_PRESS);
    }

    for (int i = 0; i < INPUT_MAX_MOUSE_BTN; i++)
    {
        int state = glfwGetMouseButton(window, i);
        in->mouse.buttons[i].curr = (state == GLFW_PRESS);
    }

    /*
        Phase 3: Update mouse position and delta
    */
    double new_x, new_y;
    glfwGetCursorPos(window, &new_x, &new_y);

    if (!in->mouse_initialized)
    {
        in->last_mouse_x = new_x;
        in->last_mouse_y = new_y;
        in->mouse_initialized = true;
    }

    in->mouse.x = new_x;
    in->mouse.y = new_y;
    in->mouse.dx = new_x - in->last_mouse_x;
    in->mouse.dy = new_y - in->last_mouse_y;

    in->last_mouse_x = new_x;
    in->last_mouse_y = new_y;

    /*
        Phase 4: Clear scroll (one-frame only)
        
        Note: This happens AFTER scroll_callback fires.
        If you need multi-frame scroll, structure differently.
    */
    in->mouse.scroll_x = 0.0;
    in->mouse.scroll_y = 0.0;
}

/*======================================================
=                  KEY QUERIES                         =
======================================================*/

bool input_key_down(const InputState* in, KeyCode key)
{
    if (!in || key < 0 || key >= INPUT_MAX_KEYS)
        return false;

    return in->keys[key].curr;
}

bool input_key_pressed(const InputState* in, KeyCode key)
{
    if (!in || key < 0 || key >= INPUT_MAX_KEYS)
        return false;

    return in->keys[key].curr && !in->keys[key].prev;
}

bool input_key_released(const InputState* in, KeyCode key)
{
    if (!in || key < 0 || key >= INPUT_MAX_KEYS)
        return false;

    return !in->keys[key].curr && in->keys[key].prev;
}

/*======================================================
=                 MOUSE QUERIES                        =
======================================================*/

bool input_mouse_down(const InputState* in, int button)
{
    if (!in || button < 0 || button >= INPUT_MAX_MOUSE_BTN)
        return false;

    return in->mouse.buttons[button].curr;
}

bool input_mouse_pressed(const InputState* in, int button)
{
    if (!in || button < 0 || button >= INPUT_MAX_MOUSE_BTN)
        return false;

    return in->mouse.buttons[button].curr && !in->mouse.buttons[button].prev;
}

bool input_mouse_released(const InputState* in, int button)
{
    if (!in || button < 0 || button >= INPUT_MAX_MOUSE_BTN)
        return false;

    return !in->mouse.buttons[button].curr && in->mouse.buttons[button].prev;
}

double input_mouse_x(const InputState* in)
{
    return in ? in->mouse.x : 0.0;
}

double input_mouse_y(const InputState* in)
{
    return in ? in->mouse.y : 0.0;
}

double input_mouse_dx(const InputState* in)
{
    return in ? in->mouse.dx : 0.0;
}

double input_mouse_dy(const InputState* in)
{
    return in ? in->mouse.dy : 0.0;
}

double input_scroll_x(const InputState* in)
{
    return in ? in->mouse.scroll_x : 0.0;
}

double input_scroll_y(const InputState* in)
{
    return in ? in->mouse.scroll_y : 0.0;
}

/*======================================================
=                OPTIONAL HELPERS                      =
======================================================*/

float input_axis_from_keys(
    const InputState* in,
    KeyCode negative,
    KeyCode positive)
{
    if (!in)
        return 0.0f;

    float result = 0.0f;

    if (input_key_down(in, negative))
        result -= 1.0f;

    if (input_key_down(in, positive))
        result += 1.0f;

    return result;
}
