/*
* File     : ofumo.c
* Platform : Linux / X11 / KDE Plasma Wayland
* Author   : Daniel Fridman (Schermaiolo)
*
* OFumo runtime, oneko-derived movement state machine, sprite animation logic,
* X11/XShape rendering, command-line handling, and backend selection.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "koishi_idle_xbm.h"
#include "koishi_wink_xbm.h"
#include "koishi_left_xbm.h"
#include "koishi_right_xbm.h"
#include "koishi_up_xbm.h"
#include "koishi_sleep0_xbm.h"
#include "koishi_sleep1_xbm.h"
#include "koishi_sleep2_xbm.h"
#include "koishi_hide_hat_xbm.h"
#include "kwin_bridge.h"

#ifndef OFUMO_VERSION
#define OFUMO_VERSION "dev"
#endif

#define MAX_TICK 9999
#define DEFAULT_INTERVAL_US 125000L
#define DEFAULT_SPEED 13.0
#define DEFAULT_IDLE 6

#define STOP_TIME 4
#define JARE_TIME 10
#define KAKI_TIME 4
#define AKUBI_TIME 6
#define AWAKE_TIME 3
#define TOGI_TIME 10
#define DEFAULT_RAISE_WAIT 16
#define PI_PER_8 (3.14159265358979323846 / 8.0)

#define OFUMO_WIDTH KOISHI_IDLE_WIDTH
#define OFUMO_HEIGHT KOISHI_IDLE_HEIGHT

/** @brief Internal movement and idle states derived from classic oneko. */
typedef enum FumoState {
    FUMO_STOP = 0,
    FUMO_JARE,
    FUMO_KAKI,
    FUMO_AKUBI,
    FUMO_SLEEP,
    FUMO_AWAKE,
    FUMO_U_MOVE,
    FUMO_D_MOVE,
    FUMO_L_MOVE,
    FUMO_R_MOVE,
    FUMO_UL_MOVE,
    FUMO_UR_MOVE,
    FUMO_DL_MOVE,
    FUMO_DR_MOVE,
    FUMO_U_TOGI,
    FUMO_D_TOGI,
    FUMO_L_TOGI,
    FUMO_R_TOGI
} FumoState;

/** @brief Cursor/window information backend selected at startup. */
typedef enum BackendMode {
    BACKEND_AUTO = 0,
    BACKEND_X11,
    BACKEND_KWIN
} BackendMode;

/** @brief Koishi sprite identifiers used by the renderer. */
typedef enum SpriteId {
    SPRITE_IDLE = 0,
    SPRITE_WINK,
    SPRITE_LEFT,
    SPRITE_RIGHT,
    SPRITE_UP,
    SPRITE_SLEEP0,
    SPRITE_SLEEP1,
    SPRITE_SLEEP2,
    SPRITE_HIDE_HAT,
    SPRITE_COUNT
} SpriteId;

/** @brief Short OFumo-specific animations layered over normal movement. */
typedef enum SpecialAnim {
    SPECIAL_NONE = 0,
    SPECIAL_SPAWN,
    SPECIAL_CLICK,
    SPECIAL_SPIN
} SpecialAnim;

/** @brief Generated bitmap and mask pair for one runtime sprite. */
typedef struct SpriteDef {
    const unsigned char *bits;
    const unsigned char *mask_bits;
    unsigned int width;
    unsigned int height;
} SpriteDef;

static const SpriteDef g_sprite_defs[SPRITE_COUNT] = {
    [SPRITE_IDLE] = { KOISHI_IDLE_BITS, KOISHI_IDLE_MASK_BITS, KOISHI_IDLE_WIDTH, KOISHI_IDLE_HEIGHT },
    [SPRITE_WINK] = { KOISHI_WINK_BITS, KOISHI_WINK_MASK_BITS, KOISHI_WINK_WIDTH, KOISHI_WINK_HEIGHT },
    [SPRITE_LEFT] = { KOISHI_LEFT_BITS, KOISHI_LEFT_MASK_BITS, KOISHI_LEFT_WIDTH, KOISHI_LEFT_HEIGHT },
    [SPRITE_RIGHT] = { KOISHI_RIGHT_BITS, KOISHI_RIGHT_MASK_BITS, KOISHI_RIGHT_WIDTH, KOISHI_RIGHT_HEIGHT },
    [SPRITE_UP] = { KOISHI_UP_BITS, KOISHI_UP_MASK_BITS, KOISHI_UP_WIDTH, KOISHI_UP_HEIGHT },
    [SPRITE_SLEEP0] = { KOISHI_SLEEP0_BITS, KOISHI_SLEEP0_MASK_BITS, KOISHI_SLEEP0_WIDTH, KOISHI_SLEEP0_HEIGHT },
    [SPRITE_SLEEP1] = { KOISHI_SLEEP1_BITS, KOISHI_SLEEP1_MASK_BITS, KOISHI_SLEEP1_WIDTH, KOISHI_SLEEP1_HEIGHT },
    [SPRITE_SLEEP2] = { KOISHI_SLEEP2_BITS, KOISHI_SLEEP2_MASK_BITS, KOISHI_SLEEP2_WIDTH, KOISHI_SLEEP2_HEIGHT },
    [SPRITE_HIDE_HAT] = { KOISHI_HIDE_HAT_BITS, KOISHI_HIDE_HAT_MASK_BITS, KOISHI_HIDE_HAT_WIDTH, KOISHI_HIDE_HAT_HEIGHT },
};

/** @brief Parsed command-line configuration. */
typedef struct Options {
    double speed;
    long interval_us;
    int idle_space;
    int x_offset;
    int y_offset;
    bool to_focus;
    bool to_window;
    const char *target_name;
    const char *window_name;
    const char *display_name;
    const char *foreground;
    const char *background;
    bool reverse_video;
    bool no_shape;
    bool debug_sync;
    int restore_cursor;
    bool verbose;
    BackendMode backend;
} Options;

/** @brief Complete runtime state for one OFumo process. */
typedef struct App {
    Display *display;
    int screen;
    unsigned int depth;
    Window root;
    Window window;
    Window target;
    Window prev_target;
    unsigned int screen_width;
    unsigned int screen_height;

    Pixmap sprites[SPRITE_COUNT];
    Pixmap masks[SPRITE_COUNT];
    GC gc;
    unsigned long foreground_pixel;
    unsigned long background_pixel;
    bool shape_enabled;
    int restore_cursor;

    int mouse_x;
    int mouse_y;
    int prev_mouse_x;
    int prev_mouse_y;

    int fumo_x;
    int fumo_y;
    int last_x;
    int last_y;
    int move_dx;
    int move_dy;

    int tick_count;
    int state_count;
    FumoState state;
    SpriteId current_sprite;
    SpriteId last_draw_sprite;
    int raise_window_delay;
    bool mapped;

    bool target_lost;

    bool use_kwin_bridge;
    KWinBridge kwin;
    uint64_t bridge_active_seen;
    bool bridge_target_changed;

    SpecialAnim special_anim;
    int special_tick;

    uint64_t frame_counter;
    uint64_t next_wink_frame;
    uint64_t next_spin_frame;
    int wink_ticks_remaining;
    long interval_us;
} App;

static volatile sig_atomic_t g_running = 1;

/** @brief Request a clean shutdown after SIGINT or SIGTERM. */
static void handle_stop(int signum) { (void)signum; g_running = 0; }
/** @brief Empty SIGALRM handler used to wake the timer-driven loop. */
static void handle_alarm(int signum) { (void)signum; }

/**
 * @brief Return a diagnostic name for a movement state.
 * @param state State to describe.
 * @return Static state-name string.
 */
static const char *state_name(FumoState state)
{
    switch (state) {
    case FUMO_STOP: return "stop";
    case FUMO_JARE: return "jare";
    case FUMO_KAKI: return "kaki";
    case FUMO_AKUBI: return "akubi";
    case FUMO_SLEEP: return "sleep";
    case FUMO_AWAKE: return "awake";
    case FUMO_U_MOVE: return "up";
    case FUMO_D_MOVE: return "down";
    case FUMO_L_MOVE: return "left";
    case FUMO_R_MOVE: return "right";
    case FUMO_UL_MOVE: return "up-left";
    case FUMO_UR_MOVE: return "up-right";
    case FUMO_DL_MOVE: return "down-left";
    case FUMO_DR_MOVE: return "down-right";
    case FUMO_U_TOGI: return "top-edge";
    case FUMO_D_TOGI: return "bottom-edge";
    case FUMO_L_TOGI: return "left-edge";
    case FUMO_R_TOGI: return "right-edge";
    }
    return "unknown";
}

/**
 * @brief Return a diagnostic name for a sprite.
 * @param sprite Sprite identifier.
 * @return Static sprite-name string.
 */
static const char *sprite_name(SpriteId sprite)
{
    switch (sprite) {
    case SPRITE_IDLE: return "idle";
    case SPRITE_WINK: return "wink";
    case SPRITE_LEFT: return "left";
    case SPRITE_RIGHT: return "right";
    case SPRITE_UP: return "up";
    case SPRITE_SLEEP0: return "sleep0";
    case SPRITE_SLEEP1: return "sleep1";
    case SPRITE_SLEEP2: return "sleep2";
    case SPRITE_HIDE_HAT: return "hide-hat";
    case SPRITE_COUNT: break;
    }
    return "?";
}

/**
 * @brief Change movement state and reset its tick counters.
 * @param app Runtime instance.
 * @param state New state.
 * @param verbose Print the transition when true.
 */
static void set_state(App *app, FumoState state, bool verbose)
{
    if (app->state != state) {
        app->tick_count = 0;
        app->state_count = 0;
        app->state = state;
        if (verbose) fprintf(stderr, "ofumo: state -> %s\n", state_name(state));
    }
}

/**
 * @brief Print command-line usage.
 * @param stream Output stream.
 * @param program Executable name.
 */
static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "OFumo - Koishi desktop pet with oneko-compatible controls.\n"
        "\n"
        "oneko-compatible options:\n"
        "  -help, -h, --help        show this help\n"
        "  -display DISPLAY         use the specified X display\n"
        "  -fg COLOR                foreground (black-pixel) color\n"
        "  -bg COLOR                background (white-pixel) color\n"
        "  -speed N                 pixels moved per tick (default: 13)\n"
        "  -time US                 tick interval in microseconds (default: 125000)\n"
        "  -idle N                  pointer movement needed to wake (default: 6)\n"
        "  -name NAME               set OFumo X11 window name\n"
        "  -towindow                click an X11/XWayland window to chase\n"
        "  -toname NAME             chase an X11/XWayland window by title\n"
        "  -tofocus                 run to/on top of the focused window\n"
        "  -position GEOMETRY       offset target relative to pointer/window\n"
        "  -rv                      swap foreground/background colors\n"
        "  -noshape                 do not use the X SHAPE extension\n"
        "  -debug                   use synchronous X11 requests (debugging)\n"
        "  -patchlevel              print OFumo patchlevel/version and exit\n"
        "  -cursor N                set X font cursor N on the root window when quitting\n"
        "  -neko|-tora|-dog|-bsd_daemon|-sakura|-tomoyo\n"
        "                           accepted for oneko CLI compatibility; Koishi is\n"
        "                           currently the only OFumo character\n"
        "\n"
        "OFumo extensions:\n"
        "  --backend MODE           auto (default), x11, or kwin\n"
        "  -v, --verbose            print backend/state diagnostics\n"
        "  --version                print version and exit\n"
        "\n"
        "Long aliases --speed/--time/--idle are also accepted.\n",
        program);
}

/** @brief Parse a strictly positive long integer. */
static bool parse_long_positive(const char *text, long *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0) return false;
    *out = value;
    return true;
}

/** @brief Parse a strictly positive bounded integer. */
static bool parse_int_positive(const char *text, int *out)
{
    long value;
    if (!parse_long_positive(text, &value) || value > 1000000L) return false;
    *out = (int)value;
    return true;
}

/** @brief Parse a non-negative bounded integer. */
static bool parse_int_nonnegative(const char *text, int *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > 1000000L) return false;
    *out = (int)value;
    return true;
}

/** @brief Parse a strictly positive floating-point value. */
static bool parse_double_positive(const char *text, double *out)
{
    char *end = NULL;
    errno = 0;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value <= 0.0 || value > 1000000.0) return false;
    *out = value;
    return true;
}

/** @brief Parse an X geometry string into target offsets. */
static bool parse_geometry_offset(const char *text, int *x, int *y)
{
    int gx = 0, gy = 0;
    unsigned int gw = 0, gh = 0;
    int flags = XParseGeometry(text, &gx, &gy, &gw, &gh);
    if ((flags & XValue) == 0 && (flags & YValue) == 0) return false;
    if ((flags & XValue) != 0) *x = gx;
    if ((flags & YValue) != 0) *y = gy;
    return true;
}

/**
 * @brief Parse oneko-compatible and OFumo-specific command-line options.
 * @return true when all options are valid.
 */
static bool parse_options(int argc, char **argv, Options *options)
{
    *options = (Options) {
        .speed = DEFAULT_SPEED,
        .interval_us = DEFAULT_INTERVAL_US,
        .idle_space = DEFAULT_IDLE,
        .x_offset = 0,
        .y_offset = 0,
        .to_focus = false,
        .to_window = false,
        .target_name = NULL,
        .window_name = "ofumo",
        .display_name = NULL,
        .foreground = "black",
        .background = "white",
        .reverse_video = false,
        .no_shape = false,
        .debug_sync = false,
        .restore_cursor = -1,
        .verbose = false,
        .backend = BACKEND_AUTO,
    };

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-help") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-patchlevel") == 0) {
            printf("ofumo %s\n", OFUMO_VERSION);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            options->verbose = true;
            continue;
        }
        if (strcmp(arg, "-debug") == 0) { options->debug_sync = true; continue; }
        if (strcmp(arg, "-rv") == 0) { options->reverse_video = true; continue; }
        if (strcmp(arg, "-noshape") == 0) { options->no_shape = true; continue; }
        if (strcmp(arg, "-tofocus") == 0) {
            options->to_focus = true; options->to_window = false; options->target_name = NULL; continue;
        }
        if (strcmp(arg, "-towindow") == 0) {
            options->to_window = true; options->to_focus = false; options->target_name = NULL; continue;
        }
        if (strcmp(arg, "-neko") == 0 || strcmp(arg, "-tora") == 0 || strcmp(arg, "-dog") == 0 ||
            strcmp(arg, "-bsd_daemon") == 0 || strcmp(arg, "-sakura") == 0 || strcmp(arg, "-tomoyo") == 0) {
            continue;
        }
        if (strcmp(arg, "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ofumo: '--backend' requires auto, x11, or kwin\n");
                return false;
            }
            const char *value = argv[++i];
            if (strcmp(value, "auto") == 0) options->backend = BACKEND_AUTO;
            else if (strcmp(value, "x11") == 0) options->backend = BACKEND_X11;
            else if (strcmp(value, "kwin") == 0) options->backend = BACKEND_KWIN;
            else { fprintf(stderr, "ofumo: invalid backend '%s'\n", value); return false; }
            continue;
        }

        if (i + 1 >= argc) { fprintf(stderr, "ofumo: '%s' requires a value\n", arg); return false; }
        const char *value = argv[++i];

        if (strcmp(arg, "-speed") == 0 || strcmp(arg, "--speed") == 0) {
            if (!parse_double_positive(value, &options->speed)) return false;
        } else if (strcmp(arg, "-time") == 0 || strcmp(arg, "--time") == 0) {
            if (!parse_long_positive(value, &options->interval_us)) return false;
        } else if (strcmp(arg, "-idle") == 0 || strcmp(arg, "--idle") == 0) {
            if (!parse_int_positive(value, &options->idle_space)) return false;
        } else if (strcmp(arg, "-toname") == 0) {
            options->target_name = value; options->to_window = true; options->to_focus = false;
        } else if (strcmp(arg, "-position") == 0) {
            if (!parse_geometry_offset(value, &options->x_offset, &options->y_offset)) {
                fprintf(stderr, "ofumo: invalid -position '%s'\n", value); return false;
            }
        } else if (strcmp(arg, "-name") == 0) {
            options->window_name = value;
        } else if (strcmp(arg, "-display") == 0) {
            options->display_name = value;
        } else if (strcmp(arg, "-fg") == 0) {
            options->foreground = value;
        } else if (strcmp(arg, "-bg") == 0) {
            options->background = value;
        } else if (strcmp(arg, "-cursor") == 0) {
            if (!parse_int_nonnegative(value, &options->restore_cursor)) {
                fprintf(stderr, "ofumo: invalid -cursor '%s'\n", value); return false;
            }
        } else {
            fprintf(stderr, "ofumo: unknown option '%s'\n", arg); return false;
        }
    }
    return true;
}

/** @brief Recursively find an X11 window with the requested title. */
static Window window_with_name(Display *dpy, Window top, const char *name)
{
    char *window_name = NULL;
    if (XFetchName(dpy, top, &window_name) && window_name != NULL) {
        bool match = strcmp(window_name, name) == 0;
        XFree(window_name);
        if (match) return top;
    }

    Window root_return, parent_return, *children = NULL;
    unsigned int nchildren = 0;
    if (!XQueryTree(dpy, top, &root_return, &parent_return, &children, &nchildren)) return None;

    Window found = None;
    for (unsigned int i = 0; i < nchildren && found == None; ++i) found = window_with_name(dpy, children[i], name);
    if (children != NULL) XFree(children);
    return found;
}

/** @brief Let the user select an X11/XWayland target window. */
static Window select_window(Display *dpy, Window root)
{
    Cursor cursor = XCreateFontCursor(dpy, XC_crosshair);
    int status = XGrabPointer(dpy, root, False, ButtonPressMask | ButtonReleaseMask,
                              GrabModeSync, GrabModeAsync, root, cursor, CurrentTime);
    if (status != GrabSuccess) {
        fprintf(stderr, "ofumo: cannot grab pointer to select target window\n");
        if (cursor != None) XFreeCursor(dpy, cursor);
        return None;
    }

    Window target = None;
    int buttons = 0;
    while ((target == None || buttons != 0) && g_running) {
        XEvent event;
        XAllowEvents(dpy, SyncPointer, CurrentTime);
        XWindowEvent(dpy, root, ButtonPressMask | ButtonReleaseMask, &event);
        if (event.type == ButtonPress) {
            if (target == None) {
                target = event.xbutton.subwindow;
                if (target == None) target = root;
            }
            buttons++;
        } else if (event.type == ButtonRelease) {
            if (buttons > 0) buttons--;
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    if (cursor != None) XFreeCursor(dpy, cursor);
    return target;
}

/** @brief Resolve an X11 child window to its top-level ancestor. */
static Window to_toplevel_window(Display *dpy, Window root, Window target)
{
    if (target == None || target == root || target == PointerRoot) return None;
    for (;;) {
        Window query_root, parent, *children = NULL;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, target, &query_root, &parent, &children, &nchildren)) {
            if (children != NULL) XFree(children);
            return None;
        }
        if (children != NULL) XFree(children);
        if (parent == query_root) return target;
        target = parent;
    }
}

/** @brief Detect whether OFumo is running inside a Wayland session. */
static bool session_is_wayland(void)
{
    const char *session = getenv("XDG_SESSION_TYPE");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    return (session != NULL && strcmp(session, "wayland") == 0) || (wayland != NULL && *wayland != '\0');
}

/**
 * @brief Check whether the current desktop session is KDE Plasma.
 *
 * @return true when KDE/Plasma session markers are present.
 */
static bool session_is_kde_plasma(void)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *kde_session = getenv("KDE_FULL_SESSION");

    if (kde_session != NULL && *kde_session != '\0') return true;
    if (desktop == NULL) return false;

    return strstr(desktop, "KDE") != NULL || strstr(desktop, "Plasma") != NULL;
}

/** @brief Allocate a named X11 color and return its pixel value. */
static bool alloc_named_color(Display *display, int screen, const char *name, unsigned long *pixel)
{
    XColor exact, screen_color;
    Colormap colormap = DefaultColormap(display, screen);
    if (!XAllocNamedColor(display, colormap, name, &screen_color, &exact)) {
        fprintf(stderr, "ofumo: cannot allocate X11 color '%s'\n", name);
        return false;
    }
    *pixel = screen_color.pixel;
    return true;
}

/** @brief Convert a random millisecond delay range into runtime ticks. */
static uint64_t random_delay_ticks(long interval_us, unsigned int min_ms, unsigned int max_ms)
{
    unsigned int span = max_ms > min_ms ? (max_ms - min_ms) : 0;
    unsigned int delay_ms = min_ms + (span > 0 ? (unsigned int)(rand() % (span + 1U)) : 0U);
    uint64_t delay_us = (uint64_t)delay_ms * 1000ULL;
    uint64_t ticks = (delay_us + (uint64_t)interval_us - 1ULL) / (uint64_t)interval_us;
    return ticks > 0 ? ticks : 1;
}

/** @brief Create X11 pixmap and mask resources for all generated sprites. */
static bool create_sprite_pixmaps(App *app)
{
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        app->sprites[i] = XCreatePixmapFromBitmapData(app->display, app->root,
            (char *)g_sprite_defs[i].bits,
            g_sprite_defs[i].width, g_sprite_defs[i].height,
            app->foreground_pixel, app->background_pixel,
            app->depth);
        app->masks[i] = XCreateBitmapFromData(app->display, app->root,
            (const char *)g_sprite_defs[i].mask_bits,
            g_sprite_defs[i].width, g_sprite_defs[i].height);
        if (app->sprites[i] == None || app->masks[i] == None) {
            fprintf(stderr, "ofumo: failed to create pixmap for sprite '%s'\n", sprite_name((SpriteId)i));
            return false;
        }
    }
    return true;
}

/**
 * @brief Initialize display, backend, target mode, window, graphics, and animations.
 * @return true when the application is ready to enter the main loop.
 */
static bool app_init(App *app, const Options *options)
{
    memset(app, 0, sizeof(*app));
    app->restore_cursor = -1;
    app->target = None;
    app->prev_target = None;
    app->last_x = -1;
    app->last_y = -1;
    app->last_draw_sprite = SPRITE_COUNT;
    app->current_sprite = SPRITE_IDLE;

    app->display = XOpenDisplay(options->display_name);
    if (app->display == NULL) {
        const char *requested = options->display_name != NULL ? options->display_name :
            (getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "<unset>");
        fprintf(stderr, "ofumo: cannot open X display '%s'\n", requested);
        return false;
    }

    if (options->debug_sync) {
        XSynchronize(app->display, True);
        if (options->verbose) fprintf(stderr, "ofumo: X11 synchronous debug mode enabled\n");
    }

    app->screen = DefaultScreen(app->display);
    app->depth = (unsigned int)DefaultDepth(app->display, app->screen);
    app->root = RootWindow(app->display, app->screen);

    Window tmp_root;
    int root_x, root_y;
    unsigned int border;
    if (!XGetGeometry(app->display, app->root, &tmp_root, &root_x, &root_y,
                      &app->screen_width, &app->screen_height, &border, &app->depth)) {
        fprintf(stderr, "ofumo: cannot query root geometry\n");
        return false;
    }

    app->shape_enabled = !options->no_shape;
    if (app->shape_enabled) {
        int shape_event_base, shape_error_base;
        if (!XShapeQueryExtension(app->display, &shape_event_base, &shape_error_base)) {
            fprintf(stderr, "ofumo: X Shape extension is not available; continuing as if -noshape was used\n");
            app->shape_enabled = false;
        }
    }

    const char *foreground = options->foreground;
    const char *background = options->background;
    if (options->reverse_video) { const char *tmp = foreground; foreground = background; background = tmp; }
    if (!alloc_named_color(app->display, app->screen, foreground, &app->foreground_pixel) ||
        !alloc_named_color(app->display, app->screen, background, &app->background_pixel)) {
        return false;
    }
    app->restore_cursor = options->restore_cursor;

    bool want_kwin = options->backend == BACKEND_KWIN;
    if (options->backend == BACKEND_AUTO && session_is_wayland()) {
        if (!session_is_kde_plasma()) {
            fprintf(stderr,
                    "ofumo: unsupported Wayland compositor.\n"
                    "       OFumo 0.0.1 supports X11 and KDE Plasma Wayland.\n");
            return false;
        }
        want_kwin = true;
    }

    if (want_kwin) {
        if (!kwin_bridge_init(&app->kwin, options->verbose)) {
            fprintf(stderr,
                    "ofumo: KDE Wayland needs the KWin bridge for global cursor tracking.\n"
                    "       The bridge could not start, so OFumo will not fall back to broken\n"
                    "       XWayland pointer tracking.\n");
            return false;
        }
        app->use_kwin_bridge = true;
        app->bridge_active_seen = app->kwin.active_generation;
    } else {
        app->use_kwin_bridge = false;
        if (options->verbose) fprintf(stderr, "ofumo: backend=x11\n");
    }

    if (options->to_window && app->use_kwin_bridge && options->target_name == NULL) {
        fprintf(stderr,
                "ofumo: note: -towindow can select only X11/XWayland windows on Wayland;\n"
                "       use -tofocus for native Wayland windows.\n");
    }

    if (options->to_window) {
        if (options->target_name != NULL) {
            app->target = window_with_name(app->display, app->root, options->target_name);
            if (app->target == None) {
                fprintf(stderr, "ofumo: no X11 window named '%s' found\n", options->target_name);
                return false;
            }
        } else {
            fprintf(stderr, "ofumo: click the window Koishi should chase (click desktop for mouse mode)\n");
            app->target = select_window(app->display, app->root);
            if (app->target == app->root) app->target = None;
            if (app->target == None && !g_running) return false;
        }
        if (app->target != None) {
            app->target = to_toplevel_window(app->display, app->root, app->target);
            if (app->target == None) {
                fprintf(stderr, "ofumo: selected target window disappeared\n");
                return false;
            }
        }
    }

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = app->background_pixel;
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask | VisibilityChangeMask | KeyPressMask | ButtonPressMask;

    app->window = XCreateWindow(app->display, app->root, 0, 0,
                                OFUMO_WIDTH, OFUMO_HEIGHT, 0,
                                app->depth, InputOutput, CopyFromParent,
                                CWBackPixel | CWOverrideRedirect | CWEventMask,
                                &attrs);
    if (app->window == None) {
        fprintf(stderr, "ofumo: failed to create X11 window\n");
        return false;
    }

    XStoreName(app->display, app->window, options->window_name);
    XClassHint class_hint = { .res_name = (char *)"ofumo", .res_class = (char *)"OFumo" };
    XSetClassHint(app->display, app->window, &class_hint);

    if (!create_sprite_pixmaps(app)) return false;

    XGCValues values;
    memset(&values, 0, sizeof(values));
    values.function = GXcopy;
    values.foreground = app->foreground_pixel;
    values.background = app->background_pixel;
    values.fill_style = FillTiled;
    values.tile = app->sprites[SPRITE_IDLE];
    values.ts_x_origin = 0;
    values.ts_y_origin = 0;
    app->gc = XCreateGC(app->display, app->window,
                        GCFunction | GCForeground | GCBackground | GCFillStyle |
                        GCTile | GCTileStipXOrigin | GCTileStipYOrigin,
                        &values);
    if (app->gc == NULL) {
        fprintf(stderr, "ofumo: failed to create graphics context\n");
        return false;
    }

    if (app->shape_enabled) {
        XShapeCombineMask(app->display, app->window, ShapeBounding, 0, 0,
                          app->masks[SPRITE_IDLE], ShapeSet);
    }

    app->fumo_x = ((int)app->screen_width - OFUMO_WIDTH / 2) / 2;
    app->fumo_y = ((int)app->screen_height - OFUMO_HEIGHT / 2) / 2;
    app->state = FUMO_STOP;
    app->tick_count = 0;
    app->state_count = 0;
    app->raise_window_delay = 0;
    app->mapped = false;
    app->special_anim = SPECIAL_SPAWN;
    app->special_tick = 0;
    app->frame_counter = 0;
    app->wink_ticks_remaining = 0;
    app->interval_us = options->interval_us;
    app->next_wink_frame = random_delay_ticks(app->interval_us, 10000, 16000);
    app->next_spin_frame = random_delay_ticks(app->interval_us, 15000, 30000);

    XFlush(app->display);
    return true;
}

/** @brief Release X11, KWin, graphics, and cursor resources. */
static void app_destroy(App *app)
{
    if (app->use_kwin_bridge || app->kwin.bus != NULL) {
        kwin_bridge_destroy(&app->kwin);
        app->use_kwin_bridge = false;
    }
    if (app->display == NULL) return;

    if (app->restore_cursor >= 0) {
        XSetWindowAttributes attrs;
        memset(&attrs, 0, sizeof(attrs));
        Cursor cursor = None;
        if (app->restore_cursor >= XC_X_cursor && app->restore_cursor < XC_X_cursor + XC_num_glyphs) {
            cursor = XCreateFontCursor(app->display, (unsigned int)app->restore_cursor);
        }
        attrs.cursor = cursor;
        XChangeWindowAttributes(app->display, app->root, CWCursor, &attrs);
        if (cursor != None) XFreeCursor(app->display, cursor);
    }

    if (app->gc != NULL) XFreeGC(app->display, app->gc);
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        if (app->sprites[i] != None) XFreePixmap(app->display, app->sprites[i]);
        if (app->masks[i] != None) XFreePixmap(app->display, app->masks[i]);
    }
    if (app->window != None) XDestroyWindow(app->display, app->window);
    XCloseDisplay(app->display);
    memset(app, 0, sizeof(*app));
}

/** @brief Return the duration, in ticks, of a special animation. */
static int special_total_ticks(SpecialAnim anim)
{
    switch (anim) {
    case SPECIAL_SPAWN: return 6;
    case SPECIAL_CLICK: return 6;
    case SPECIAL_SPIN: return 8; /* left -> up -> right -> front, twice */
    case SPECIAL_NONE: break;
    }
    return 0;
}

/** @brief Start a spawn, click, or spin animation from its first frame. */
static void start_special(App *app, SpecialAnim anim)
{
    app->special_anim = anim;
    app->special_tick = 0;
}

/** @brief Schedule sparse winks and occasional idle spins. */
static void update_ambient_animations(App *app)
{
    app->frame_counter++;

    if (app->special_anim != SPECIAL_NONE) return;

    const bool can_wink = app->state == FUMO_STOP || app->state == FUMO_JARE;
    if (can_wink && app->wink_ticks_remaining == 0 &&
        app->frame_counter >= app->next_wink_frame) {
        /* Keep ambient blinking sparse so it remains an idle expression. */
        app->wink_ticks_remaining = 2;
        app->next_wink_frame = app->frame_counter +
            random_delay_ticks(app->interval_us, 10000, 16000);
    }

    /* Spin is an OFumo idle special, analogous to one of oneko's fidgets.
     * It only gets considered when JARE starts, has a long cooldown, and has
     * a 1-in-3 chance so it does not become repetitive. */
    if (app->state == FUMO_JARE && app->tick_count == 0 &&
        app->frame_counter >= app->next_spin_frame && (rand() % 3) == 0) {
        start_special(app, SPECIAL_SPIN);
        app->next_spin_frame = app->frame_counter +
            random_delay_ticks(app->interval_us, 20000, 40000);
    }
}

/** @brief Select the sprite for the current special-animation tick. */
static SpriteId special_sprite(const App *app)
{
    if (app->special_anim == SPECIAL_SPAWN) {
        if (app->special_tick < 2) return SPRITE_HIDE_HAT;
        if (app->special_tick == 4) return SPRITE_WINK;
        return SPRITE_IDLE;
    }
    if (app->special_anim == SPECIAL_CLICK) {
        switch (app->special_tick) {
        case 0: return SPRITE_WINK;
        case 1:
        case 2: return SPRITE_HIDE_HAT;
        case 4: return SPRITE_WINK;
        default: return SPRITE_IDLE;
        }
    }
    if (app->special_anim == SPECIAL_SPIN) {
        switch (app->special_tick % 4) {
        case 0: return SPRITE_LEFT;
        case 1: return SPRITE_UP;
        case 2: return SPRITE_RIGHT;
        default: return SPRITE_IDLE;
        }
    }
    return SPRITE_IDLE;
}

/** @brief Map the current runtime state to a Koishi sprite. */
static SpriteId sprite_for_state(const App *app)
{
    if (app->special_anim != SPECIAL_NONE) return special_sprite(app);

    if (app->wink_ticks_remaining > 0 &&
        (app->state == FUMO_STOP || app->state == FUMO_JARE)) {
        return SPRITE_WINK;
    }

    switch (app->state) {
    case FUMO_STOP:
        return SPRITE_IDLE;
    case FUMO_JARE:
        return SPRITE_IDLE;
    case FUMO_KAKI:
        return ((app->tick_count / 2) % 2 == 0) ? SPRITE_HIDE_HAT : SPRITE_IDLE;
    case FUMO_AKUBI:
        return SPRITE_SLEEP0;
    case FUMO_SLEEP: {
        switch ((app->tick_count / 2) % 4) {
        case 0: return SPRITE_SLEEP0;
        case 1: return SPRITE_SLEEP1;
        case 2: return SPRITE_SLEEP2;
        default: return SPRITE_SLEEP1;
        }
    }
    case FUMO_AWAKE:
        return SPRITE_IDLE;
    case FUMO_U_MOVE:
    case FUMO_UL_MOVE:
    case FUMO_UR_MOVE:
    case FUMO_U_TOGI:
        return SPRITE_UP;
    case FUMO_L_MOVE:
    case FUMO_L_TOGI:
        return SPRITE_LEFT;
    case FUMO_R_MOVE:
    case FUMO_R_TOGI:
        return SPRITE_RIGHT;
    case FUMO_D_MOVE:
    case FUMO_D_TOGI:
    case FUMO_DL_MOVE:
    case FUMO_DR_MOVE:
        return SPRITE_IDLE;
    }
    return SPRITE_IDLE;
}

/** @brief Move, shape, and redraw the OFumo window when state changes. */
static void draw_fumo(App *app)
{
    app->current_sprite = sprite_for_state(app);
    if (app->current_sprite >= SPRITE_COUNT) app->current_sprite = SPRITE_IDLE;

    if (app->fumo_x != app->last_x || app->fumo_y != app->last_y || app->current_sprite != app->last_draw_sprite) {
        XWindowChanges changes;
        changes.x = app->fumo_x;
        changes.y = app->fumo_y;
        XConfigureWindow(app->display, app->window, CWX | CWY, &changes);

        XSetTile(app->display, app->gc, app->sprites[app->current_sprite]);
        if (app->shape_enabled) {
            XShapeCombineMask(app->display, app->window, ShapeBounding, 0, 0,
                              app->masks[app->current_sprite], ShapeSet);
        }

        if (!app->mapped) {
            XMapWindow(app->display, app->window);
            app->mapped = true;
        }

        XClearWindow(app->display, app->window);
        XFillRectangle(app->display, app->window, app->gc, 0, 0, OFUMO_WIDTH, OFUMO_HEIGHT);
    }

    XFlush(app->display);
    app->last_x = app->fumo_x;
    app->last_y = app->fumo_y;
    app->last_draw_sprite = app->current_sprite;
}

/** @brief Repaint the current sprite after an X11 expose event. */
static void redraw_fumo(App *app)
{
    XSetTile(app->display, app->gc, app->sprites[app->current_sprite]);
    if (app->shape_enabled) {
        XShapeCombineMask(app->display, app->window, ShapeBounding, 0, 0,
                          app->masks[app->current_sprite], ShapeSet);
    }
    XClearWindow(app->display, app->window);
    XFillRectangle(app->display, app->window, app->gc, 0, 0, OFUMO_WIDTH, OFUMO_HEIGHT);
    XFlush(app->display);
}

/** @brief Advance the oneko-style tick and state counters. */
static void tick_count(App *app)
{
    if (++app->tick_count >= MAX_TICK) app->tick_count = 0;
    if ((app->tick_count % 2) == 0 && app->state_count < MAX_TICK) app->state_count++;
}

/** @brief Check whether pointer/target movement should wake Koishi. */
static bool is_move_start(const App *app, int idle_space)
{
    return !((app->prev_mouse_x >= app->mouse_x - idle_space && app->prev_mouse_x <= app->mouse_x + idle_space) &&
             (app->prev_mouse_y >= app->mouse_y - idle_space && app->prev_mouse_y <= app->mouse_y + idle_space) &&
             (app->prev_target == app->target) && !app->bridge_target_changed);
}

/** @brief Clamp Koishi to the screen and report an edge collision. */
static bool is_window_over(App *app)
{
    bool over = false;
    const int max_x = (int)app->screen_width - OFUMO_WIDTH;
    const int max_y = (int)app->screen_height - OFUMO_HEIGHT;

    if (app->fumo_y <= 0) { app->fumo_y = 0; over = true; }
    else if (app->fumo_y >= max_y) { app->fumo_y = max_y; over = true; }

    if (app->fumo_x <= 0) { app->fumo_x = 0; over = true; }
    else if (app->fumo_x >= max_x) { app->fumo_x = max_x; over = true; }
    return over;
}

/** @brief Check whether the last movement tick changed position. */
static bool is_fumo_dont_move(const App *app)
{
    return app->fumo_x == app->last_x && app->fumo_y == app->last_y;
}

/** @brief Convert the movement vector into one of eight direction states. */
static void fumo_direction(App *app, bool verbose)
{
    FumoState next;
    if (app->move_dx == 0 && app->move_dy == 0) {
        next = FUMO_STOP;
    } else {
        double x = (double)app->move_dx;
        double y = (double)(-app->move_dy);
        double length = sqrt(x * x + y * y);
        double sin_theta = y / length;
        double s1 = sin(PI_PER_8);
        double s3 = sin(PI_PER_8 * 3.0);

        if (app->move_dx > 0) {
            if (sin_theta > s3) next = FUMO_U_MOVE;
            else if (sin_theta > s1) next = FUMO_UR_MOVE;
            else if (sin_theta > -s1) next = FUMO_R_MOVE;
            else if (sin_theta > -s3) next = FUMO_DR_MOVE;
            else next = FUMO_D_MOVE;
        } else {
            if (sin_theta > s3) next = FUMO_U_MOVE;
            else if (sin_theta > s1) next = FUMO_UL_MOVE;
            else if (sin_theta > -s1) next = FUMO_L_MOVE;
            else if (sin_theta > -s3) next = FUMO_DL_MOVE;
            else next = FUMO_D_MOVE;
        }
    }
    set_state(app, next, verbose);
}

/**
 * @brief Read the active backend and calculate the normalized target vector.
 *
 * Mouse and window targeting preserve the bottom-centre semantics used by
 * classic oneko.
 */
static void calc_dxdy(App *app, const Options *options)
{
    int absolute_x = app->mouse_x;
    int absolute_y = app->mouse_y;

    app->bridge_target_changed = false;
    if (app->use_kwin_bridge) {
        kwin_bridge_process(&app->kwin);
        if (app->kwin.cursor_valid) {
            absolute_x = app->kwin.cursor_x;
            absolute_y = app->kwin.cursor_y;
        }
        app->bridge_target_changed = app->kwin.active_generation != app->bridge_active_seen;
        app->bridge_active_seen = app->kwin.active_generation;
    } else {
        Window query_root, query_child;
        int relative_x, relative_y;
        unsigned int mod_mask;
        (void)XQueryPointer(app->display, app->window, &query_root, &query_child,
                            &absolute_x, &absolute_y, &relative_x, &relative_y, &mod_mask);
    }

    app->prev_mouse_x = app->mouse_x;
    app->prev_mouse_y = app->mouse_y;
    app->prev_target = app->target;

    app->mouse_x = absolute_x + options->x_offset;
    app->mouse_y = absolute_y + options->y_offset;

    if (options->to_focus && !app->use_kwin_bridge) {
        Window focus;
        int revert;
        XGetInputFocus(app->display, &focus, &revert);
        app->target = to_toplevel_window(app->display, app->root, focus);
    }

    double large_x;
    double large_y;

    if (options->to_focus && app->use_kwin_bridge && app->kwin.active_valid) {
        const int wx = app->kwin.active_x;
        const int wy = app->kwin.active_y;
        const int ww = app->kwin.active_width;

        if (app->mouse_x < wx + OFUMO_WIDTH / 2) {
            large_x = (double)(wx + options->x_offset - app->fumo_x);
        } else if (app->mouse_x > wx + ww - OFUMO_WIDTH / 2) {
            large_x = (double)(wx + ww + options->x_offset - app->fumo_x - OFUMO_WIDTH);
        } else {
            large_x = (double)(app->mouse_x - app->fumo_x - OFUMO_WIDTH / 2);
        }
        large_y = (double)(wy + options->y_offset - app->fumo_y - OFUMO_HEIGHT);
    } else if ((options->to_window || options->to_focus) && app->target != None) {
        XWindowAttributes attr;
        int attr_status = XGetWindowAttributes(app->display, app->target, &attr);
        if (attr_status != 0 && attr.map_state == IsViewable &&
            attr.x + attr.width > 0 && attr.x < (int)app->screen_width &&
            attr.y + attr.height > 0 && attr.y < (int)app->screen_height) {
            if (options->to_focus) {
                if (app->mouse_x < attr.x + OFUMO_WIDTH / 2) {
                    large_x = (double)(attr.x + options->x_offset - app->fumo_x);
                } else if (app->mouse_x > attr.x + attr.width - OFUMO_WIDTH / 2) {
                    large_x = (double)(attr.x + attr.width + options->x_offset - app->fumo_x - OFUMO_WIDTH);
                } else {
                    large_x = (double)(app->mouse_x - app->fumo_x - OFUMO_WIDTH / 2);
                }
                large_y = (double)(attr.y + options->y_offset - app->fumo_y - OFUMO_HEIGHT);
            } else {
                app->mouse_x = attr.x + attr.width / 2 + options->x_offset;
                app->mouse_y = attr.y + options->y_offset;
                large_x = (double)(app->mouse_x - app->fumo_x - OFUMO_WIDTH / 2);
                large_y = (double)(app->mouse_y - app->fumo_y - OFUMO_HEIGHT);
            }
        } else {
            if (options->to_window && attr_status == 0) app->target_lost = true;
            large_x = (double)(app->mouse_x - app->fumo_x - OFUMO_WIDTH / 2);
            large_y = (double)(app->mouse_y - app->fumo_y - OFUMO_HEIGHT);
        }
    } else {
        large_x = (double)(app->mouse_x - app->fumo_x - OFUMO_WIDTH / 2);
        large_y = (double)(app->mouse_y - app->fumo_y - OFUMO_HEIGHT);
    }

    double double_length = large_x * large_x + large_y * large_y;
    if (double_length != 0.0) {
        double length = sqrt(double_length);
        if (length <= options->speed) {
            app->move_dx = (int)large_x;
            app->move_dy = (int)large_y;
        } else {
            app->move_dx = (int)((options->speed * large_x) / length);
            app->move_dy = (int)((options->speed * large_y) / length);
        }
    } else {
        app->move_dx = 0;
        app->move_dy = 0;
    }
}

/**
 * @brief Execute one OFumo state-machine tick.
 *
 * This is the main oneko-derived movement/idle state machine with OFumo
 * animation scheduling layered on top.
 */
static void think_fumo(App *app, const Options *options)
{
    calc_dxdy(app, options);
    update_ambient_animations(app);
    draw_fumo(app);
    if (app->wink_ticks_remaining > 0) app->wink_ticks_remaining--;
    tick_count(app);

    if (app->special_anim != SPECIAL_NONE) {
        app->special_tick++;
        if (app->special_tick >= special_total_ticks(app->special_anim)) {
            app->special_anim = SPECIAL_NONE;
            app->special_tick = 0;
            app->tick_count = 0;
            app->state_count = 0;
        }
        return;
    }

    switch (app->state) {
    case FUMO_STOP:
        if (is_move_start(app, options->idle_space)) {
            set_state(app, FUMO_AWAKE, options->verbose);
            break;
        }
        if (app->state_count < STOP_TIME) break;

        if (app->move_dx < 0 && app->fumo_x <= 0) {
            set_state(app, FUMO_L_TOGI, options->verbose);
        } else if (app->move_dx > 0 && app->fumo_x >= (int)app->screen_width - OFUMO_WIDTH) {
            set_state(app, FUMO_R_TOGI, options->verbose);
        } else if ((app->move_dy < 0 && app->fumo_y <= 0) ||
                   (options->to_focus && (app->target != None || (app->use_kwin_bridge && app->kwin.active_valid)) && app->fumo_y > app->mouse_y)) {
            set_state(app, FUMO_U_TOGI, options->verbose);
        } else if ((app->move_dy > 0 && app->fumo_y >= (int)app->screen_height - OFUMO_HEIGHT) ||
                   (options->to_focus && (app->target != None || (app->use_kwin_bridge && app->kwin.active_valid)) && app->fumo_y < app->mouse_y - OFUMO_HEIGHT)) {
            set_state(app, FUMO_D_TOGI, options->verbose);
        } else {
            set_state(app, FUMO_JARE, options->verbose);
        }
        break;

    case FUMO_JARE:
        if (is_move_start(app, options->idle_space)) set_state(app, FUMO_AWAKE, options->verbose);
        else if (app->state_count >= JARE_TIME) set_state(app, FUMO_KAKI, options->verbose);
        break;

    case FUMO_KAKI:
        if (is_move_start(app, options->idle_space)) set_state(app, FUMO_AWAKE, options->verbose);
        else if (app->state_count >= KAKI_TIME) set_state(app, FUMO_AKUBI, options->verbose);
        break;

    case FUMO_AKUBI:
        if (is_move_start(app, options->idle_space)) set_state(app, FUMO_AWAKE, options->verbose);
        else if (app->state_count >= AKUBI_TIME) set_state(app, FUMO_SLEEP, options->verbose);
        break;

    case FUMO_SLEEP:
        if (is_move_start(app, options->idle_space)) set_state(app, FUMO_AWAKE, options->verbose);
        break;

    case FUMO_AWAKE:
        if (app->state_count >= AWAKE_TIME) fumo_direction(app, options->verbose);
        break;

    case FUMO_U_MOVE:
    case FUMO_D_MOVE:
    case FUMO_L_MOVE:
    case FUMO_R_MOVE:
    case FUMO_UL_MOVE:
    case FUMO_UR_MOVE:
    case FUMO_DL_MOVE:
    case FUMO_DR_MOVE:
        app->fumo_x += app->move_dx;
        app->fumo_y += app->move_dy;
        fumo_direction(app, options->verbose);
        if (is_window_over(app) && is_fumo_dont_move(app)) set_state(app, FUMO_STOP, options->verbose);
        break;

    case FUMO_U_TOGI:
    case FUMO_D_TOGI:
    case FUMO_L_TOGI:
    case FUMO_R_TOGI:
        if (is_move_start(app, options->idle_space)) set_state(app, FUMO_AWAKE, options->verbose);
        else if (app->state_count >= TOGI_TIME) set_state(app, FUMO_KAKI, options->verbose);
        break;
    }
}

/** @brief Process pending X11 events and click interactions. */
static bool process_x_events(App *app)
{
    while (XPending(app->display) > 0) {
        XEvent event;
        XNextEvent(app->display, &event);
        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0) redraw_fumo(app);
            break;
        case VisibilityNotify:
            if (app->raise_window_delay == 0) {
                XRaiseWindow(app->display, app->window);
                app->raise_window_delay = DEFAULT_RAISE_WAIT;
            }
            break;
        case ButtonPress:
            if (event.xbutton.window == app->window && app->special_anim != SPECIAL_SPAWN) {
                start_special(app, SPECIAL_CLICK);
            }
            break;
        case KeyPress: {
            if ((event.xkey.state & Mod1Mask) != 0) {
                char buffer[8] = {0};
                KeySym keysym;
                if (XLookupString(&event.xkey, buffer, (int)sizeof(buffer) - 1, &keysym, NULL) > 0 &&
                    (buffer[0] == 'q' || buffer[0] == 'Q')) {
                    return false;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return true;
}

/** @brief Start the periodic SIGALRM timer used by the main loop. */
static bool start_interval_timer(long interval_us)
{
    struct itimerval value;
    memset(&value, 0, sizeof(value));
    value.it_interval.tv_sec = interval_us / 1000000L;
    value.it_interval.tv_usec = interval_us % 1000000L;
    value.it_value = value.it_interval;
    return setitimer(ITIMER_REAL, &value, NULL) == 0;
}

/** @brief Stop the periodic SIGALRM timer. */
static void stop_interval_timer(void)
{
    struct itimerval value;
    memset(&value, 0, sizeof(value));
    (void)setitimer(ITIMER_REAL, &value, NULL);
}

/** @brief Initialize OFumo, execute the runtime loop, and clean up. */
static int run_ofumo(const Options *options)
{
    struct sigaction alarm_action;
    memset(&alarm_action, 0, sizeof(alarm_action));
    alarm_action.sa_handler = handle_alarm;
    sigemptyset(&alarm_action.sa_mask);
    sigaction(SIGALRM, &alarm_action, NULL);

    struct sigaction stop_action;
    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = handle_stop;
    sigemptyset(&stop_action.sa_mask);
    sigaction(SIGINT, &stop_action, NULL);
    sigaction(SIGTERM, &stop_action, NULL);

    App app;
    if (!app_init(&app, options)) {
        app_destroy(&app);
        return EXIT_FAILURE;
    }

    if (!start_interval_timer(options->interval_us)) {
        perror("ofumo: setitimer");
        app_destroy(&app);
        return EXIT_FAILURE;
    }

    calc_dxdy(&app, options);
    app.prev_mouse_x = app.mouse_x;
    app.prev_mouse_y = app.mouse_y;
    app.prev_target = app.target;

    while (g_running) {
        think_fumo(&app, options);
        if (g_running) {
            pause();
            if (app.raise_window_delay > 0) app.raise_window_delay--;
        }
        if (!process_x_events(&app)) break;
        if (app.target_lost && options->to_window) {
            fprintf(stderr, "ofumo: target window disappeared\n");
            break;
        }
    }

    stop_interval_timer();
    app_destroy(&app);
    return EXIT_SUCCESS;
}

/**
 * @brief OFumo process entry point.
 * @return EXIT_SUCCESS on normal termination, otherwise EXIT_FAILURE.
 */
int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    Options options;
    if (!parse_options(argc, argv, &options)) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (options.verbose) {
        fprintf(stderr, "ofumo: speed=%.2f time=%ld idle=%d sprite=%dx%d mode=%s\n",
                options.speed, options.interval_us, options.idle_space,
                OFUMO_WIDTH, OFUMO_HEIGHT,
                options.to_focus ? "focus-window" : options.to_window ? "target-window" : "mouse");
        fprintf(stderr, "ofumo: requested backend=%s session=%s display=%s\n",
                options.backend == BACKEND_X11 ? "x11" : options.backend == BACKEND_KWIN ? "kwin" : "auto",
                session_is_wayland() ? "wayland" : "x11",
                options.display_name != NULL ? options.display_name :
                (getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "<unset>"));
        fprintf(stderr, "ofumo: colors fg=%s bg=%s reverse=%s shape=%s\n",
                options.foreground, options.background,
                options.reverse_video ? "yes" : "no", options.no_shape ? "off" : "on");
    }

    return run_ofumo(&options);
}
