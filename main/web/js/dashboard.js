/* ── Dashboard: token auth, status polling, tab switching ────── */

var API_TOKEN_KEY = "webApiToken.v1";
var apiToken = "";
var tokenInput = null;
var tokenToggleBtn = null;
var tokenChangeBtn = null;
var tokenForgetBtn = null;
var isTokenEditing = false;

function loadApiToken() {
  try {
    var saved = localStorage.getItem(API_TOKEN_KEY);
    apiToken = saved || "";
  } catch (e) {
    apiToken = "";
  }
}

function persistApiToken(value) {
  apiToken = value || "";
  try {
    if (apiToken) localStorage.setItem(API_TOKEN_KEY, apiToken);
    else localStorage.removeItem(API_TOKEN_KEY);
  } catch (e) { /* storage unavailable */ }
}

function authHeaders() {
  if (!apiToken) return {};
  return { "X-API-Key": apiToken };
}

function setTokenInputMode(editing) {
  if (!tokenInput || !tokenChangeBtn) return;
  isTokenEditing = !!editing;
  tokenInput.disabled = !isTokenEditing;
  tokenChangeBtn.textContent = isTokenEditing ? "Save token" : "Change token";
  if (isTokenEditing) {
    tokenInput.focus();
    tokenInput.select();
  }
}

function setTokenVisibility(show) {
  if (!tokenInput || !tokenToggleBtn) return;
  tokenInput.type = show ? "text" : "password";
  tokenToggleBtn.textContent = show ? "Hide key" : "Show key";
}

function setVal(id, text, cls) {
  var el = document.getElementById(id);
  if (!el) return;
  el.textContent = text;
  if (cls !== undefined) el.className = "value " + cls;
  else el.className = "value";
}

function showError(show) {
  var el = document.getElementById("status-error");
  if (el) el.className = show ? "error-banner" : "error-banner hidden";
}

/* ── Nexus server health (direct browser fetch, no firmware proxy) ─ */

var NEXUS_HEALTH_URL  = "http://nexus-lan.local:47102/api/v1/health";
var NEXUS_TIMEOUT_MS  = 5000;
var nexusController   = null;

function setNexusStatus(state) {
  var el = document.getElementById("nexus-status");
  if (!el) return;
  if (state === "ok") {
    el.textContent = "OK!";
    el.className = "value connected";
  } else if (state === "checking") {
    el.textContent = "checking…";
    el.className = "value";
  } else {
    el.textContent = "unreachable";
    el.className = "value disconnected";
  }
}

async function checkNexusHealth() {
  if (nexusController) nexusController.abort();
  var controller = new AbortController();
  nexusController = controller;
  var timer = setTimeout(function() { controller.abort(); }, NEXUS_TIMEOUT_MS);
  try {
    var res = await fetch(NEXUS_HEALTH_URL, { signal: controller.signal });
    setNexusStatus("ok");
  } catch (e) {
    setNexusStatus("unreachable");
  } finally {
    clearTimeout(timer);
  }
}

async function refreshStatus() {
  try {
    var res = await fetch("/api/status", { headers: authHeaders() });
    if (!res.ok) throw new Error("Not available");
    var data = await res.json();

    showError(false);

    setVal("wifi-status", data.wifi ? "Connected" : "Disconnected",
           data.wifi ? "connected" : "disconnected");
    setVal("lan-ip", data.ip || "--");
    setVal("mac", data.mac || "--");

    setVal("heap-free", fmtBytes(data.heap_free));
    setVal("heap-total", fmtBytes(data.heap_total));
    setVal("heap-min-free", fmtBytes(data.heap_min_free));
    setVal("free-stack", fmtBytes(data.free_stack));
    setVal("task-count", data.task_count != null ? data.task_count : "--");
    setVal("uptime", fmtUptime(data.uptime));

    setVal("chip-model", data.chip_model || "--");
    setVal("chip-cores", data.chip_cores != null ? data.chip_cores : "--");
    setVal("chip-revision", data.chip_revision != null ? "Rev " + data.chip_revision : "--");
    setVal("cpu-freq", fmtFreq(data.cpu_freq));
    setVal("flash-size", fmtBytes(data.flash_size));
    setVal("chip-features", (data.chip_features && data.chip_features.length)
           ? data.chip_features.join(", ") : "--");

    setVal("app-name", data.app_name || "--");
    setVal("app-version", data.app_version || "--");
    setVal("app-build", (data.app_date && data.app_time)
           ? data.app_date + " " + data.app_time : "--");

    var ipEl = document.getElementById("sidebar-ip");
    if (ipEl && data.ip) ipEl.textContent = data.ip;

    updateCharts(data);
  } catch {
    showError(true);
  }
}

function switchTab(tabName) {
  document.querySelectorAll(".tab").forEach(function(t) {
    t.classList.remove("active");
  });
  document.querySelectorAll(".menu-item, .bottombar-item").forEach(function(el) {
    if (el.dataset.tab === tabName) el.classList.add("active");
    else el.classList.remove("active");
  });

  var tab = document.getElementById("tab-" + tabName);
  if (tab) tab.classList.add("active");
}

document.addEventListener("DOMContentLoaded", function() {
  loadApiToken();

  tokenInput = document.getElementById("api-token");
  tokenToggleBtn = document.getElementById("toggle-api-token");
  tokenChangeBtn = document.getElementById("change-api-token");
  tokenForgetBtn = document.getElementById("forget-api-token");

  if (tokenInput) {
    tokenInput.value = apiToken;
    tokenInput.addEventListener("keydown", function(evt) {
      if (evt.key === "Enter" && isTokenEditing) {
        persistApiToken(this.value.trim());
        setTokenInputMode(false);
        refreshStatus();
      }
    });
    setTokenInputMode(!apiToken);
  }

  if (tokenToggleBtn) {
    tokenToggleBtn.addEventListener("click", function() {
      setTokenVisibility(tokenInput && tokenInput.type === "password");
    });
  }
  setTokenVisibility(false);

  if (tokenChangeBtn) {
    tokenChangeBtn.addEventListener("click", function() {
      if (!tokenInput) return;
      if (!isTokenEditing) {
        setTokenInputMode(true);
        return;
      }
      persistApiToken(tokenInput.value.trim());
      setTokenInputMode(false);
      refreshStatus();
    });
  }

  if (tokenForgetBtn) {
    tokenForgetBtn.addEventListener("click", function() {
      persistApiToken("");
      if (tokenInput) tokenInput.value = "";
      setTokenInputMode(true);
      setTokenVisibility(false);
      showError(true);
    });
  }

  document.querySelectorAll(".menu-item, .bottombar-item").forEach(function(el) {
    el.addEventListener("click", function() {
      switchTab(this.dataset.tab);
    });
  });

  loadHistory();
  initCharts();
  refreshStatus();
  setInterval(refreshStatus, 5000);
  checkNexusHealth();
  setInterval(checkNexusHealth, 5000);
  window.addEventListener("beforeunload", saveHistory);
});
