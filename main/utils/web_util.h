/**
 * @file    web_util.h
 * @brief   HTTP server lifecycle — init, start, health check, stop.
 *
 * Thin wrapper around esp_http_server. Stores the port and server handle in
 * module-level statics so callers only need to invoke web_start()/web_stop().
 * Endpoint registration is delegated to web_API_init().
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Store the HTTPS server port. Must be called before web_start().
 *
 * @param port  TCP port number (default: 38429).
 * @return      Always ESP_OK.
 */
esp_err_t web_init(uint16_t port);

/**
 * @brief   Start the HTTPS server and register all URI handlers.
 *
 * If the server is already running this is a no-op.
 * Enables URI wildcard matching (httpd_uri_match_wildcard) so that '/' '*'
 * catch-all patterns work. Applies fixed resource caps to reduce exposure to
 * socket starvation and oversized-request memory pressure.
 *
 * @param servercert     Pointer to PEM-encoded certificate in flash.
 * @param servercert_len Length of certificate in bytes.
 * @param prvkey         Pointer to PEM-encoded private key in flash.
 * @param prvkey_len     Length of private key in bytes.
 *
 * @return  ESP_OK on success, or an esp_err_t from httpd_ssl_start().
 */
esp_err_t web_start(const uint8_t *servercert, size_t servercert_len,
                    const uint8_t *prvkey, size_t prvkey_len);

/**
 * @brief   Check whether the HTTP server is currently running.
 *
 * @return  true if the server handle is non-NULL.
 */
bool web_health(void);

/**
 * @brief   Stop the HTTP server and NULL the handle.
 *
 * Safe to call when already stopped (no-op).
 *
 * @return  ESP_OK or an esp_err_t from httpd_stop().
 */
esp_err_t web_stop(void);

#ifdef __cplusplus
}
#endif
