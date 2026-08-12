# lan-controller-esp32

ESP32 firmware built with **ESP-IDF v6.0.2**. Connects to Wi-Fi, serves an
embedded web dashboard with real-time device status and Wake-on-LAN
configuration (WIP), and exposes a JSON REST API. Credentials are read from
an embedded `.env` file — no recompile needed for Wi-Fi or port changes.

> `doc/README.md` — Doxygen API documentation setup and local preview.

## Screenshot

```
 ┌──────────┬─────────────────────────────────────────┐
 │ ESP32 WoL│ Device Status                            │
 │ LAN Ctrl │                                          │
 │          │ ┌─ Network ────────────────────────────┐ │
 │ ■ Status │ │ Wi-Fi    Connected      LAN IP  .1.9 │ │
 │ ★ WoL Cfg│ │ MAC     3C:8A:1F:A3:D2:74            │ │
 │          │ └──────────────────────────────────────┘ │
 │          │ ┌─ Memory ─────────────────────────────┐ │
 │          │ │ Heap Free 212 kB    Heap Total 320 kB │ │
 │          │ │ Min Free  198 kB    Free Stack  12 kB │ │
 │          │ │ Tasks   14           Uptime  2m 34s   │ │
 │          │ └──────────────────────────────────────┘ │
 │          │ ┌─ Metrics (2×2 uPlot charts) ────────┐  │
 │          │ │ Heap Free/Min     Wi-Fi RSSI -55 dBm│  │
 │          │ │ Tasks    10       Free Stack 2.1 kB │  │
 │          │ └──────────────────────────────────────┘ │
 │          │ ┌─ Device ─┐ ┌─ Firmware ─────────────┐ │
 │  .1.9    │ │ ESP32    │ │ lan-controller-esp32 │ │
 │          │ │ 2 cores  │ │ 55a178f-dirty           │ │
 └──────────┴─────────────────────────────────────────┘
```

## Hardware

- **Target**: ESP32 (xtensa)
- **Flash**: 2 MB (single factory partition)
- **Framework**: ESP-IDF v6.0.2

## Project structure

```
├── CMakeLists.txt
├── Doxyfile               # Doxygen config — docs in doc/README.md
├── sdkconfig
├── managed_components/    # (gitignored)
├── dependencies.lock
├── doc/
│   ├── README.md          # Doxygen build/deploy guide
│   └── mainpage.md        # Doxygen landing page content
├── .github/
│   └── workflows/
│       └── docs.yml       # CI: build + deploy docs to GitHub Pages
└── main/
    ├── CMakeLists.txt     # EMBED_FILES + objcopy for .env
    ├── app.c              # Entry point
    ├── .env.example       # Copy to .env and fill in credentials
    ├── .env               # Wi-Fi credentials (gitignored)
    ├── web/
    │   ├── index.html     # Sidebar + tabbed dashboard
    │   ├── uplot.min.js   # Vendored uPlot 1.6.32 (51 KB, MIT)
    │   ├── uplot.min.css  # Vendored uPlot base styles (2 KB)
    │   ├── css/
    │   │   ├── layout.css      # Reset, variables, sidebar, bottombar, breakpoints
    │   │   └── components.css  # Cards, forms, chart grid, error, placeholders
    │   └── js/
    │       ├── formatters.js   # fmtBytes, fmtUptime, fmtFreq, fmtRssi
    │       ├── metrics.js      # METRICS registry, CHARTS definitions, constants
    │       ├── store.js        # Ring buffer, pushSample, load/saveHistory (debounced)
    │       ├── charts.js       # uPlot init, setData, resize — 4-chart 2×2 grid
    │       └── dashboard.js    # Token auth, status polling, tab switching, DOM init
    └── utils/
        ├── dotenv.h / .c  # Embedded .env parser
        ├── wifi_connect.h/c # Wi-Fi STA lifecycle
        ├── web_util.h / .c  # HTTP server lifecycle
        ├── web_API.h / .c # REST endpoints + cJSON
        └── wol.h / .c       # Wake-on-LAN magic packet
    └── test/
        ├── README           # Unit test guide
        ├── test_dotenv.c    # 7 dotenv parser tests
        ├── test_wol.c       # 14 WoL validation tests
        └── test_web_API.c   # 5 chip model mapping tests
```

## Quick start

### 1. Configure

```bash
cp main/.env.example main/.env
```

Edit `main/.env`:

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `SSID_WIFI` | yes | — | Wi-Fi SSID |
| `PASSWD_WIFI` | yes | — | Wi-Fi password |
| `APP_NAME` | no | `ESP32_WoL` | Logged at boot |
| `WEB_PORT` | no | `38429` | HTTPS port |
| `WEB_API_TOKEN` | yes | — | API key required on all `/api/*` routes (`X-API-Key`) |

### 2. Generate SSL Certificate (one-time)

The HTTPS server requires a self-signed certificate:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout main/server.key \
  -out main/server.crt \
  -days 3650 \
  -subj "/CN=nexus-coffee.duckdns.org"
```

Replace `nexus-coffee.duckdns.org` with your domain or IP. Certificates are gitignored.

### 3. Build & flash

```bash
idf.py set-target esp32
idf.py fullclean  # Required after adding certificates
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

> **Note:** Browsers will show a certificate warning (`NET::ERR_CERT_AUTHORITY_INVALID`) because the certificate is self-signed. Accept it to proceed.

## Web interface

The dashboard has a left sidebar with two tabs:

### Status

Displays real-time ESP32 metadata fetched from `/api/status` every 5 seconds:

| Section | Fields |
|---------|--------|
| Network | Wi-Fi state, LAN IP, MAC address |
| Memory | Heap free/total/min, free stack, task count, uptime |
| Metrics | 4 uPlot time-series charts in a 2×2 grid — Heap (free + min free), Wi-Fi RSSI, Task count, Free stack. 30-min ring buffer persisted to browser `localStorage` (debounced every 30 s). |
| Device | Chip model, cores, revision, CPU freq, flash size, features |
| Firmware | App name, version, build date |

### WoL Config

Placeholder panel with UI skeletons for:
- Target device list (MAC addresses to wake)
- IP forwarding rules
- Broadcast settings (UDP port, broadcast IP, interface)

All inputs are currently disabled — backend not implemented.

## API Endpoints

All endpoints are served over **HTTPS** (TLS with self-signed certificate).

| Method | URI | Response |
|--------|-----|----------|
| `GET` | `/` | Dashboard HTML |
| `GET` | `/index.html` | Dashboard HTML |
| `GET` | `/css/layout.css` | Layout + breakpoints |
| `GET` | `/css/components.css` | Cards, forms, charts |
| `GET` | `/js/formatters.js` | Formatting helpers |
| `GET` | `/js/metrics.js` | Chart config + constants |
| `GET` | `/js/store.js` | Ring buffer + persistence |
| `GET` | `/js/charts.js` | uPlot init + update |
| `GET` | `/js/dashboard.js` | Auth + polling + tabs |
| `GET` | `/uplot.min.js` | uPlot 1.6.32 |
| `GET` | `/uplot.min.css` | uPlot base styles |
| `POST` | `/api/wol` | Trigger WoL packet, returns `{"ok":true}` or `{"ok":false}` |
| `GET` | `/api/wol` | Same behavior as POST |
| `GET` | `/api/status` | JSON with full device metadata |

Static assets are embedded in flash — no filesystem or SD card.

All `/api/*` endpoints require:

```http
X-API-Key: <WEB_API_TOKEN>
```

Missing/invalid key returns `401 Unauthorized`. If free heap is below the
safety threshold, API routes return `503 Service Unavailable`.

### Testing with curl

```bash
# Get device status
curl -k -s -H "X-API-Key: <TOKEN>" https://<ESP_IP>:<PORT>/api/status

# Trigger Wake-on-LAN
curl -k -X POST -H "X-API-Key: <TOKEN>" https://<ESP_IP>:<PORT>/api/wol
```

The `-k` flag skips certificate verification (self-signed cert).

## Remote Access (Port Forwarding)

To access the ESP32 dashboard and API from outside your local network, configure port forwarding on your router.

### Router Configuration

1. Log in to your router's admin panel (typically `192.168.1.1` or `192.168.0.1`)
2. Navigate to **Port Forwarding** / **NAT** / **Virtual Server** section
3. Add a new rule:

| Setting | Value |
|---------|-------|
| **Service Name** | `WakeOnLine_HTTPS` |
| **External Port** | `38429` (or any high port you prefer) |
| **Internal IP** | `<ESP32_STATIC_IP>` (from `.env` `ESP_IP` or DHCP) |
| **Internal Port** | `38429` (must match `WEB_PORT` in `.env`) |
| **Protocol** | TCP |

4. Save and apply the configuration

### Dynamic DNS (Recommended)

If your ISP assigns a dynamic public IP, use a DDNS service like DuckDNS:

1. Create an account at [duckdns.org](https://www.duckdns.org)
2. Register a subdomain (e.g., `nexus-coffee.duckdns.org`)
3. Install the DuckDNS updater on a device in your network (or configure it on your router if supported)
4. Use your domain when generating the SSL certificate:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout main/server.key \
  -out main/server.crt \
  -days 3650 \
  -subj "/CN=nexus-coffee.duckdns.org"
```

### Testing Remote Access

From any external network:

```bash
# Via IP
curl -k -H "X-API-Key: <TOKEN>" https://<YOUR_PUBLIC_IP>:38429/api/status

# Via DDNS domain
curl -k -H "X-API-Key: <TOKEN>" https://nexus-coffee.duckdns.org:38429/api/status
```

Or open in a browser: `https://nexus-coffee.duckdns.org:38429/`

> **Security Note:** The self-signed certificate will trigger browser warnings. For production use, consider Let's Encrypt certificates (requires more complex setup) or certificate pinning in your client application.

### `/api/status` response

```json
{
  "wifi": true,
  "ip": "192.168.1.9",
  "wifi_rssi": -55,
  "mac": "3C:8A:1F:A3:D2:74",
  "heap_free": 215460,
  "heap_min_free": 198216,
  "heap_total": 327680,
  "free_stack": 12300,
  "task_count": 14,
  "uptime": 154,
  "chip_model": "ESP32",
  "chip_cores": 2,
  "chip_revision": 3,
  "cpu_freq": 160000000,
  "flash_size": 4194304,
  "chip_features": ["Embedded Flash","Wi-Fi b/g/n","BT","BLE"],
  "app_name": "lan-controller-esp32",
  "app_version": "55a178f-dirty",
  "app_date": "Jul 26 2026",
  "app_time": "18:39:49"
}
```

## Unit tests

26 unit tests run on-device via the ESP-IDF Unity framework. See
[main/test/README](main/test/README) for the full guide.

```bash
# Test mode is enabled by default in sdkconfig (CONFIG_RUN_UNIT_TESTS_AT_BOOT=y)
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
# Press Enter, then type * to run all 26 tests
```

Tag-based filtering: `"[dotenv]"` (7), `"[wol]"` (14), `"[web_API]"` (5).

## Dependencies

[cJSON](https://github.com/DaveGamble/cJSON) via Espressif component registry.

```bash
idf.py add-dependency "namespace/name^1"
```

Lock file `dependencies.lock` is committed. `managed_components/` is gitignored.

## idf.py

| Command | Description |
|---------|-------------|
| `idf.py set-target esp32` | Configure for ESP32 (run once) |
| `idf.py build` | Compile |
| `idf.py -p PORT flash` | Flash binary |
| `idf.py -p PORT flash monitor` | Flash + serial monitor |
| `idf.py monitor` | Serial monitor only |
| `idf.py menuconfig` | Kconfig editor |
| `idf.py clean` | Delete build artifacts |
| `idf.py fullclean` | Delete everything, including config |
| `idf.py add-dependency "x"` | Add component dependency |
| `idf.py reconfigure` | Re-run CMake |

See [AGENTS.md](AGENTS.md) for where `idf.py` is available on this machine.
