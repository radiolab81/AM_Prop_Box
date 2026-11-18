#include "amp_web.hpp"
#include "amp_config.hpp"
#include "amp_net.hpp"
#include "sim.hpp"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

static const char *TAG = "web";
static httpd_handle_t s_srv = nullptr;

// ------------------------------------------------------------------ WebSocket-Verteiler
static std::mutex s_ws_mx;
static std::vector<int> s_ws_fds;
static std::string s_ws_latest;
static TaskHandle_t s_ws_task = nullptr;

static void ws_add(int fd) {
    std::lock_guard<std::mutex> l(s_ws_mx);
    for (int f : s_ws_fds) if (f == fd) return;
    if (s_ws_fds.size() >= 3) s_ws_fds.erase(s_ws_fds.begin());   // aeltesten verdraengen (Socket-Reserve)
    s_ws_fds.push_back(fd);
}

static void ws_remove(int fd) {
    std::lock_guard<std::mutex> l(s_ws_mx);
    for (size_t i = 0; i < s_ws_fds.size(); ++i) if (s_ws_fds[i] == fd) { s_ws_fds.erase(s_ws_fds.begin() + (long)i); return; }
}

// Wird vom Sim-Task gerufen: nur den neuesten Zustand merken, Senden macht ein eigener Task,
// damit ein langsamer Client die Simulation (und damit die UDP-Pakete) nie aufhaelt.
static void on_state(const std::string &json) {
    {
        std::lock_guard<std::mutex> l(s_ws_mx);
        if (s_ws_fds.empty()) return;
        s_ws_latest = json;
    }
    if (s_ws_task) xTaskNotifyGive(s_ws_task);
}

static void ws_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::string msg;
        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> l(s_ws_mx);
            msg.swap(s_ws_latest);
            fds = s_ws_fds;
        }
        if (msg.empty()) continue;
        httpd_ws_frame_t f = {};
        f.type = HTTPD_WS_TYPE_TEXT;
        f.payload = (uint8_t *)msg.data();
        f.len = msg.size();
        for (int fd : fds) {
            if (httpd_ws_get_fd_info(s_srv, fd) != HTTPD_WS_CLIENT_WEBSOCKET || httpd_ws_send_data(s_srv, fd, &f) != ESP_OK)
                ws_remove(fd);
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;            // Handshake (aeltere IDF-Versionen)
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
    if (f.len > 0 && f.len <= 128) {
        uint8_t buf[129];
        f.payload = buf;
        if (httpd_ws_recv_frame(req, &f, f.len) != ESP_OK) return ESP_FAIL;
    }
    ws_add(httpd_req_to_sockfd(req));                       // Client meldet sich mit einem beliebigen Frame an ("hello")
    return ESP_OK;
}

static void on_close(httpd_handle_t, int fd) {
    ws_remove(fd);
    close(fd);
}

// ------------------------------------------------------------------ Hilfen
static esp_err_t send_json(httpd_req_t *r, const std::string &s) {
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, s.data(), (ssize_t)s.size());
}

static esp_err_t bad(httpd_req_t *r, const char *msg) {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, msg);
    return ESP_FAIL;
}

// Body (max. 2 kB) als JSON lesen; Aufrufer gibt mit cJSON_Delete frei
static cJSON *read_json(httpd_req_t *r) {
    if (r->content_len == 0 || r->content_len > 2048) return nullptr;
    std::string b(r->content_len, '\0');
    size_t got = 0;
    while (got < b.size()) {
        int n = httpd_req_recv(r, &b[got], b.size() - got);
        if (n <= 0) return nullptr;
        got += (size_t)n;
    }
    return cJSON_ParseWithLength(b.data(), b.size());
}

static double jnum(cJSON *o, const char *k, double def) {
    cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(i) ? i->valuedouble : def;
}
static const char *jstr(cJSON *o, const char *k) {
    cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(i) && i->valuestring ? i->valuestring : nullptr;
}
static bool jhas(cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k) != nullptr; }

static void reboot_later(void *) { vTaskDelay(pdMS_TO_TICKS(800)); esp_restart(); }

// ------------------------------------------------------------------ REST
static esp_err_t h_lists(httpd_req_t *r) { return send_json(r, sim::lists_json()); }
static esp_err_t h_scenario(httpd_req_t *r) { return send_json(r, sim::scenario_json()); }
static esp_err_t h_antenna_get(httpd_req_t *r) { return send_json(r, sim::antenna_json()); }
static esp_err_t h_state(httpd_req_t *r) { return send_json(r, sim::state_json()); }

static esp_err_t h_antenna_put(httpd_req_t *r) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> j(read_json(r), cJSON_Delete);
    if (!j) return bad(r, "JSON erwartet");
    if (const char *f = jstr(j.get(), "file")) {
        if (!sim::set_antenna(f)) return bad(r, "Antenne nicht gefunden");
    }
    if (jhas(j.get(), "heading")) sim::set_heading((float)jnum(j.get(), "heading", 0));
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_rx(httpd_req_t *r) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> j(read_json(r), cJSON_Delete);
    if (!j || !jhas(j.get(), "lat") || !jhas(j.get(), "lon")) return bad(r, "lat/lon erwartet");
    sim::set_rx((float)jnum(j.get(), "lat", 0), (float)jnum(j.get(), "lon", 0));
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_sim(httpd_req_t *r) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> j(read_json(r), cJSON_Delete);
    if (!j) return bad(r, "JSON erwartet");
    cJSON *o = j.get();
    AppConfig &c = cfg();
    if (jhas(o, "model")) sim::set_model((int)jnum(o, "model", 0));
    if (jhas(o, "speed")) sim::set_speed((int)jnum(o, "speed", 1));
    if (cJSON *ck = cJSON_GetObjectItemCaseSensitive(o, "clock"))
        sim::set_clock((int)jnum(ck, "y", 1975), (int)jnum(ck, "mo", 10), (int)jnum(ck, "d", 15), (float)jnum(ck, "h", 12));
    if (jhas(o, "ntp")) sim::follow_ntp(jnum(o, "ntp", 0) != 0);
    if (jhas(o, "sens") || jhas(o, "gm") || jhas(o, "gw")) {
        c.sens = (float)jnum(o, "sens", c.sens);
        c.gain_mode = (uint8_t)jnum(o, "gm", c.gain_mode);
        c.gain_window = (uint8_t)jnum(o, "gw", c.gain_window);
        sim::set_params(c.sens, c.gain_mode, c.gain_window);
    }
    if (const char *s = jstr(o, "scenario")) {
        if (!sim::set_scenario(s)) return bad(r, "Szenario nicht gefunden");
    }
    if (jhas(o, "carriers")) {                  // 0..1 = Traegertest, -1 = zurueck zur Simulation
        double v = jnum(o, "carriers", -1);
        if (v < 0) sim::carriers_release(); else sim::carriers((float)v);
    }
    if (jhas(o, "jumpers")) { c.jumpers = jnum(o, "jumpers", 1) != 0; config_save(); }
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_storm(httpd_req_t *r) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> j(read_json(r), cJSON_Delete);
    if (!j) return bad(r, "JSON erwartet");
    const char *op = jstr(j.get(), "op");
    if (!op) return bad(r, "op erwartet");
    if (!std::strcmp(op, "start")) sim::storm_start((float)jnum(j.get(), "lat", 50), (float)jnum(j.get(), "lon", 10), (int)jnum(j.get(), "profile", 1));
    else if (!std::strcmp(op, "set")) sim::storm_set((float)jnum(j.get(), "spd", 40), (float)jnum(j.get(), "hdg", 270));
    else if (!std::strcmp(op, "stop")) sim::storm_stop();
    else return bad(r, "op unbekannt");
    if (jhas(j.get(), "spd") && !std::strcmp(op, "start")) sim::storm_set((float)jnum(j.get(), "spd", 40), (float)jnum(j.get(), "hdg", 270));
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_config_get(httpd_req_t *r) {
    AppConfig &c = cfg();
    char b[400];
    std::snprintf(b, sizeof b,
                  "{\"ssid\":\"%s\",\"mod_host\":\"%s\",\"mod_ip\":\"%s\",\"hostname\":\"%s\",\"port_gain\":%u,\"port_sferics\":%u,\"jumpers\":%d,\"ip\":\"%s\"}",
                  c.ssid, c.mod_host, udp_out_target().c_str(), c.hostname, c.port_gain, c.port_sferics, c.jumpers, net_ip().c_str());
    return send_json(r, b);
}

static bool valid_host(const char *s) {
    size_t n = std::strlen(s);
    if (n == 0 || n > 63) return false;
    for (size_t i = 0; i < n; ++i) {
        char ch = s[i];
        if (!(std::isalnum((unsigned char)ch) || ch == '.' || ch == '-' || ch == '_')) return false;
    }
    return true;
}

static esp_err_t h_config_put(httpd_req_t *r) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> j(read_json(r), cJSON_Delete);
    if (!j) return bad(r, "JSON erwartet");
    AppConfig &c = cfg();
    bool reboot = false;
    if (const char *h = jstr(j.get(), "mod_host")) {
        if (!valid_host(h)) return bad(r, "Ungueltige Adresse");
        std::strncpy(c.mod_host, h, sizeof c.mod_host - 1);
        udp_out_set_target(c.mod_host);
    }
    if (jhas(j.get(), "port_gain")) c.port_gain = (uint16_t)jnum(j.get(), "port_gain", 8888);
    if (jhas(j.get(), "port_sferics")) c.port_sferics = (uint16_t)jnum(j.get(), "port_sferics", 8889);
    if (const char *s = jstr(j.get(), "ssid")) {
        if (std::strlen(s) > 32) return bad(r, "SSID zu lang");
        if (std::strcmp(s, c.ssid) != 0) reboot = true;
        std::strncpy(c.ssid, s, sizeof c.ssid - 1);
        c.ssid[sizeof c.ssid - 1] = 0;
    }
    if (const char *p = jstr(j.get(), "pass")) {
        if (std::strlen(p) > 64) return bad(r, "Passwort zu lang");
        std::strncpy(c.pass, p, sizeof c.pass - 1);
        reboot = true;
    }
    if (const char *h = jstr(j.get(), "hostname")) {
        if (valid_host(h) && std::strlen(h) < sizeof c.hostname && std::strcmp(h, c.hostname) != 0) {
            std::strncpy(c.hostname, h, sizeof c.hostname - 1);
            reboot = true;
        }
    }
    if (jhas(j.get(), "jumpers")) c.jumpers = jnum(j.get(), "jumpers", 1) != 0;
    config_save();
    esp_err_t e = send_json(r, reboot ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":true}");
    if (reboot) xTaskCreate(reboot_later, "reboot", 2048, nullptr, 1, nullptr);
    return e;
}

// ------------------------------------------------------------------ statische Dateien
static const char *ctype(const std::string &p) {
    auto ends = [&](const char *e) { size_t n = std::strlen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json") || ends(".geojson")) return "application/json";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t h_static(httpd_req_t *r) {
    std::string uri = r->uri;
    size_t q = uri.find_first_of("?#");
    if (q != std::string::npos) uri.resize(q);
    if (uri == "/" || uri.empty()) uri = "/index.html";
    if (uri.find("..") != std::string::npos) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "pfad"); return ESP_FAIL; }

    std::string path = "/fs/www" + uri + ".gz";
    bool gz = true;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { gz = false; path = "/fs/www" + uri; f = std::fopen(path.c_str(), "rb"); }
    if (!f) { httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "nicht gefunden"); return ESP_FAIL; }

    httpd_resp_set_type(r, ctype(uri));
    if (gz) httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    if (uri != "/index.html") httpd_resp_set_hdr(r, "Cache-Control", "max-age=86400");
    std::unique_ptr<char[]> buf(new char[1024]);
    size_t n;
    esp_err_t e = ESP_OK;
    while ((n = std::fread(buf.get(), 1, 1024, f)) > 0) {
        if ((e = httpd_resp_send_chunk(r, buf.get(), (ssize_t)n)) != ESP_OK) break;
    }
    std::fclose(f);
    if (e == ESP_OK) httpd_resp_send_chunk(r, nullptr, 0);
    return e;
}

// ------------------------------------------------------------------ Start
void web_start() {
    httpd_config_t cfgd = HTTPD_DEFAULT_CONFIG();
    cfgd.max_uri_handlers = 16;
    cfgd.max_open_sockets = 7;
    cfgd.lru_purge_enable = true;
    cfgd.stack_size = 8192;
    cfgd.uri_match_fn = httpd_uri_match_wildcard;
    cfgd.close_fn = on_close;
    cfgd.core_id = 0;
    if (httpd_start(&s_srv, &cfgd) != ESP_OK) { ESP_LOGE(TAG, "httpd_start fehlgeschlagen"); return; }

    auto reg = [&](const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *), bool ws = false) {
        httpd_uri_t u = {};
        u.uri = uri; u.method = m; u.handler = h; u.is_websocket = ws;
        if (httpd_register_uri_handler(s_srv, &u) != ESP_OK) ESP_LOGE(TAG, "URI %s nicht registriert", uri);
    };
    reg("/api/lists", HTTP_GET, h_lists);
    reg("/api/scenario", HTTP_GET, h_scenario);
    reg("/api/antenna", HTTP_GET, h_antenna_get);
    reg("/api/antenna", HTTP_PUT, h_antenna_put);
    reg("/api/state", HTTP_GET, h_state);
    reg("/api/rx", HTTP_PUT, h_rx);
    reg("/api/sim", HTTP_PUT, h_sim);
    reg("/api/storm", HTTP_PUT, h_storm);
    reg("/api/config", HTTP_GET, h_config_get);
    reg("/api/config", HTTP_PUT, h_config_put);
    reg("/ws", HTTP_GET, ws_handler, true);
    reg("/*", HTTP_GET, h_static);          // zuletzt: faengt alles Uebrige ab

    xTaskCreatePinnedToCore(ws_task, "ws", 4096, nullptr, 3, &s_ws_task, 0);
    sim::set_state_callback(on_state);
    ESP_LOGI(TAG, "Web-Server laeuft auf Port 80");
}
