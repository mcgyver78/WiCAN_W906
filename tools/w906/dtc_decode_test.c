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
	check(dtc_find_response(pending, sizeof(pending), 0x59, msg, sizeof(msg)) == 0, "pending without response");

	// Truncated data must not be read past the end
	const uint8_t truncated[] = {0x07, 0x59, 0x02};
	check(dtc_find_response(truncated, sizeof(truncated), 0x59, msg, sizeof(msg)) == 0, "truncated single frame");

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
