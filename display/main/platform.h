/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "app.h"

/*
 * The platform: what connects the logic in components/core (app.h, app_web.h) to the device - tasks, the
 * WiFi driver, HTTP in both directions, the flash, the screen. It decides nothing: it passes on what
 * happened and carries out what the app asks for. Everything that can be decided is in the core, where it
 * is tested on the host; this layer can only be built, not tested, until the board is there.
 *
 * Three tasks and the HTTP server of ESP-IDF:
 *   screen   (screen.c)  reads knob, switch and touch every 20 ms, ticks the app five times a second,
 *                        draws the scene, sets the backlight; the only task that touches LVGL
 *   network  (net.c)     drives the WiFi by app->link and talks to the adapter by app->poll
 *   httpd    (web.c)     the web interface of the display: web_route() and app_web.h, the page, the upload
 *   main     (main.c)    starts everything, and carries out the events of the app
 *
 * One lock (app.h): platform_lock() around every call into the app and every read of it. Nothing that
 * blocks happens under the lock - no HTTP request, no scan, no write to the flash, no drawing.
 *
 * Events: whoever called into the app calls platform_events() BEFORE it gives the lock back. That takes
 * the events of the app and copies what they name (settings, networks, layout text, ...) into a queue of
 * the main task, which writes to the flash and restarts - in the order the events came, storing first.
 * So an event always stores what was in the app when it was raised.
 */

extern app_t *platform_app;         // allocated once in the external RAM by main.c, never freed

void platform_lock(void);
void platform_unlock(void);

// Milliseconds since the start, from the one clock every task uses (esp_timer). Never runs backwards.
uint64_t platform_now_ms(void);

// Takes the events of the app and hands them, with copies of what they name, to the main task. Called
// with the lock held, after every call that can raise one. Never blocks: if the queue is full the events
// stay with the app and are taken with the next call.
void platform_events(void);

// What the tasks know about the device, for the info page. Each task fills its part under the lock; the
// main task passes the whole to app_platform() about once a second.
typedef struct
{
	char ssid[33];              // net.c: network the display is in, empty if in none
	char ip[40];
	int rssi;
	char ap_ssid[33];           // net.c: own access point
	char ap_password[65];
	uint32_t reconnects;
	char slot[16];              // main.c: running app slot
	char reset[24];             // main.c: reason of the last reset as a word
} platform_info_t;

extern platform_info_t platform_info;

// screen.c: attaches LVGL to the panel, builds the user interface and starts the screen task
esp_err_t screen_start(esp_lcd_panel_handle_t panel);

// net.c: starts the WiFi driver and the network task. ap_ssid and ap_password: the own access point
// (name from the chip id, password made once and stored by main.c)
esp_err_t net_start(const char *ap_ssid, const char *ap_password);

// web.c: starts the HTTP server of the display
esp_err_t web_start(void);

// web.c -> main.c: the firmware that was uploaded into the other slot is complete and its check sum is
// right. The main task makes it the one to boot when APP_EVENT_INSTALL_FIRMWARE comes.
// main.c also has to remember over a restart that the other slot is no "previous version" any more as
// soon as an upload begins (app_web.h): platform_upload_begun() is called by web.c before it erases.
void platform_upload_begun(void);
void platform_upload_complete(void);

#endif
