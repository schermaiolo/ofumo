/*
* File     : kwin_bridge.h
* Platform : Linux / KDE Plasma Wayland
* Author   : Daniel Fridman (Schermaiolo)
*
* KWin bridge interface used by OFumo to obtain global pointer and active
* window geometry on Plasma Wayland sessions.
*/

#ifndef OFUMO_KWIN_BRIDGE_H
#define OFUMO_KWIN_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include <systemd/sd-bus.h>

/**
 * @brief Runtime state for the temporary KWin-to-OFumo D-Bus bridge.
 */
typedef struct KWinBridge {
    sd_bus *bus;
    sd_bus_slot *slot;

    char service_name[128];
    char script_name[128];
    char script_path[256];
    int script_id;

    bool verbose;
    bool cursor_valid;
    int cursor_x;
    int cursor_y;

    bool active_valid;
    int active_x;
    int active_y;
    int active_width;
    int active_height;
    uint64_t active_generation;
} KWinBridge;

/**
 * @brief Initialize the KWin bridge and obtain the initial global cursor state.
 *
 * @param bridge Bridge instance to initialize.
 * @param verbose Enable diagnostic output when true.
 * @return true on success, false when KWin/D-Bus setup fails.
 */
bool kwin_bridge_init(KWinBridge *bridge, bool verbose);

/**
 * @brief Process pending D-Bus messages without blocking.
 *
 * @param bridge Initialized bridge instance.
 */
void kwin_bridge_process(KWinBridge *bridge);

/**
 * @brief Unload the temporary KWin script and release bridge resources.
 *
 * @param bridge Bridge instance to destroy.
 */
void kwin_bridge_destroy(KWinBridge *bridge);

#endif
