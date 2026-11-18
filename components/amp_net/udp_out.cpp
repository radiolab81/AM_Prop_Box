#include "amp_net.hpp"
#include "amp_config.hpp"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cerrno>
#include <cstring>
#include <mutex>

static const char *TAG = "udp_out";
static int s_sock = -1;
static std::mutex s_mx;
static char s_host[64] = "";
static sockaddr_in s_dst = {};
static bool s_have_dst = false;
static unsigned s_sent = 0, s_err = 0;
static volatile bool s_ok = false;
static volatile bool s_resolve_now = false;

static bool resolve_host(const char *host, in_addr *out) {
    if (inet_pton(AF_INET, host, out) == 1) return true;
    size_t n = std::strlen(host);
    if (n > 6 && std::strcmp(host + n - 6, ".local") == 0) {
        char name[64];
        std::strncpy(name, host, n - 6); name[n - 6] = 0;
        esp_ip4_addr_t a;
        if (mdns_query_a(name, 2000, &a) == ESP_OK) { out->s_addr = a.addr; return true; }
        return false;
    }
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) == 0 && res) {
        *out = ((sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
        return true;
    }
    return false;
}

// Hintergrund: Adresse (neu) aufloesen, bei Hostnamen alle 60 s
static void resolver_task(void *) {
    TickType_t last = 0;
    for (;;) {
        bool need = s_resolve_now || !s_have_dst || (xTaskGetTickCount() - last) > pdMS_TO_TICKS(60000);
        if (need) {
            s_resolve_now = false;
            char host[64];
            { std::lock_guard<std::mutex> l(s_mx); std::strncpy(host, s_host, sizeof host); }
            in_addr a;
            if (host[0] && resolve_host(host, &a)) {
                std::lock_guard<std::mutex> l(s_mx);
                s_dst.sin_family = AF_INET;
                s_dst.sin_addr = a;
                s_have_dst = true;
                ESP_LOGI(TAG, "Ziel %s -> %s", host, inet_ntoa(a));
            }
            last = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void udp_out_init() {
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) ESP_LOGE(TAG, "socket() fehlgeschlagen");
    udp_out_set_target(cfg().mod_host);
    xTaskCreate(resolver_task, "udp_res", 4096, nullptr, 3, nullptr);
}

void udp_out_set_target(const char *host) {
    std::lock_guard<std::mutex> l(s_mx);
    std::strncpy(s_host, host ? host : "", sizeof s_host - 1);
    s_host[sizeof s_host - 1] = 0;
    s_have_dst = false;
    s_resolve_now = true;
}

std::string udp_out_target() {
    std::lock_guard<std::mutex> l(s_mx);
    return s_have_dst ? std::string(inet_ntoa(s_dst.sin_addr)) : std::string();
}

static void send_to(uint16_t port, const std::string &msg) {
    sockaddr_in dst;
    {
        std::lock_guard<std::mutex> l(s_mx);
        if (!s_have_dst || s_sock < 0) { s_ok = false; return; }
        dst = s_dst;
    }
    dst.sin_port = htons(port);
    for (int attempt = 0; attempt < 3; ++attempt) {
        int r = sendto(s_sock, msg.data(), msg.size(), 0, (sockaddr *)&dst, sizeof dst);
        if (r >= 0) { ++s_sent; s_ok = true; return; }
        if (errno == ENOMEM || errno == EAGAIN) { vTaskDelay(1); continue; }   // Pufferstau: kurz warten
        break;
    }
    ++s_err;
    s_ok = false;
}

void udp_send_gain(const std::string &msg) { send_to(cfg().port_gain, msg); }
void udp_send_sferic(const std::string &msg) { send_to(cfg().port_sferics, msg); }
unsigned udp_out_sent() { return s_sent; }
unsigned udp_out_errors() { return s_err; }
bool udp_out_ok() { return s_ok; }
