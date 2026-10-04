# HTTP API for standalone clients (version 1)

The W906 fork of the WiCAN firmware answers these requests next to the MQTT interface. They let a client
without MQTT broker, for example a display, read the state of the adapter and read or clear the fault
memory. The MQTT commands `read_dtc` / `clear_dtc` and their topic keep working as before, with two more
reasons of an error message (`not_ready`, `expired`, see below); a scan started one way is visible the other way.

This file is the contract. `main/dtc_api.c` (answers), `main/dtc_state.c` (rules of a scan),
`tools/w906/mock_wican.py` (a stand-in for the adapter) and the tests of a client are written against it.
Examples in `tools/w906/fixtures/` are compared byte for byte.

All answers are JSON in UTF-8 without whitespace, with `Cache-Control: no-store`. No CORS headers are sent.
All numbers are integers below 2^31, except `batt_v`.

## GET /api/state

Always `200`. A `404` means the firmware does not have this API.

```json
{"api":1,"id":"a1b2c3d4e5f6","fw":"4.21","git":"w906-v1.4.0-9-g0123abc","boot":1234567890,"up":812,"autopid":"run","pids":35,"ecu":"online","pass":1234,"rx_age_ms":140,"mqtt":"connected","batt_v":12.4,"sleep_in_s":-1,"heap":61000,"heap_min":48000,"dtc":{"supported":true,"state":"idle","action":"","src":"","seq":0,"ecu":0,"total":0,"name":"","reason":"","age_s":0,"count":0,"result_seq":0}}
```

The fields come in exactly this order.

| Field | Meaning |
|---|---|
| `api` | 1 |
| `id` | device id, the same as in the access point name `WiCAN_<id>` |
| `fw`, `git` | version text of the application image (at most 31 bytes) and `git describe` of the build. The builds of this repository have no version number of their own: `fw` is then the same text as `git`, not a number like the `4.21` of the example |
| `boot` | random number chosen at boot, 1..2^31-1. A different value means the adapter restarted: sequence numbers start anew and the stored result is gone |
| `up` | seconds since boot |
| `autopid` | `off` (the AutoPID task does not exist: the protocol is not AutoPID, no PID is configured or the start failed), `starting` (task not in its loop yet), `run`. Without a vehicle profile but with other PIDs it is `run`, and `dtc.supported` is `false` |
| `pids` | number of values of the vehicle profile |
| `ecu` | `online` or `offline` (ignition off or control unit not answering) |
| `pass` | counter, incremented after every round of the polling loop in which at least one request was answered. A round asks only the values that are due, so the counter moves several times a second and does not count whole passes over all values (measured 2026-10-04 on a W906 with 35 values: about 14 steps per second, each single value is renewed about every 2.6 s). Values of `/autopid_data` are fresh only if this counter moved; it does not tell which of them were renewed |
| `rx_age_ms` | milliseconds since the last answered request, `-1` if none since boot, at most 2^31-1. The adapter counts it in 32 bits: 2^32 ms (49.7 days) after the last answer it starts again at 0, `ecu` is `offline` by then |
| `mqtt` | `off`, `connected`, `disconnected` |
| `batt_v` | battery voltage with one decimal, `-1` if not measured |
| `sleep_in_s` | whole seconds until the adapter goes to sleep, the rest cut off: `0` from the last second on. `-1` if it is not counting down. At `0` it is due to sleep and starts no new scan, see "Sleep" |
| `heap`, `heap_min` | free heap now and its minimum since boot, in bytes |
| `dtc` | state of the fault memory scan, see below |

Text fields are escaped like JSON strings (`"` and `\`), control characters below 0x20 are dropped.

### dtc

Written by `dtc_state_json()`; see `main/dtc_state.h` for the rules and `fixtures/dtc_state_*.json`.

| Field | Meaning |
|---|---|
| `supported` | `false` if the loaded vehicle profile has no fault memory table (only W906 has one) |
| `state` | `idle` (nothing requested since boot), `queued`, `running`, `done`, `error` |
| `action`, `src` | `read` / `clear` and `mqtt` / `http` of the last accepted request, empty while idle |
| `seq` | sequence number of the last accepted request, 0 = none |
| `ecu`, `total` | step of a running scan: 0 = engine check, 1..total = control unit being processed. Both are 0 while idle or queued: a client must not divide by `total` unchecked |
| `name` | control unit being processed, empty unless running |
| `reason` | of an error: `ecu_offline`, `engine_running`, `engine_state_unknown`, `not_supported`, `out_of_memory`, `result_serialize_failed`, `expired`, `internal` |
| `age_s` | seconds since `done` or `error`, else 0 |
| `count` | trouble codes in the stored result |
| `result_seq` | sequence number the stored result belongs to, 0 = no result |

## POST /api/dtc?action=read
## POST /api/dtc?action=clear&seq=N

Starts a scan. There is no body. The answer comes at once, the scan takes about 35 s; the client follows it
with `GET /api/state` and fetches the result when `state` is `done` and `result_seq` equals the `seq` it got.

Required: the request header `X-WiCAN-DTC: 1`, and a `Host` header that is an IPv4 address or a name of the
form `wican_<id>.local`, each optionally with a port. A web page in a browser cannot send such a request to the
adapter (no CORS answer for the custom header, no foreign host name).

`seq` is the number of the read whose list is to be cleared: 1 to 10 decimal digits, 1..2147483647.
It is required for `clear` and ignored for `read`. Parameters are separated by `&`, the first `action` and
the first `seq` count, nothing is percent-decoded, and the query may be at most 63 bytes long. A longer query is
answered like a missing one, with `400 bad_request`.

| Status | Body | When |
|---|---|---|
| 202 | `{"accepted":true,"seq":43}` | accepted, 43 is the number of the new request |
| 409 | `{"accepted":false,"reason":"busy","seq":42}` | a scan is queued or running; 42 is its number |
| 409 | `{"accepted":false,"reason":"read_required","seq":42}` | clear, but the last request is not a read that finished with a result at most 600 s ago |
| 409 | `{"accepted":false,"reason":"stale_seq","seq":42}` | clear refers to another read than the last one |
| 409 | `{"accepted":false,"reason":"nothing_to_clear","seq":42}` | the last read found no trouble codes |
| 503 | `{"accepted":false,"reason":"not_ready","seq":0}` | `autopid` is not `run`, or `sleep_in_s` is `0` |
| 403 | `{"accepted":false,"reason":"forbidden","seq":0}` | header missing or not `1`, or host not allowed |
| 400 | `{"accepted":false,"reason":"bad_request","seq":0}` | `action` missing or unknown, `seq` missing or malformed for a clear |

The checks are made in this order: forbidden, bad_request, not_ready, then the rules of the scan state.
A request is never repeated by the firmware and must not be repeated blindly by a client: after a timeout
the client looks at `GET /api/state` to see whether it was accepted.

An accepted request waits as `queued` until the AutoPID task looks at it, which it does between two polling
passes. If more than 20 s have passed by then, the request ends as `error` with reason `expired` and does not
run. The 20 s are not watched by a timer: until the task looks at the request, the state stays `queued` and
further requests are answered `busy`, also later than 20 s after the acceptance.

Rejections of an HTTP request are not published on MQTT. Its expiry is, as an error with reason `expired`:
an MQTT command may have been answered `busy` because of the request, and `busy` is always followed by a
final message on the topic. A scan that actually runs publishes its progress, errors and the retained result
on the MQTT topic as before, whoever started it.

## GET /api/dtc/result

| Status | Body | When |
|---|---|---|
| 200 | the stored result, the same text as the retained MQTT message, with the header `X-DTC-Seq: 43` | a scan has finished since boot |
| 204 | empty | no result stored |
| 503 | `{"accepted":false,"reason":"not_ready","seq":0}` | `autopid` is not `run`, or no memory for the copy |

The result is kept in RAM until the next scan finishes or the adapter restarts. It survives a later error.
For the format see W906.md, section "Fehlerspeicher", "Lesen und Löschen mit dem WiCAN".

## Sleep

When `sleep_in_s` has reached `0` the adapter goes to sleep: CAN and WiFi are switched off, and it wakes up
with a restart (`boot` changes). The stored result is gone then; of a scan published over MQTT the retained
message stays on the broker.

For a scan the adapter stays awake beyond that moment:

- while a request is queued or running, at most 120 s from its acceptance,
- and for 10 s after it ended (`done` or `error`), so that the last MQTT message leaves and a client sees the
  end and fetches the result,
- in any case no longer than 150 s after sleep was due.

While `sleep_in_s` is `0` no new request is accepted (`503 not_ready`, over MQTT an error with reason
`not_ready`): it could not finish. That holds while the adapter stays awake for a scan as well; the answer is
then `not_ready`, not `busy`. `not_ready` has two causes, and `GET /api/state` says which: `autopid` is not
`run` (the adapter is starting, or AutoPID is off), or `sleep_in_s` is `0` (it is about to switch off). A client
fetches the result as soon as it sees `done`; after a sleep it is gone.

## Other requests a client may use (unchanged upstream endpoints)

- `GET /autopid_data`: the current values as a flat JSON object. `{}` while no value is valid.
- `GET /load_car_config`: `{"NAME":{"class":"…","unit":"…"}, …}` for every value of the profile, also with
  the ignition off. It waits for the end of the current polling pass.

While `autopid` is `off` these two do not answer like that: `/autopid_data` gives `200` with
`{"error":"No data available"}` and `/load_car_config` gives `500` with a text. A client looks at `autopid`
in `/api/state` first.

The API expects a vehicle profile with `autopid_polling` enabled, as the W906 upload file has it. With polling
disabled every `GET /autopid_data` starts a polling pass and waits for its end, with the ignition off that
takes many seconds, and no other request is answered meanwhile. Only while a scan is queued or running it
returns the stored values at once.

A client should use one connection, ask about once a second and never call `/check_status`,
`/load_config` or `/scan_available_pids`.

The adapter may close a connection at any time: it drops a client that no longer answers (TCP keep-alive,
after about 25 s), and its least recently used connection when a new client finds none free. After an answer
of the HTTP server itself it always closes the connection, without saying so in a header: `404` for a path it
does not know, `405` with the text `Specified method is invalid for this resource` for a known path with
another method (also HEAD, which gets the text as a body). A client opens a new connection for its next
request. A request that got no answer may be sent again if it was a GET, never if it was a POST: whether a
POST was accepted is in `GET /api/state`.
