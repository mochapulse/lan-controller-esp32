# Agent Setup — lan-controller-esp32

## idf.py availability

`idf.py` is defined as a shell function in `~/.zshrc`:

```zsh
idf.py() {
    if [[ -z "${IDF_PATH}" ]]; then
        source "/home/javastral/.espressif/tools/activate_idf_v6.0.2.sh" > /dev/null 2>&1
    fi
    python3 "$IDF_PATH/tools/idf.py" "$@"
}
```

When running `idf.py` commands, source the activation script first if the function
is not available in your shell:

```bash
source /home/javastral/.espressif/tools/activate_idf_v6.0.2.sh > /dev/null 2>&1 && \
  python3 "$IDF_PATH/tools/idf.py" <command>
```

## Python venv note

If you get a warning about `python` vs `python3` venv mismatch:

```
'python' is currently active while the project was configured with 'python3'.
Run 'idf.py fullclean' to start again.
```

This is cosmetic — the build proceeds normally. If it blocks, use the explicit
`python3` invocation shown above instead of the `idf.py` function.

## Flash port

The ESP32 is connected at `/dev/ttyUSB0`.

## Build commands

```bash
# Build only
python3 "$IDF_PATH/tools/idf.py" build

# Build + flash
python3 "$IDF_PATH/tools/idf.py" flash -p /dev/ttyUSB0

# Build + flash + monitor (needs TTY — not usable in agent environments)
python3 "$IDF_PATH/tools/idf.py" -p /dev/ttyUSB0 flash monitor
```

## Unit tests

26 tests in `main/test/` (7 dotenv + 14 wol + 5 web_API). Tests run
on-device via the ESP-IDF Unity test runner.

### Test mode toggle

```bash
# Enable tests (default)
python3 "$IDF_PATH/tools/idf.py" menuconfig
# Unit Tests → Run all unit tests at boot → [*]

# Disable for production
# Unit Tests → Run all unit tests at boot → [ ]
```

### Build & flash (test mode)

```bash
python3 "$IDF_PATH/tools/idf.py" build
python3 "$IDF_PATH/tools/idf.py" flash -p /dev/ttyUSB0
```

### Run tests over serial

```bash
# Interactive (press Enter for menu, then * for all tests)
python3 "$IDF_PATH/tools/idf.py" monitor -p /dev/ttyUSB0

# Automated — run all 26 tests and parse pass/fail
python3 -c "
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
ser.dtr=False; ser.rts=True; time.sleep(0.1); ser.dtr=True; time.sleep(4)
ser.write(b'*\r'); time.sleep(6)
out = ser.read(16384)
ser.close()
print(out.decode(errors='replace'))
"
```

Full guide: [main/test/README](main/test/README).

## Doxygen docs

API docs are built with `doxygen` + `graphviz` (installed system-wide):

```bash
# Generate (output dir must exist first — doxygen cannot create it)
mkdir -p doc/doxygen
doxygen Doxyfile

# Output: doc/doxygen/html/index.html (gitignored)
```

- Keep the `Doxyfile` **minimal** — only non-default values. Do NOT run
  `doxygen -u Doxyfile`; it expands the file to ~3000 lines of defaults.
- CI builds and deploys docs on push to `main` via `.github/workflows/docs.yml`.
- Full guide: [doc/README.md](doc/README.md)

## API auth + endpoint behavior

- API routes (`/api/status`, `/api/wol`) require header auth:
  `X-API-Key: <WEB_API_TOKEN>`.
- Configure `WEB_API_TOKEN` in `main/.env` (see `main/.env.example`).
- API routes can return `503 Service Unavailable` with body
  `Low memory threshold reached` when free heap is below the safety guard.
- All endpoints are served over **HTTPS** (TLS with self-signed certificate).

### Testing HTTPS endpoints

```bash
# Status endpoint
curl -k -s -H "X-API-Key: <TOKEN>" https://<ESP_IP>:<PORT>/api/status

# Trigger WoL
curl -k -X POST -H "X-API-Key: <TOKEN>" https://<ESP_IP>:<PORT>/api/wol

# Dashboard (HTML)
curl -k -s https://<ESP_IP>:<PORT>/
```

The `-k` flag skips certificate verification (self-signed cert).

## SSL Certificate Generation

The HTTPS server requires a self-signed certificate. Generate once and embed:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout main/server.key \
  -out main/server.crt \
  -days 3650 \
  -subj "/CN=<your-domain>.duckdns.org"
```

- `server.crt` and `server.key` are gitignored (secrets).
- Embedded via `EMBED_TXTFILES` in `main/CMakeLists.txt`.
- Run `idf.py fullclean` after adding certificates to regenerate build artifacts.
- Browsers will show a certificate warning (`NET::ERR_CERT_AUTHORITY_INVALID`) — accept it to proceed.

## Web frontend

Web assets under `main/web/` are embedded in flash via `EMBED_FILES` in
`main/CMakeLists.txt` and served through the static file table in
`main/utils/web_API.c` (`s_files[]`).

### File structure

```
main/web/
├── index.html         (imports css/ + js/ + uplot)
├── uplot.min.js       (v1.6.32, ~51 KB)
├── uplot.min.css      (~2 KB)
├── css/
│   ├── layout.css     (reset, variables, sidebar, bottombar, all breakpoints)
│   └── components.css (cards, forms, chart grid, error, placeholders, uPlot overrides)
└── js/
    ├── formatters.js  (fmtBytes, fmtUptime, fmtFreq, fmtRssi)
    ├── metrics.js     (METRICS registry, CHARTS, MAX_POINTS, STORAGE_KEY)
    ├── store.js       (ring buffer, pushSample, loadHistory, saveHistory — 30s debounced)
    ├── charts.js      (makeChartOpts, initCharts, updateCharts — 4-chart 2×2 grid)
    └── dashboard.js   (token auth, refreshStatus, switchTab, DOMContentLoaded)
```

- Script load order in `index.html` is **critical**: `uplot.min.js` → `formatters.js` →
  `metrics.js` → `store.js` → `charts.js` → `dashboard.js`.
- No bundler, no ES modules — everything is IIFE globals with `var`.
- History is persisted in the browser via `localStorage` (`history.v2` key),
  debounced to every 30 seconds (not the 5-second poll interval). Flushed on
  `beforeunload`. Auto-purged on page init if corrupt.

### Adding a new static file

1. Add to `EMBED_FILES` in `main/CMakeLists.txt`.
2. Add `extern const uint8_t <name>_start[] asm("_binary_<flat_name>_start")`
   in `main/utils/web_API.c`.
3. Add a row to `s_files[]` mapping the URI to the start/end symbols.
4. If JS/CSS, add `<script>`/`<link>` in `index.html`.

**Gotcha**: `EMBED_FILES` flattens subdirectories — the symbol for
`web/js/formatters.js` is `_binary_formatters_js_start`, **not**
`_binary_web_js_formatters_js_start`. The `web/js/` prefix is stripped.
After adding new files, run `idf.py fullclean` to regenerate the `.S` objects.

## Project path

```
/home/javastral/GIT/Mocha/lan-controller-esp32
```
