# ESP32 HTTPD API Security Guidelines for Public Exposure

## Purpose
This document provides actionable engineering guidelines and security constraints for AI code agents, software developers, and automated code review tools implementing or auditing an ESP32 httpd-based API exposed directly to the public internet via port forwarding.

---

## Threat Model: Public Port Forwarding
When an ESP32 is made accessible via port forwarding:
* **Full Exposure:** The device is scanned continuously by automated scanners (ZMap, Masscan, Shodan).
* **Hardware Limitations:** Non-standard/random ports offer zero defense against full sweeps. Microcontroller TCP stacks lack enterprise OS syn-flood and high-throughput network protections.
* **Objective:** Block Remote Code Execution (RCE), token leaks, heap-starvation crash loops, and unauthorized control operations.

---

## Mandatory Agent Rules & Constraints

### Rule 1: Always Enforce TLS Encryption (`esp_https_server`)
* **Requirement:** Never deploy unencrypted HTTP (`esp_httpd`) on public interfaces.
* **Implementation:** Always use `esp_https_server.h` with `httpd_ssl_start()`.
* **Certificates:** Provision valid TLS certificates or self-signed certs paired with client-side hash pinning.
* **Certificate Generation:**
  ```bash
  openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout server.key \
    -out server.crt \
    -days 3650 \
    -subj "/CN=<your-domain>.duckdns.org"
  ```
* **Embedding:** Use `EMBED_TXT` in CMakeLists.txt to embed `server.crt` and `server.key` into flash.

### Rule 2: Enforce Strict HTTP Server Resource Caps
Prevent socket exhaustion and memory starvation attacks by applying the following limits to `httpd_ssl_config_t`:

| Parameter | Recommended Value | Rationale |
| :--- | :--- | :--- |
| `max_open_sockets` | `3` to `4` | Prevents RAM exhaustion from socket allocation |
| `lru_purge_enable` | `true` | Aggressively purges idle connections when sockets fill up |
| `max_uri_len` | `256` bytes | Blocks long URI buffer allocation/overflow vectors |
| `max_req_hdr_len` | `512` bytes | Prevents oversized header payloads from exhausting memory |
| `recv_wait_timeout` | `5` seconds | Mitigates slowloris / held socket attacks |
| `send_wait_timeout` | `5` seconds | Prevents blocked socket tasks on dropped connections |

### Rule 3: Fail-Fast Authentication & Timing-Safe Token Checks
* **Header-Based Auth:** Require credentials via headers (e.g., `X-API-Key` or `Authorization: Bearer <TOKEN>`). **Do not** accept authorization tokens in URI parameters (prevents token leakage in proxy logs).
* **Fail-Fast Pattern:** Validate authorization **before** reading request bodies, allocating memory, or executing JSON/payload parsers.
* **Constant-Time Comparison:** Use `mbedtls_ct_memcmp()` (or an equivalent constant-time function) for token comparisons to eliminate timing side-channel leaks.

### Rule 4: Heap Protection Guards & DoS Resilience
* **Heap Guard:** Check free heap size (`esp_get_free_heap_size()`). If free heap drops below **30 KB**, reject requests immediately with `503 Service Unavailable`.
* **Input Constraints:** Enforce tight length checks on `httpd_req_recv()` buffers.
* **Watchdog Integration:** Ensure Task Watchdog Timer (TWDT) is active so thread stalls trigger automatic system resets.

### Rule 5: Hardware & Silicon-Level Protection
* **Flash Encryption:** Must be enabled in production environments to protect API keys, certificates, and internal Flash storage.
* **Secure Boot v2:** Enforce cryptographic signature verification to prevent loading untrusted firmware binaries.

---

## ESP-IDF Reference Implementation Template

```c
#include <esp_https_server.h>
#include <esp_system.h>
#include <mbedtls/constant_time.h>

#define SECURE_API_TOKEN "YOUR_SECURE_TOKEN_HERE"
#define MIN_SAFE_HEAP_BYTES 30000

/**
 * @brief Timing-safe authorization check
 */
static bool is_request_authorized(httpd_req_t *req) {
    char header_val[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-API-Key", header_val, sizeof(header_val)) == ESP_OK) {
        // Constant-time memory comparison to mitigate timing side-channel leaks
        if (mbedtls_ct_memcmp(header_val, SECURE_API_TOKEN, strlen(SECURE_API_TOKEN)) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Protected API endpoint handler pattern
 */
esp_err_t secure_api_endpoint_handler(httpd_req_t *req) {
    // 1. Fail-Fast Authorization Check
    if (!is_request_authorized(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL; // Abort immediately before parsing body
    }

    // 2. Heap Protection Guard
    if (esp_get_free_heap_size() < MIN_SAFE_HEAP_BYTES) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low Memory Threshold Reached");
        return ESP_FAIL;
    }

    // 3. Process Request Safely
    const char *resp_str = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    return ESP_OK;
}

/**
 * @brief Hardened HTTPS Server Initialization
 */
httpd_handle_t start_hardened_https_server(const unsigned char *cacert, size_t cacert_len,
                                          const unsigned char *prvkey, size_t prvkey_len) {
    httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
    
    // Configure TLS Certificates
    ssl_config.cacert_pem = cacert;
    ssl_config.cacert_len = cacert_len;
    ssl_config.prvkey_pem = prvkey;
    ssl_config.prvkey_len = prvkey_len;

    // Strict Resource Caps
    ssl_config.httpd.max_open_sockets = 3;
    ssl_config.httpd.lru_purge_enable = true;
    ssl_config.httpd.max_uri_len = 256;
    ssl_config.httpd.max_req_hdr_len = 512;
    ssl_config.httpd.recv_wait_timeout = 5;
    ssl_config.httpd.send_wait_timeout = 5;

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_ssl_start(&server, &ssl_config);
    if (ret == ESP_OK) {
        // Register API routes
        static const httpd_uri_t api_route = {
            .uri       = "/api/v1/resource",
            .method    = HTTP_GET,
            .handler   = secure_api_endpoint_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &api_route);
        return server;
    }

    return NULL;
}
```

---

## Agent Verification Checklist
When writing or auditing ESP32 firmware exposed to port forwarding:

- [ ] HTTPS (`esp_https_server.h`) is used instead of standard HTTP (`esp_httpd.h`).
- [ ] Socket capacity (`max_open_sockets`) is restricted to $\le 4$.
- [ ] `lru_purge_enable` is enabled (`true`).
- [ ] API tokens are passed in headers, never URL parameters.
- [ ] Key verification uses `mbedtls_ct_memcmp()` (timing-safe comparison).
- [ ] A heap guard (`esp_get_free_heap_size()`) prevents low-memory allocations.
- [ ] Flash Encryption and Secure Boot v2 are specified in device configuration documentation.
