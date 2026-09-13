/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include "dtc_decode.h"

#define ISOTP_FRAME_BYTES	8

int dtc_isotp_next(const uint8_t *data, size_t len, size_t *pos, uint8_t *msg, size_t msg_size)
{
	while(*pos < len)
	{
		uint8_t pci = data[*pos];

		switch(pci & 0xF0)
		{
			case 0x00:
			{
				size_t length = pci & 0x0F;

				if(length == 0 || *pos + 1 + length > len || length > msg_size) return -1;

				memcpy(msg, &data[*pos + 1], length);
				*pos += 1 + length;
				return (int)length;
			}
			case 0x10:
			{
				if(*pos + ISOTP_FRAME_BYTES > len) return -1;

				size_t total = ((size_t)(pci & 0x0F) << 8) | data[*pos + 1];
				size_t copied = 0;

				for(size_t i = 2; i < ISOTP_FRAME_BYTES && copied < total && copied < msg_size; i++)
				{
					msg[copied++] = data[*pos + i];
				}
				*pos += ISOTP_FRAME_BYTES;

				while(copied < total && *pos + ISOTP_FRAME_BYTES <= len && (data[*pos] & 0xF0) == 0x20)
				{
					for(size_t i = 1; i < ISOTP_FRAME_BYTES && copied < total && copied < msg_size; i++)
					{
						msg[copied++] = data[*pos + i];
					}
					*pos += ISOTP_FRAME_BYTES;
				}
				return (int)copied;
			}
			default:
				// Stray consecutive or flow control frame
				*pos += ISOTP_FRAME_BYTES;
				break;
		}
	}
	return 0;
}

int dtc_find_response(const uint8_t *data, size_t len, uint8_t sid, uint8_t *msg, size_t msg_size)
{
	size_t pos = 0;
	int length;

	while((length = dtc_isotp_next(data, len, &pos, msg, msg_size)) > 0)
	{
		if(msg[0] == 0x7F && length >= 3)
		{
			// 0x78: response pending, the real response follows
			if(msg[2] == 0x78) continue;
			return -(int)msg[2];
		}
		if(msg[0] == sid) return length;
	}
	return 0;
}

void dtc_format_uds(const uint8_t *code, char *out, size_t out_size)
{
	static const char letters[] = "PCBU";

	snprintf(out, out_size, "%c%X%X%02X-%02X", letters[code[0] >> 6], (code[0] >> 4) & 0x03,
				code[0] & 0x0F, code[1], code[2]);
}

void dtc_format_kwp(const uint8_t *code, char *out, size_t out_size)
{
	snprintf(out, out_size, "%02X%02X", code[0], code[1]);
}
