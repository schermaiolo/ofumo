/*
* File     : kwin_bridge.c
* Platform : Linux / KDE Plasma Wayland
* Author   : Daniel Fridman (Schermaiolo)
*
* Temporary KWin scripting and D-Bus bridge used to expose global pointer
* coordinates and focused-window geometry to the OFumo runtime.
*/

#define _GNU_SOURCE

#include "kwin_bridge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define OFUMO_DBUS_PATH "/dev/schermaiolo/OFumo"
#define OFUMO_DBUS_IFACE "dev.schermaiolo.OFumo.Bridge"

static const char bridge_script_template[] =
    "// OFumo temporary KWin bridge. Loaded only for this process.\n"
    "const OFUMO_SERVICE = '@@SERVICE@@';\n"
    "const OFUMO_PATH = '/dev/schermaiolo/OFumo';\n"
    "const OFUMO_IFACE = 'dev.schermaiolo.OFumo.Bridge';\n"
    "const OFUMO_PID = @@PID@@;\n"
    "let lastCursorSent = 0;\n"
    "\n"
    "function sendCursor(force) {\n"
    "    const now = Date.now();\n"
    "    if (!force && (now - lastCursorSent) < 16) return;\n"
    "    lastCursorSent = now;\n"
    "    const p = workspace.cursorPos;\n"
    "    const payload = String(Math.round(p.x)) + ',' + String(Math.round(p.y));\n"
    "    callDBus(OFUMO_SERVICE, OFUMO_PATH, OFUMO_IFACE, 'CursorMoved', payload);\n"
    "}\n"
    "\n"
    "function sendActiveWindow() {\n"
    "    const w = workspace.activeWindow;\n"
    "    if (!w || w.pid === OFUMO_PID) {\n"
    "        callDBus(OFUMO_SERVICE, OFUMO_PATH, OFUMO_IFACE, 'ActiveWindowChanged', '0,0,0,0,0');\n"
    "        return;\n"
    "    }\n"
    "    const g = w.frameGeometry;\n"
    "    const payload = String(Math.round(g.x)) + ',' + String(Math.round(g.y)) + ',' +\n"
    "                    String(Math.round(g.width)) + ',' + String(Math.round(g.height)) + ',1';\n"
    "    callDBus(OFUMO_SERVICE, OFUMO_PATH, OFUMO_IFACE, 'ActiveWindowChanged', payload);\n"
    "}\n"
    "\n"
    "function markPet(w) {\n"
    "    if (!w || w.pid !== OFUMO_PID) return;\n"
    "    try { w.skipTaskbar = true; } catch (e) {}\n"
    "    try { w.skipPager = true; } catch (e) {}\n"
    "    try { w.skipSwitcher = true; } catch (e) {}\n"
    "    try { w.keepAbove = true; } catch (e) {}\n"
    "}\n"
    "\n"
    "workspace.cursorPosChanged.connect(function() { sendCursor(false); sendActiveWindow(); });\n"
    "workspace.windowActivated.connect(function() { sendActiveWindow(); });\n"
    "workspace.windowAdded.connect(function(w) { markPet(w); });\n"
    "const ofumoWindows = workspace.stackingOrder;\n"
    "for (let i = 0; i < ofumoWindows.length; ++i) markPet(ofumoWindows[i]);\n"
    "sendCursor(true);\n"
    "sendActiveWindow();\n";

/**
 * @brief D-Bus callback receiving global cursor coordinates from KWin.
 *
 * @param message Incoming D-Bus method call.
 * @param userdata Pointer to the KWinBridge instance.
 * @param ret_error Unused sd-bus error output.
 * @return sd-bus status code.
 */
static int on_cursor_moved(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    KWinBridge *bridge = userdata;
    const char *payload = NULL;
    int r = sd_bus_message_read(message, "s", &payload);
    if (r < 0) return r;

    int x = 0;
    int y = 0;
    if (payload == NULL || sscanf(payload, "%d,%d", &x, &y) != 2) {
        return sd_bus_reply_method_errorf(message,
                                          SD_BUS_ERROR_INVALID_ARGS,
                                          "invalid cursor payload");
    }

    bridge->cursor_x = x;
    bridge->cursor_y = y;
    bridge->cursor_valid = true;
    return sd_bus_reply_method_return(message, "");
}

/**
 * @brief D-Bus callback receiving active-window geometry from KWin.
 *
 * @param message Incoming D-Bus method call.
 * @param userdata Pointer to the KWinBridge instance.
 * @param ret_error Unused sd-bus error output.
 * @return sd-bus status code.
 */
static int on_active_window_changed(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    KWinBridge *bridge = userdata;
    const char *payload = NULL;
    int r = sd_bus_message_read(message, "s", &payload);
    if (r < 0) return r;

    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int valid = 0;
    if (payload == NULL ||
        sscanf(payload, "%d,%d,%d,%d,%d", &x, &y, &width, &height, &valid) != 5) {
        return sd_bus_reply_method_errorf(message,
                                          SD_BUS_ERROR_INVALID_ARGS,
                                          "invalid active-window payload");
    }

    bool changed = bridge->active_valid != (valid != 0) ||
                   bridge->active_x != x || bridge->active_y != y ||
                   bridge->active_width != width || bridge->active_height != height;

    bridge->active_valid = valid != 0;
    bridge->active_x = x;
    bridge->active_y = y;
    bridge->active_width = width;
    bridge->active_height = height;
    if (changed) bridge->active_generation++;

    return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable bridge_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("CursorMoved", "s", "", on_cursor_moved, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ActiveWindowChanged", "s", "", on_active_window_changed, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

/**
 * @brief Expand bridge-script placeholders and write the generated script.
 *
 * @param file Destination file.
 * @param template_text JavaScript template text.
 * @param service_name Per-process D-Bus service name.
 * @param pid OFumo process identifier.
 * @return true when the complete script is written successfully.
 */
static bool replace_and_write(FILE *file, const char *template_text,
                              const char *service_name, pid_t pid)
{
    static const char service_marker[] = "@@SERVICE@@";
    static const char pid_marker[] = "@@PID@@";
    const size_t service_marker_len = sizeof(service_marker) - 1;
    const size_t pid_marker_len = sizeof(pid_marker) - 1;

    const char *p = template_text;
    while (*p != '\0') {
        if (strncmp(p, service_marker, service_marker_len) == 0) {
            if (fputs(service_name, file) == EOF) return false;
            p += service_marker_len;
        } else if (strncmp(p, pid_marker, pid_marker_len) == 0) {
            if (fprintf(file, "%ld", (long)pid) < 0) return false;
            p += pid_marker_len;
        } else {
            if (fputc((unsigned char)*p, file) == EOF) return false;
            p++;
        }
    }
    return fflush(file) == 0;
}

/**
 * @brief Create the temporary KWin JavaScript bridge on disk.
 *
 * @param bridge Bridge instance receiving the generated script path.
 * @return true on success.
 */
static bool create_bridge_script(KWinBridge *bridge)
{
    char template_path[] = "/tmp/ofumo-kwin-bridge-XXXXXX";
    int fd = mkstemp(template_path);
    if (fd < 0) {
        fprintf(stderr, "ofumo: cannot create KWin bridge script: %s\n", strerror(errno));
        return false;
    }

    FILE *file = fdopen(fd, "w");
    if (file == NULL) {
        fprintf(stderr, "ofumo: fdopen failed for KWin bridge script: %s\n", strerror(errno));
        close(fd);
        unlink(template_path);
        return false;
    }

    bool ok = replace_and_write(file, bridge_script_template,
                                bridge->service_name, getpid());
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "ofumo: failed to write KWin bridge script\n");
        unlink(template_path);
        return false;
    }

    if (strlen(template_path) >= sizeof(bridge->script_path)) {
        unlink(template_path);
        return false;
    }
    strcpy(bridge->script_path, template_path);
    return true;
}

/**
 * @brief Load and start the temporary script through KWin scripting D-Bus.
 *
 * @param bridge Initialized bridge instance.
 * @return Non-negative sd-bus status on success, negative errno-style value on failure.
 */
static int call_kwin_load(KWinBridge *bridge)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(bridge->bus,
                               "org.kde.KWin",
                               "/Scripting",
                               "org.kde.kwin.Scripting",
                               "loadScript",
                               &error,
                               &reply,
                               "ss",
                               bridge->script_path,
                               bridge->script_name);
    if (r < 0) {
        fprintf(stderr, "ofumo: KWin loadScript failed: %s\n",
                error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return r;
    }

    int32_t script_id = -1;
    r = sd_bus_message_read(reply, "i", &script_id);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    if (r < 0 || script_id < 0) {
        fprintf(stderr, "ofumo: KWin returned an invalid script id\n");
        return r < 0 ? r : -EINVAL;
    }

    bridge->script_id = script_id;

    char object_path[128];
    int n = snprintf(object_path, sizeof(object_path),
                     "/Scripting/Script%d", bridge->script_id);
    if (n < 0 || (size_t)n >= sizeof(object_path)) return -ENAMETOOLONG;

    error = SD_BUS_ERROR_NULL;
    reply = NULL;
    r = sd_bus_call_method(bridge->bus,
                           "org.kde.KWin",
                           object_path,
                           "org.kde.kwin.Script",
                           "run",
                           &error,
                           &reply,
                           "");
    if (r < 0) {
        fprintf(stderr, "ofumo: KWin bridge script could not start: %s\n",
                error.message != NULL ? error.message : strerror(-r));
    }
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

/**
 * @brief Initialize the user D-Bus endpoint and KWin helper script.
 *
 * @param bridge Bridge instance to initialize.
 * @param verbose Enable diagnostic output when true.
 * @return true when cursor tracking is ready.
 */
bool kwin_bridge_init(KWinBridge *bridge, bool verbose)
{
    memset(bridge, 0, sizeof(*bridge));
    bridge->script_id = -1;
    bridge->verbose = verbose;

    int n = snprintf(bridge->service_name, sizeof(bridge->service_name),
                     "dev.schermaiolo.OFumo.p%ld", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(bridge->service_name)) return false;

    n = snprintf(bridge->script_name, sizeof(bridge->script_name),
                 "ofumo-bridge-%ld", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(bridge->script_name)) return false;

    int r = sd_bus_open_user(&bridge->bus);
    if (r < 0) {
        fprintf(stderr, "ofumo: cannot connect to the user D-Bus: %s\n", strerror(-r));
        return false;
    }

    r = sd_bus_request_name(bridge->bus, bridge->service_name, 0);
    if (r < 0) {
        fprintf(stderr, "ofumo: cannot acquire D-Bus name %s: %s\n",
                bridge->service_name, strerror(-r));
        kwin_bridge_destroy(bridge);
        return false;
    }

    r = sd_bus_add_object_vtable(bridge->bus,
                                 &bridge->slot,
                                 OFUMO_DBUS_PATH,
                                 OFUMO_DBUS_IFACE,
                                 bridge_vtable,
                                 bridge);
    if (r < 0) {
        fprintf(stderr, "ofumo: cannot register KWin bridge D-Bus object: %s\n",
                strerror(-r));
        kwin_bridge_destroy(bridge);
        return false;
    }

    if (!create_bridge_script(bridge)) {
        kwin_bridge_destroy(bridge);
        return false;
    }

    r = call_kwin_load(bridge);
    if (r < 0) {
        kwin_bridge_destroy(bridge);
        return false;
    }

    /* Prime the first movement tick with valid global Wayland coordinates. */
    for (int i = 0; i < 100 && !bridge->cursor_valid; ++i) {
        r = sd_bus_process(bridge->bus, NULL);
        if (r < 0) break;
        if (r == 0) {
            (void)sd_bus_wait(bridge->bus, 10000); /* 10 ms */
        }
    }

    if (!bridge->cursor_valid) {
        fprintf(stderr, "ofumo: KWin bridge loaded but did not provide a cursor position\n");
        kwin_bridge_destroy(bridge);
        return false;
    }

    if (bridge->verbose) {
        fprintf(stderr, "ofumo: backend=kwin-wayland cursor=(%d,%d)\n",
                bridge->cursor_x, bridge->cursor_y);
    }
    return true;
}

/**
 * @brief Drain pending bridge messages without blocking.
 *
 * @param bridge Initialized bridge instance.
 */
void kwin_bridge_process(KWinBridge *bridge)
{
    if (bridge == NULL || bridge->bus == NULL) return;

    for (;;) {
        int r = sd_bus_process(bridge->bus, NULL);
        if (r <= 0) break;
    }
}

/**
 * @brief Unload the KWin helper and release all D-Bus resources.
 *
 * @param bridge Bridge instance to clean up.
 */
void kwin_bridge_destroy(KWinBridge *bridge)
{
    if (bridge == NULL) return;

    if (bridge->bus != NULL && bridge->script_id >= 0) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = NULL;
        (void)sd_bus_call_method(bridge->bus,
                                 "org.kde.KWin",
                                 "/Scripting",
                                 "org.kde.kwin.Scripting",
                                 "unloadScript",
                                 &error,
                                 &reply,
                                 "s",
                                 bridge->script_name);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
    }

    if (bridge->script_path[0] != '\0') {
        unlink(bridge->script_path);
    }

    bridge->slot = sd_bus_slot_unref(bridge->slot);
    bridge->bus = sd_bus_flush_close_unref(bridge->bus);
    memset(bridge, 0, sizeof(*bridge));
    bridge->script_id = -1;
}
