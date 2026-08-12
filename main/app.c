/**
 * @file    app.c
 * @brief   Firmware entry point — bootstraps dotenv, Wi-Fi, and HTTPS server.
 *
 * Flow:
 *   1. Parse embedded .env.
 *   2. Init Wi-Fi station and connect.
 *   3. Wait up to 15 s for a DHCP lease.
 *   4. Start the HTTPS server.
 *   5. Enter a health-monitoring loop (every 5 s).
 *
 * The health loop retries Wi-Fi on disconnect and restarts the HTTPS server if
 * it stops. Warning logs are emitted once per transition into the unhealthy
 * state to avoid console spam during extended outages.
 */

#include "dotenv.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_util.h"
#include "wifi_connect.h"
#include "wol.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#if CONFIG_RUN_UNIT_TESTS_AT_BOOT
#include "unity_test_runner.h"
extern void test_dotenv_anchor(void);
extern void test_wol_anchor(void);
extern void test_web_API_anchor(void);
#endif

static const char *TAG = "APP";

extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[]   asm("_binary_server_crt_end");
extern const uint8_t server_key_start[] asm("_binary_server_key_start");
extern const uint8_t server_key_end[]   asm("_binary_server_key_end");

/* ── Health-loop constants ────────────────────────────────────── */

#define HEALTH_POLL_MS      5000   /**< Main-loop interval (ms)          */
#define IP_WAIT_TIMEOUT_MS  15000  /**< Max wait for first DHCP lease    */

/**
 * @brief   Firmware entry point — bootstraps dotenv, Wi-Fi, and HTTP server.
 *
 * Loads credentials from the embedded `.env`, connects to Wi-Fi, waits for
 * a DHCP lease, starts the HTTP dashboard, then enters an infinite
 * health-monitoring loop that retries Wi-Fi and restarts the server on
 * failure.
 *
 * @note    Warning logs use edge-detection to avoid console spam during
 *          extended outages — a transition warning fires only once per
 *          unhealthy episode.
 */
void app_main(void)
{
#if CONFIG_RUN_UNIT_TESTS_AT_BOOT
    test_dotenv_anchor();
    test_wol_anchor();
    test_web_API_anchor();
    ESP_LOGI(TAG, "Test mode — running unit test menu");
    unity_run_menu();
    while (1) { vTaskDelay(portMAX_DELAY); }
#endif

    /* ── .env ────────────────────────────────────────────────── */
    dotenv_init();

    const char *app_name = dotenv_get("APP_NAME");
    if (!app_name) app_name = "ESP32_WoL";
    ESP_LOGI(TAG, "%s starting", app_name);

    const char *wol_mac = dotenv_get("SERVER_WOL_MAC");
    const char *bcast_ip = dotenv_get("BROADCAST_WOL_IP");

    const char *ssid   = dotenv_get("SSID_WIFI");
    const char *passwd = dotenv_get("PASSWD_WIFI");
    if (!ssid || !passwd) {
        ESP_LOGE(TAG, "SSID_WIFI or PASSWD_WIFI not set in .env");
        return;
    }

    uint16_t web_port = (uint16_t)dotenv_get_int("WEB_PORT", 80);

    esp_err_t wol_ret = wol_init(wol_mac, bcast_ip);
    if (wol_ret != ESP_OK) {
        ESP_LOGW(TAG, "WoL disabled due to invalid config: %s",
                 esp_err_to_name(wol_ret));
    }

    /* ── Wi-Fi ───────────────────────────────────────────────── */
    const char *esp_ip = dotenv_get("ESP_IP");
    ESP_ERROR_CHECK(wifi_init(ssid, passwd, esp_ip));
    ESP_ERROR_CHECK(wifi_connect());

    const char *lan_ip = wifi_wait_for_ip(IP_WAIT_TIMEOUT_MS);
    if (lan_ip) {
        ESP_LOGI(TAG, "LAN IP: %s", lan_ip);
    } else {
        ESP_LOGW(TAG, "No IP yet, starting web server anyway");
        lan_ip = "0.0.0.0";
    }

    /* ── HTTPS server ─────────────────────────────────────────── */
    ESP_ERROR_CHECK(web_init(web_port));
    ESP_ERROR_CHECK(web_start(
        server_crt_start, server_crt_end - server_crt_start,
        server_key_start, server_key_end - server_key_start
    ));

    ESP_LOGI(TAG, "HTTPS server: https://%s:%u", lan_ip, web_port);

    /* ── Health loop ─────────────────────────────────────────── */
    bool was_wifi_unhealthy   = false; /**< Edge-detection for Wi-Fi warn  */
    bool was_server_unhealthy = false; /**< Edge-detection for server warn */

    while (1) {
        if (!wifi_health()) {
            if (!was_wifi_unhealthy) {
                ESP_LOGW(TAG, "WiFi not healthy — retrying");
                was_wifi_unhealthy = true;
            }
            wifi_retry();
        } else {
            was_wifi_unhealthy = false;
        }

        if (!web_health()) {
            if (!was_server_unhealthy) {
                ESP_LOGW(TAG, "HTTPS server not healthy — restarting");
                was_server_unhealthy = true;
            }
            web_start(
                server_crt_start, server_crt_end - server_crt_start,
                server_key_start, server_key_end - server_key_start
            );
        } else {
            was_server_unhealthy = false;
        }

        vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_MS));
    }
}
