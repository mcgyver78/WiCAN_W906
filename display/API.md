# Web interface of the display

The display serves one page (`GET /`) for arranging the views, storing WiFi networks, looking at the last
fault memory list and installing firmware. The page talks to the display with the requests below; the
browser never talks to the WiCAN adapter, only the display does.

The rules are implemented in plain C and tested on the host: `components/core/web_route.h` (which request
is what, and which is refused before a handler runs), `web_json.h` (the bodies), `access.h` (who may change
something), `app_web.h` (what each request does). The examples named here are hand-written files in
`test/fixtures/` that the tests compare byte for byte.

Status of this document: the logic is tested on the host. The HTTP server that carries it runs on the
device only and has not run anywhere yet.

**The web interface can neither read nor clear the fault memory of the vehicle.** That is done at the knob.

## Who may do what

- **Reading** is open to everybody in the same WiFi. No answer ever contains a WiFi password.
- **Changing** needs the *release*: menu "Web-Zugriff" at the display, switched on by hand. It ends 10
  minutes after it was given or after the last accepted change, at the latest 30 minutes after it was
  switched on, when it is switched off at the display, and with a restart. Without it: `403`
  `{"error":"locked","hint":"Am Display: Menü > Web-Zugriff freigeben"}`.
- Three changes also need a **press of the knob**, because they can lock the owner out or replace the
  firmware: storing a WiFi network, installing firmware, factory reset. The request is answered with
  `202 {"ticket":17,"hint":"Am Display bestätigen: Knopf drücken"}`, the display shows the question for 60
  seconds, and `GET /api/ticket?id=17` tells what became of it.
- Every request that is not a `GET` has to carry the header `X-Display: 1`. A web page of another origin
  cannot send it (the browser asks first, and that question is never answered). The display never sends an
  `Access-Control-Allow-*` header.
- The `Host` header has to be an IPv4 address or `wican-display.local` (optionally with a port). This keeps
  out pages whose name was bent to the address of the display.
- There is no login and no TLS. Whoever is in the WiFi while the release is open can change what the
  release allows. Keep the release closed when it is not needed.

## Requests

| Request | What it does | Release | Knob |
|---|---|---|---|
| `GET /` | the page | | |
| `GET /api/info` | version, memory, network, adapter, release, settings | | |
| `GET /api/catalog` | the values the adapter can deliver | | |
| `GET /api/values` | the current values, from the memory of the display | | |
| `GET /api/layout` | the views in use, as the layout text | | |
| `PUT /api/layout?mode=check` | checks a layout, changes nothing | | |
| `PUT /api/layout?mode=apply` | shows it on the display without storing it | yes | |
| `PUT /api/layout?mode=save` | stores it | yes | |
| `POST /api/layout/reset` | back to the built-in or generated views | yes | |
| `GET /api/dtc/last` | the last fault memory list and the one before the last clear | | |
| `GET /api/wifi` | stored and visible networks | | |
| `POST /api/wifi` | store a network | yes | yes |
| `POST /api/wifi/forget` | remove a stored network | yes | |
| `POST /api/settings` | brightness, night mode, direction of the knob, standby | yes | |
| `POST /api/reboot` | restart | yes | |
| `POST /api/reset` | factory reset: WiFi, binding and settings; the views stay | yes | yes |
| `POST /api/ota` | firmware upload | yes | yes, after the upload |
| `GET /api/ticket?id=N` | what became of a question to the knob | | |

Paths are exact: no trailing slash, no upper case. A query string is only allowed where the table shows one.

## Refusals

Checked in this order; the first that applies is the answer. The body is `{"error":"<word>"}`.

| Status | Word | When |
|---|---|---|
| 404 | `not_found` | unknown path |
| 405 | `method` | known path, other method |
| 403 | `host` | `Host` missing or not allowed |
| 403 | `header` | not a `GET` and `X-Display: 1` missing |
| 403 | `locked` | the request changes something and the release is closed |
| 400 | `query` | query string too long (63 bytes), `mode` or `id` missing, doubled or malformed, or a query where none belongs |
| 411 | `length` | not a `GET` and no `Content-Length` |
| 413 | `too_large` | body above 16384 bytes (layout), above the app slot (firmware), above 512 bytes (everything else); an empty firmware |
| 409 | `busy` | upload, restart or factory reset while the display reads or clears the fault memory, shows the clear dialog, or receives a firmware; a question to the knob while a firmware is received |
| 409 | `asking` | a question to the knob while another one still waits |
| 400 | `body` | the body is not what the request expects; for settings with `"member":"<name>"` |
| 422 | see below | the firmware file is refused |

## Bodies

Numbers, texts and their order are fixed; texts are UTF-8.

### `GET /api/info` — `web_info.json`

```json
{"project":"wican-display","version":"0.1.0","git":"display-v0.1.0-3-g1a2b3c4","slot":"ota_0",
 "reset":"poweron","up":4711,"safe_mode":false,"rolled_back":false,"update_pending":true,
 "heap":182340,"heap_min":151200,"psram":7340032,"psram_min":7100416,"temp_c":47,"heat":"normal",
 "release":{"open":true,"left_s":540},
 "wifi":{"ssid":"Werkstatt","ip":"192.168.1.77","rssi":-61,"ap":false,"ap_ssid":"WiCAN-Display"},
 "wican":{"host":"192.168.1.50","id":"a1b2c3d4e5f6","fw":"4.21","view":"live"},
 "layout":{"name":"W906 OM651 Standard","source":"stored"},
 "http":{"ok":12345,"failed":7,"reconnects":2},
 "settings":{"brightness":80,"night":25,"night_mode":false,"reverse":false,"standby_s":60}}
```

- `view`: what the display shows about the adapter: `live`, `no_wifi`, `connecting`, `no_answer`,
  `foreign`, `no_api`, `autopid_off`, `starting`, `scan`, `ecu_offline`.
- `layout.source`: `stored`, `builtin`, `generated` (made from the catalogue for a vehicle nobody wrote
  views for) or `preview` (applied but not stored).
- `heat`: `normal`, `dim`, `off` (backlight limited by the chip temperature).

### `GET /api/catalog`

```json
{"ENGINE_RPM":{"unit":"rpm","class":"frequency","profile":true,"delivered":true},
 "@BATT_V":{"unit":"V","class":"","profile":false,"delivered":false}}
```

Every value the vehicle profile of the adapter names (`profile`) or that has arrived (`delivered`), in the
order of the profile. `@BATT_V` is the battery voltage measured by the adapter. The last catalogue is kept
on the display, so the views can be edited while the adapter sleeps.

### `GET /api/values` — `web_values.json`

```json
{"view":"live","values":{"ENGINE_RPM":{"v":812.5,"age":"fresh"},"DPF_REGEN_STATUS":{"v":"on","age":"old"}}}
```

`v` is a number, `"on"` or `"off"`; `age` is `fresh` (under 3 s) or `old` (under 10 s). Older values are
left out.

### Layout — `GET /api/layout`, `PUT /api/layout?mode=...`

The format is described in `components/core/layout.h`; `layouts/w906_default.json` is the built-in one.
At most 12 pages with 1 to 6 values each, 16384 bytes. The answer to a `PUT`:

```json
{"ok":true,"name":"W906 OM651 Standard","pages":2,"items":5,"warnings":0,"warning_path":"","warning":"","unknown":[]}
{"ok":false,"path":"pages[2].items[0].min","problem":"min is not below max"}
```

Status 200 if the layout is taken, 400 if not. `unknown` lists the keys that are not in the catalogue
(they are shown as "n. v."); it is empty while no catalogue is loaded. `mode=check` needs no release.

### `GET /api/dtc/last` — `web_dtc_last.json`

```json
{"read":<result or null>,"read_age_s":120,"before_clear":<result or null>}
```

`read` is the list the display read itself and still shows, `before_clear` the list before the last clear
that the adapter accepted. Both are the result text of the adapter (`tools/w906/API.md`, W906.md).

### WiFi — `web_wifi.json`, `web_wifi_request.json`, `web_forget_request.json`

```json
{"current":"Werkstatt",
 "profiles":[{"ssid":"Werkstatt","host":"192.168.1.50","password":true,"factory":false,"wican_ap":false}],
 "seen":[{"ssid":"Freifunk","rssi":-88,"secure":false}]}
```

`password` tells whether one is stored, `factory` whether it is the password printed in the documentation
of the WiCAN, `wican_ap` whether the network is the access point of a WiCAN. Up to 4 networks are stored;
the order is the priority.

- `POST /api/wifi` with `{"ssid":"..","password":"..","host":".."}`. `host` is optional: empty means the
  display finds the adapter itself (the gateway in the access point of a WiCAN, else the service
  `_wican._tcp`). Without `password` the stored password of that SSID is kept; for a new SSID it means an
  open network. A password has 8 to 64 bytes. Answer: `202` with a ticket.
- `POST /api/wifi/forget` with `{"ssid":".."}`: `200 {"ok":true}`, `404` if no such network is stored.
  Without any stored network the display opens its own access point.

### `POST /api/settings` — `settings_*.json`

```json
{"brightness":80,"night":25,"night_mode":false,"reverse":false,"standby_s":60}
```

Every member is optional. `brightness` and `night` 5 to 100 percent, `standby_s` 0 to 3600 (0 = never):
after that long without input the backlight goes dark while there is nothing to show. Answer: the settings
now in use.

### Tickets — `web_ticket.json`, `web_asked.json`

```json
{"ticket":17,"state":"waiting","left_s":42}
```

`state`: `waiting`, `confirmed`, `refused` (at the display, or the release ended), `expired` (nobody
pressed the knob), `unknown`. The numbers start again at 1 after a restart of the display. After a
confirmed firmware installation or factory reset the display restarts and cannot answer any more.

### `POST /api/ota`

The body is the firmware file (`wican-display.bin`), `Content-Type: application/octet-stream`. The first
bytes are checked before anything is erased; a refused file is answered with `422` and one of
`too_short`, `no_image`, `wrong_chip`, `no_description`, `wrong_project` (for example the firmware of the
WiCAN), `too_large`. A broken upload ends with `500 {"error":"upload"}`; an upload that brings nothing for
30 seconds is ended by the display. A complete upload is answered with `202` and a ticket: the display
shows the version of the file and asks for the knob. Only the press starts the new firmware. After its
first start it asks "Update in Ordnung?" on the screen; without an answer within 5 minutes, or after any
restart before that, the version before it runs again.
