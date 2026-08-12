# ── main component ──────────────────────────────────────────────────────────
# Sources, include dirs, embedded web assets, and dependencies.
#
# EMBED_FILES  — HTML/CSS/JS are embedded in flash via objcopy at build time.
#               They appear as _binary_<name>_start / _end symbols.
#
# .env         — EMBED_FILES skips dotfiles, so we embed .env manually using
#               a custom objcopy command below.
#
# REQUIRES     — Component dependencies for the linker. If a required component
#               is missing, CMake will fail at configure time.
# ────────────────────────────────────────────────────────────────────────────

idf_component_register(
    SRCS       "app.c"
               "utils/dotenv.c"
               "utils/wifi_connect.c"
               "utils/wol.c"
               "utils/web_util.c"
               "utils/web_API.c"
    INCLUDE_DIRS "."
                 "utils"
    EMBED_TXTFILES "server.crt" "server.key"
    EMBED_FILES "web/index.html"
                "web/css/layout.css"
                "web/css/components.css"
                "web/js/formatters.js"
                "web/js/metrics.js"
                "web/js/store.js"
                "web/js/charts.js"
                "web/js/dashboard.js"
                "web/uplot.min.js"
                "web/uplot.min.css"
    REQUIRES  nvs_flash
              esp_wifi
              esp_netif
              esp_event
              esp_https_server
              spi_flash
              esp_timer
              esp_app_format
              cjson
              mbedtls
              lwip
              unity
)

# ── Embed .env (dotfile — not picked up by EMBED_FILES) ─────────────────────
# objcopy converts the flat binary into an ELF object with symbols
# _binary__env_start / _binary__env_end, placed in a custom rodata section.
# The symbols are declared extern in dotenv.c.

set(dotenv_obj "${CMAKE_CURRENT_BINARY_DIR}/dotenv.o")
add_custom_command(
    OUTPUT ${dotenv_obj}
    COMMAND ${CMAKE_OBJCOPY}
            -I binary -O elf32-xtensa-le -B xtensa
            --rename-section .data=.rodata.dotenv,contents,alloc,load,readonly,data
            .env ${dotenv_obj}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.env"
    COMMENT "Embedding .env -> dotenv.o"
)
target_sources(${COMPONENT_LIB} PRIVATE ${dotenv_obj})

# ── Unit test sources ────────────────────────────────────────────
if(CONFIG_RUN_UNIT_TESTS_AT_BOOT)
    target_sources(${COMPONENT_LIB} PRIVATE
        "test/test_dotenv.c"
        "test/test_wol.c"
        "test/test_web_API.c"
    )
endif()
