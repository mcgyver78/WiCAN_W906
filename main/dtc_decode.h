/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_DECODE_H__
#define __DTC_DECODE_H__

#include <stdint.h>
#include <stddef.h>

/*
 * The ELM327 emulator prints every received CAN frame as PCI byte plus data
 * (single frame: N bytes, first/consecutive frame: 7 bytes) and the AutoPID
 * parser concatenates all frames of one request. These helpers rebuild the
 * diagnostic messages from that byte stream and decode trouble codes.
 */

// Next reassembled message from the byte stream. Returns its length, 0 at the end, -1 on malformed or
// incomplete data (missing, repeated or reordered consecutive frame, message larger than msg_size).
int dtc_isotp_next(const uint8_t *data, size_t len, size_t *pos, uint8_t *msg, size_t msg_size);

#define DTC_RESPONSE_INCOMPLETE     (-256)
#define DTC_RESPONSE_PENDING        (-257)

// Positive response with service id `sid`. Returns its length, 0 without response, -NRC for a negative
// response to this service, DTC_RESPONSE_PENDING if the control unit only answered "response pending"
// (7F xx 78) or DTC_RESPONSE_INCOMPLETE if the response could not be reassembled completely.
int dtc_find_response(const uint8_t *data, size_t len, uint8_t sid, uint8_t *msg, size_t msg_size);

// UDS 3 byte DTC, e.g. 24 2F FA -> "P242F-FA"
void dtc_format_uds(const uint8_t *code, char *out, size_t out_size);

// KWP2000 2 byte DTC, e.g. 93 01 -> "9301"
void dtc_format_kwp(const uint8_t *code, char *out, size_t out_size);

#endif
