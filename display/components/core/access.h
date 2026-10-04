/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __ACCESS_H__
#define __ACCESS_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Who may change something on the display through its web interface.
 *
 * Reading is open to everybody in the same WiFi. A change needs the release given at the device (menu
 * "Web-Zugriff freigeben"): somebody has to sit in front of the display. The release ends ACCESS_OPEN_MS
 * after it was given or after the last accepted change, whichever is later, when it is switched off at
 * the device, and with a restart (this struct is never stored).
 *
 * Three changes need more than the release, because they can lock the owner out or replace the firmware:
 * new WiFi data, installing firmware, and the factory reset. The browser asks, the display shows what is
 * asked, and only a press of the knob within ACCESS_CONFIRM_MS carries it out. The browser gets a ticket
 * number and asks what became of it.
 *
 * Time is milliseconds since boot. A time before one passed earlier counts as no time passed. Nothing lasts
 * beyond the largest time, 2^64-1: what would end later ends there.
 */

#define ACCESS_OPEN_MS      (600u * 1000u)
#define ACCESS_CONFIRM_MS   (60u * 1000u)

typedef enum
{
	ACCESS_ASK_NONE,
	ACCESS_ASK_WIFI,        // store WiFi data
	ACCESS_ASK_FIRMWARE,    // start the uploaded firmware
	ACCESS_ASK_RESET,       // factory reset
} access_ask_t;

typedef enum
{
	ACCESS_TICKET_UNKNOWN,      // no such ticket (never given, or two newer ones were given since)
	ACCESS_TICKET_WAITING,
	ACCESS_TICKET_CONFIRMED,
	ACCESS_TICKET_REFUSED,      // refused at the device, or the release was switched off or ended while it waited
	ACCESS_TICKET_EXPIRED,      // nobody pressed the knob in time
} access_ticket_t;

// The fields are as the last call that changed them left them: what ran out since then still stands here.
// Read them through the functions below, which take the time into account.
typedef struct
{
	bool open;
	uint64_t open_until_ms;
	uint64_t clock_ms;          // the latest time seen: it follows the calls but never runs backwards. Every
	                            // function that gets this struct to change takes its time over, also when
	                            // it refuses; the ones that only ask cannot store theirs
	access_ask_t asking;        // what waits for the knob, ACCESS_ASK_NONE if nothing
	uint64_t asking_until_ms;
	uint32_t ticket;            // number of the last ticket given, 0 = none yet
	access_ticket_t ticket_end; // how the last ticket ended, ACCESS_TICKET_WAITING while it waits
	access_ticket_t previous_end;   // how the ticket before the last one ended (ACCESS_TICKET_UNKNOWN if
	                                // there was none): a browser that asks late still gets its answer
} access_t;

// Closed, nothing asked, no ticket
void access_init(access_t *access);

// The release was switched on at the device (again: the time starts anew)
void access_open(access_t *access, uint64_t now_ms);

// The release was switched off at the device. A question that waits is refused.
void access_close(access_t *access, uint64_t now_ms);

// true while the release holds. It ends by itself at open_until_ms; a question that waits then expires
// with it (ACCESS_TICKET_REFUSED: the release it was asked under is gone). A question whose own time is
// over by then does not wait any more, also if both run out in the same millisecond: it stays
// ACCESS_TICKET_EXPIRED. With the two times above that is always so, because an accepted question renews
// the release; the rule is kept for other times.
bool access_is_open(const access_t *access, uint64_t now_ms);

// Seconds until the release ends, rounded up, 0 if it is closed
uint32_t access_seconds_left(const access_t *access, uint64_t now_ms);

// A change arrives. Returns true if it is allowed; the release then lasts ACCESS_OPEN_MS from now.
// Returns false if the release is closed or has ended: nothing changes then, only the time is taken over
// and what ran out by then is noted in the fields.
bool access_write(access_t *access, uint64_t now_ms);

// A change that needs the knob arrives. Returns the ticket number (1 or more, counting up, never 0; after
// 2^32-1 comes 1), or 0 if it is refused: the release is closed or has ended, `ask` is ACCESS_ASK_NONE or
// no value of the enum, or another question still waits (the first one stays). An accepted question counts
// as a change: the release lasts ACCESS_OPEN_MS from now.
uint32_t access_ask(access_t *access, access_ask_t ask, uint64_t now_ms);

// What the display has to show: the question that waits, ACCESS_ASK_NONE if none waits or its time is over
access_ask_t access_asking(const access_t *access, uint64_t now_ms);

// Seconds left to press the knob, rounded up, 0 if nothing waits
uint32_t access_ask_seconds_left(const access_t *access, uint64_t now_ms);

// The knob was pressed while the question was shown. Returns the question that is confirmed by it, exactly
// once: the caller carries it out. ACCESS_ASK_NONE if nothing waits, its time is over or the release ended.
// The press is no change through the web: the release is neither renewed nor ended by it.
access_ask_t access_confirm(access_t *access, uint64_t now_ms);

// The question was refused at the device (long press, or the dialog was left). Nothing happens if none waits.
// The release goes on as it was.
void access_refuse(access_t *access, uint64_t now_ms);

// What became of a ticket, for the browser. Known are the last ticket and the one before it. The numbers
// begin again with 1 after a restart: a number from before it is then unknown or the number of a newer ticket.
access_ticket_t access_ticket(const access_t *access, uint32_t ticket, uint64_t now_ms);

#endif
