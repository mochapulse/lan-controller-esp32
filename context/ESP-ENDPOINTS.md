# ESP32 HTTP Endpoints (Current Firmware)

This document describes the endpoints implemented in the current codebase (`main/utils/web_API.c`).

## Base URL

- Server starts on `WEB_PORT` from `.env` (default: `38429`).
- Access from your LAN using:

```text
https://192.168.1.19:38429
```

> **Note:** When using a self-signed certificate, browsers will show a security warning. Accept it or configure client-side certificate pinning.

You can see the current LAN IP in serial logs (`HTTPS server: https://...`) or by opening `/api/status`.

---

## API Authentication

All `/api/*` routes require header-based authentication.

- Header: `X-API-Key: <WEB_API_TOKEN>`
- Token source: `WEB_API_TOKEN` in `main/.env`

Missing or invalid token returns:

- HTTP status: `401 Unauthorized`
- Body: `Unauthorized`

If `WEB_API_TOKEN` is missing/invalid in firmware config:

- HTTP status: `500 Internal Server Error`
- Body: `API auth is not configured` or `API auth config is invalid`

---

## 1) `GET /api/status`

Returns device/network/runtime info as JSON.

### Example

```bash
curl -k -s \
  -H "X-API-Key: <WEB_API_TOKEN>" \
  https://<ESP32_LAN_IP>:<WEB_PORT>/api/status
```

### Response (shape)

```json
{
  "wifi": true,
  "ip": "192.168.1.120",
  "heap_free": 123456,
  "heap_min_free": 98765,
  "heap_total": 327680,
  "free_stack": 5000,
  "task_count": 12,
  "uptime": 1234,
  "chip_features": ["Embedded Flash", "Wi-Fi b/g/n", "BLE"],
  "chip_model": "ESP32",
  "chip_cores": 2,
  "chip_revision": 3,
  "cpu_freq": 240000000,
  "app_name": "ESP32_WoL",
  "app_version": "x.y.z",
  "app_date": "Jul 31 2026",
  "app_time": "20:00:00",
  "flash_size": 2097152,
  "mac": "AA:BB:CC:DD:EE:FF"
}
```

Notes:
- `ip` can be empty when Wi-Fi is not connected yet.
- Some fields (for example `flash_size`, `mac`) appear only when underlying calls succeed.
- If free heap is below the safety threshold, request is rejected with:
  - HTTP status: `503 Service Unavailable`
  - Body: `Low memory threshold reached`

---

## 2) `POST /api/wol`
## 3) `GET /api/wol`

Both methods trigger the same Wake-on-LAN send operation.

### Examples

```bash
curl -k -X POST \
  -H "X-API-Key: <WEB_API_TOKEN>" \
  https://<ESP32_LAN_IP>:<WEB_PORT>/api/wol

curl -k \
  -H "X-API-Key: <WEB_API_TOKEN>" \
  https://<ESP32_LAN_IP>:<WEB_PORT>/api/wol
```

### Success response

```json
{"ok":true}
```

### Failure response

- HTTP status: `500 Internal Server Error`
- Body:

```json
{"ok":false}
```

Typical failure reason:
- WoL is not initialized because `.env` values are missing/invalid:
  - `SERVER_WOL_MAC` must be `XX:XX:XX:XX:XX:XX`
  - `BROADCAST_WOL_IP` must be a valid IPv4 address (for example `192.168.1.255`)
- Request is rejected when free heap is under the safety threshold (`503`).

---

## 4) Static UI files

Handled by:
- `GET /` (root)
- `GET /*` (wildcard)

Served embedded assets:
- `/index.html`
- `/style.css`
- `/app.js`
- `/uplot.min.js`
- `/uplot.min.css`

If a requested static path is not found, firmware returns `404`.
