# HTTPS Migration Plan — HTTP to TLS

## Goal

Upgrade the existing ESP32 web server from plain HTTP (`esp_http_server`) to HTTPS (`esp_https_server`) while preserving the current modular architecture, security hardening, and dotenv-based configuration.

---

## Current State

- **Server:** `web_util.c` uses `httpd_start()` with `httpd_config_t`
- **Port:** Configured via `.env` (`WEB_PORT`, default 80)
- **Security:** Resource caps, heap guard, `mbedtls_ct_memcmp()`, fail-fast auth
- **Architecture:** Modular — `app.c` → `web_util.c` → `web_API.c`

---

## Required Changes

### 1. Generate Self-Signed Certificate (One-Time)

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout main/server.key \
  -out main/server.crt \
  -days 3650 \
  -subj "/CN=nexus-coffee.duckdns.org"
```

Place `server.crt` and `server.key` in `main/` directory.

### 2. Update `main/CMakeLists.txt`

Add certificate embedding via `EMBED_TXT`:

```cmake
idf_component_register(
    SRCS       "app.c"
               # ... existing sources ...
    INCLUDE_DIRS "."
                 "utils"
    EMBED_TXT  "server.crt" "server.key"
    EMBED_FILES "web/index.html"
                # ... existing files ...
    REQUIRES  nvs_flash
              esp_wifi
              esp_netif
              esp_event
              esp_https_server   # <-- Change from esp_http_server
              # ... rest unchanged ...
)
```

### 3. Update `main/utils/web_util.h`

Add cert/key parameters to `web_start()`:

```c
esp_err_t web_start(const uint8_t *servercert, size_t servercert_len,
                    const uint8_t *prvkey, size_t prvkey_len);
```

### 4. Update `main/utils/web_util.c`

Switch from `httpd_start` to `httpd_ssl_start`:

```c
#include "esp_https_server.h"

esp_err_t web_start(const uint8_t *servercert, size_t servercert_len,
                    const uint8_t *prvkey, size_t prvkey_len)
{
    if (s_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.port_secure        = s_port;
    config.servercert         = servercert;
    config.servercert_len     = servercert_len;
    config.prvkey_pem         = prvkey;
    config.prvkey_len         = prvkey_len;

    // Preserve existing security resource caps
    config.httpd.uri_match_fn     = httpd_uri_match_wildcard;
    config.httpd.max_open_sockets = WEB_MAX_OPEN_SOCKETS;
    config.httpd.lru_purge_enable = true;
    config.httpd.max_uri_len      = WEB_MAX_URI_LEN;
    config.httpd.max_req_hdr_len  = WEB_MAX_REQ_HDR_LEN;
    config.httpd.recv_wait_timeout = WEB_RECV_TIMEOUT_SEC;
    config.httpd.send_wait_timeout = WEB_SEND_TIMEOUT_SEC;

    esp_err_t ret = httpd_ssl_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS: %s", esp_err_to_name(ret));
        return ret;
    }

    web_API_init(s_server);
    ESP_LOGI(TAG, "HTTPS server started on port %u", s_port);
    return ESP_OK;
}
```

### 5. Update `main/app.c`

Declare extern cert symbols and pass to `web_start()`:

```c
// Embedded SSL certificate pointers
extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[]   asm("_binary_server_crt_end");
extern const uint8_t server_key_start[] asm("_binary_server_key_start");
extern const uint8_t server_key_end[]   asm("_binary_server_key_end");

// In app_main(), replace web_start() call:
ESP_ERROR_CHECK(web_start(
    server_crt_start, server_crt_end - server_crt_start,
    server_key_start, server_key_end - server_key_start
));
```

### 6. Update `.env` (Optional)

Change `WEB_PORT` to a high port for HTTPS:

```env
WEB_PORT=38429
```

---

## Files to Modify

| File | Change |
|------|--------|
| `main/server.crt` | Add (generate via OpenSSL) |
| `main/server.key` | Add (generate via OpenSSL) |
| `main/CMakeLists.txt` | Add `EMBED_TXT`, change `esp_http_server` → `esp_https_server` |
| `main/utils/web_util.h` | Update `web_start()` signature |
| `main/utils/web_util.c` | Switch to `httpd_ssl_start()` |
| `main/app.c` | Add cert extern declarations, pass to `web_start()` |
| `main/.env` | Update `WEB_PORT` to HTTPS port |

---

## Verification Checklist

- [ ] `idf.py fullclean && idf.py build` succeeds
- [ ] Flash and monitor: "HTTPS server started on port XXXXX"
- [ ] `curl -k https://<ESP32_IP>:<PORT>/` returns index.html
- [ ] `curl -k -H "X-API-Key: <TOKEN>" https://<ESP32_IP>:<PORT>/api/status` returns JSON
- [ ] `curl -k -X POST -H "X-API-Key: <TOKEN>" https://<ESP32_IP>:<PORT>/api/wol` sends WoL
- [ ] Browser shows certificate warning (self-signed)
- [ ] Resource caps still enforced (max_open_sockets, timeouts)
- [ ] Heap guard still active (503 on low memory)

---

## Security Notes

- **Self-signed cert:** Browsers will show `NET::ERR_CERT_AUTHORITY_INVALID`. Accept or use client-side hash pinning.
- **Never embed tokens in HTML:** The proposed implementation's `dashboard_handler` exposes `WEB_API_TOKEN` in client-side JS — this is a critical flaw. Keep token in headers only.
- **Flash encryption:** Enable in production to protect embedded cert/key.

---

## What NOT to Change

- Keep `mbedtls_ct_memcmp()` — do not use custom constant-time compare
- Keep `.env` for configuration — do not hardcode credentials
- Keep health monitoring loop — do not remove edge-detection
- Keep resource caps — do not remove socket/memory limits
- Keep heap guard — do not remove MIN_SAFE_HEAP_BYTES check
