/*
 * Host test for main/dtc_decode.c with byte streams as the WiCAN AutoPID parser
 * produces them from real Sprinter W906 responses:
 *   cc -I../../main ../../main/dtc_decode.c dtc_decode_test.c -o dtc_decode_test && ./dtc_decode_test
 */
#include <stdio.h>
#include <string.h>
#include "dtc_decode.h"

static int failures = 0;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if(!condition) failures++;
}

int main(void)
{
	uint8_t msg[256];
	char code[16];

	// 7E8: 7F 19 78 (pending), then 59 02 FF 24 2F FA 68
	const uint8_t engine[] = {0x03, 0x7F, 0x19, 0x78, 0x07, 0x59, 0x02, 0xFF, 0x24, 0x2F, 0xFA, 0x68};
	int n = dtc_find_response(engine, sizeof(engine), 0x59, msg, sizeof(msg));
	check(n == 7 && msg[3] == 0x24 && msg[6] == 0x68, "UDS response after pending");
	dtc_format_uds(&msg[3], code, sizeof(code));
	check(strcmp(code, "P242F-FA") == 0, "UDS code P242F-FA");

	// 48B: multi frame 59 02 FB 52 04 00 28 77 10 00 28
	const uint8_t cpa[] = {0x10, 0x0B, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00,
	                       0x21, 0x28, 0x77, 0x10, 0x00, 0x28, 0xAA, 0xAA};
	n = dtc_find_response(cpa, sizeof(cpa), 0x59, msg, sizeof(msg));
	check(n == 11, "UDS multi frame length");
	dtc_format_uds(&msg[3], code, sizeof(code));
	check(strcmp(code, "C1204-00") == 0, "UDS code C1204-00");
	dtc_format_uds(&msg[7], code, sizeof(code));
	check(strcmp(code, "C3710-00") == 0, "UDS code C3710-00");

	// 4F6: 59 02 39 00 C2 64 28
	const uint8_t radio[] = {0x07, 0x59, 0x02, 0x39, 0x00, 0xC2, 0x64, 0x28};
	n = dtc_find_response(radio, sizeof(radio), 0x59, msg, sizeof(msg));
	dtc_format_uds(&msg[3], code, sizeof(code));
	check(n == 7 && strcmp(code, "P00C2-64") == 0, "UDS code P00C2-64");

	// 4F1 KWP: 7F 18 78, then 58 01 93 01 20
	const uint8_t climate[] = {0x03, 0x7F, 0x18, 0x78, 0x05, 0x58, 0x01, 0x93, 0x01, 0x20};
	n = dtc_find_response(climate, sizeof(climate), 0x58, msg, sizeof(msg));
	check(n == 5 && msg[1] == 1, "KWP response with one DTC");
	dtc_format_kwp(&msg[2], code, sizeof(code));
	check(strcmp(code, "9301") == 0 && msg[4] == 0x20, "KWP code 9301 status 20");

	// No DTCs and clear confirmations
	const uint8_t empty[] = {0x03, 0x59, 0x02, 0x0C};
	check(dtc_find_response(empty, sizeof(empty), 0x59, msg, sizeof(msg)) == 3, "UDS without DTC");
	const uint8_t cleared[] = {0x03, 0x7F, 0x14, 0x78, 0x01, 0x54};
	check(dtc_find_response(cleared, sizeof(cleared), 0x54, msg, sizeof(msg)) == 1, "UDS clear after pending");
	const uint8_t kwp_cleared[] = {0x03, 0x54, 0xFF, 0x00};
	check(dtc_find_response(kwp_cleared, sizeof(kwp_cleared), 0x54, msg, sizeof(msg)) == 3, "KWP clear");

	// Negative response and pending only
	const uint8_t negative[] = {0x03, 0x7F, 0x19, 0x22};
	check(dtc_find_response(negative, sizeof(negative), 0x59, msg, sizeof(msg)) == -0x22, "negative response NRC 22");
	const uint8_t pending[] = {0x03, 0x7F, 0x19, 0x78};
	check(dtc_find_response(pending, sizeof(pending), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_PENDING,
	      "pending without response");

	// A late negative response to the previous request belongs to another service
	const uint8_t late_nrc[] = {0x03, 0x7F, 0x14, 0x22, 0x07, 0x59, 0x02, 0xFF, 0x24, 0x2F, 0xFA, 0x68};
	check(dtc_find_response(late_nrc, sizeof(late_nrc), 0x59, msg, sizeof(msg)) == 7,
	      "negative response to another service skipped");
	check(dtc_find_response(late_nrc, 4, 0x59, msg, sizeof(msg)) == 0, "only a negative response to another service");

	// Stray consecutive frame before the response
	const uint8_t stray[] = {0x21, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x03, 0x59, 0x02, 0x0C};
	check(dtc_find_response(stray, sizeof(stray), 0x59, msg, sizeof(msg)) == 3, "stray consecutive frame skipped");

	// Truncated or invalid data must not be read past the end and is reported as incomplete
	const uint8_t truncated[] = {0x07, 0x59, 0x02};
	check(dtc_find_response(truncated, sizeof(truncated), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "truncated single frame");
	const uint8_t invalid_single[] = {0x08, 0x59, 0x02, 0xFF, 0x24, 0x2F, 0xFA, 0x68};
	check(dtc_find_response(invalid_single, sizeof(invalid_single), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "single frame length 8");

	// Multi frame responses have to be complete and in order: 59 02 FB 52 04 00 28 77 10 00 28 C3 71 00 28 P2 42 F0
	const uint8_t first_only[] = {0x10, 0x12, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00};
	check(dtc_find_response(first_only, sizeof(first_only), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "first frame without consecutive frames");
	const uint8_t cut_off[] = {0x10, 0x12, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00,
	                           0x21, 0x28, 0x77, 0x10, 0x00, 0x28, 0xC3, 0x71};
	check(dtc_find_response(cut_off, sizeof(cut_off), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "last consecutive frame missing");
	const uint8_t missing[] = {0x10, 0x12, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00,
	                           0x22, 0x00, 0x28, 0x24, 0x2F, 0x00, 0xAA, 0xAA};
	check(dtc_find_response(missing, sizeof(missing), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "first consecutive frame missing");
	const uint8_t swapped[] = {0x10, 0x12, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00,
	                           0x22, 0x00, 0x28, 0x24, 0x2F, 0x00, 0xAA, 0xAA,
	                           0x21, 0x28, 0x77, 0x10, 0x00, 0x28, 0xC3, 0x71};
	check(dtc_find_response(swapped, sizeof(swapped), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "consecutive frames swapped");
	const uint8_t short_first[] = {0x10, 0x05, 0x59, 0x02, 0xFB, 0x52, 0x04, 0x00};
	check(dtc_find_response(short_first, sizeof(short_first), 0x59, msg, sizeof(msg)) == DTC_RESPONSE_INCOMPLETE,
	      "first frame shorter than 8 bytes");

	// 17 consecutive frames: the sequence number wraps from F to 0
	uint8_t stream[8 * 18];
	uint8_t expected[6 + 7 * 17];
	size_t total = sizeof(expected);

	for(size_t i = 0; i < total; i++) expected[i] = (uint8_t)i;
	expected[0] = 0x59;
	stream[0] = 0x10 | (uint8_t)(total >> 8);
	stream[1] = (uint8_t)total;
	memcpy(&stream[2], expected, 6);
	for(size_t frame = 0; frame < 17; frame++)
	{
		stream[8 + 8 * frame] = 0x20 | (uint8_t)((frame + 1) & 0x0F);
		memcpy(&stream[9 + 8 * frame], &expected[6 + 7 * frame], 7);
	}
	n = dtc_find_response(stream, sizeof(stream), 0x59, msg, sizeof(msg));
	check(n == (int)total && memcmp(msg, expected, total) == 0, "sequence number wraps after 15 consecutive frames");
	check(dtc_find_response(stream, sizeof(stream), 0x59, msg, 100) == DTC_RESPONSE_INCOMPLETE,
	      "message larger than the buffer");

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
