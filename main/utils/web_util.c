/**
 * @file    web_util.c
 * @brief   HTTPS server lifecycle — implementation.
 *
 * Manages a single httpd_handle_t. Opens on web_start(), delegates endpoint
 * registration to web_API_init(), and closes on web_stop().
 *
 * @note    Wildcard URI matching (httpd_uri_match_wildcard) is enabled so
 *          that '/' '*' patterns work. The default httpd config uses strncmp
 *          which treats `*` as a literal character.
 */

#include "web_util.h"

#include "esp_err.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "web_API.h"

static const char *TAG = "web_util";

static httpd_handle_t s_server = NULL;   /**< Active server handle */
static uint16_t       s_port   = 38429;  /**< TCP port (default 38429) */

/* ── Security resource caps (public-exposure hardening) ─────── */

#define WEB_MAX_OPEN_SOCKETS   3    /**< Bound concurrent sockets to limit RAM pressure */
#define WEB_MAX_URI_LEN        256  /**< Reject oversized URI paths */
#define WEB_MAX_REQ_HDR_LEN    2048 /**< Allow browser headers (cookies, user-agent) */
#define WEB_RECV_TIMEOUT_SEC   5    /**< Slow-client receive timeout */
#define WEB_SEND_TIMEOUT_SEC   5    /**< Stalled-send timeout */

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t web_init(uint16_t port)
{
    s_port = port;
    ESP_LOGI(TAG, "Initialized on port %u", s_port);
    return ESP_OK;
}

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
    config.prvtkey_pem        = prvkey;
    config.prvtkey_len        = prvkey_len;

    config.httpd.uri_match_fn     = httpd_uri_match_wildcard;
    config.httpd.max_open_sockets = WEB_MAX_OPEN_SOCKETS;
    config.httpd.lru_purge_enable = true;
    config.httpd.max_uri_len      = WEB_MAX_URI_LEN;
    config.httpd.max_req_hdr_len  = WEB_MAX_REQ_HDR_LEN;
    config.httpd.recv_wait_timeout = WEB_RECV_TIMEOUT_SEC;
    config.httpd.send_wait_timeout = WEB_SEND_TIMEOUT_SEC;
    config.httpd.stack_size       = 8192;

    esp_err_t ret = httpd_ssl_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS: %s", esp_err_to_name(ret));
        return ret;
    }

    web_API_init(s_server);

    ESP_LOGI(TAG, "HTTPS server started on port %u", s_port);
    return ESP_OK;
}

bool web_health(void)
{
    return s_server != NULL;
}

esp_err_t web_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop: %s", esp_err_to_name(ret));
        return ret;
    }

    s_server = NULL;
    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}
