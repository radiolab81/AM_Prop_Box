// UDP-Eingaenge wie im PC-Simulator:
//   8886: "lat,lon"                         -> Empfaengerposition (GPS/Fahrten-Player)
//   8887: "HDG:145.5"                       -> Antennenkurs
//         "GET_CONFIG"                      -> Antwort: Antennenmuster als JSON (DFscope)
//         "GET_TRANSMITTERS"                -> Antwort: Senderliste als JSON (DFscope)
#include "amp_net.hpp"
#include "sim.hpp"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <cstring>
#include <string>

static const char *TAG = "udp_in";

static int bind_udp(uint16_t port) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) return -1;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (sockaddr *)&a, sizeof a) < 0) { close(s); return -1; }
    return s;
}

static void reply(int sock, const sockaddr_in &to, const std::string &body) {
    // Grosse Antworten (Antennenmuster ca. 6 kB, Senderliste) gehen als ein Datagramm raus; lwIP fragmentiert
    sendto(sock, body.data(), body.size(), 0, (const sockaddr *)&to, sizeof to);
}

static void udp_in_task(void *) {
    int gps = bind_udp(8886), ctl = bind_udp(8887);
    if (gps < 0 || ctl < 0) { ESP_LOGE(TAG, "bind fehlgeschlagen"); vTaskDelete(nullptr); return; }
    char buf[256];
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(gps, &rd);
        FD_SET(ctl, &rd);
        int maxfd = (gps > ctl ? gps : ctl) + 1;
        if (select(maxfd, &rd, nullptr, nullptr, nullptr) <= 0) continue;

        sockaddr_in from;
        socklen_t fl = sizeof from;
        if (FD_ISSET(gps, &rd)) {
            int n = recvfrom(gps, buf, sizeof buf - 1, 0, (sockaddr *)&from, &fl);
            if (n > 0) {
                buf[n] = 0;
                char *comma = std::strchr(buf, ',');
                if (comma) {
                    *comma = 0;
                    char *e1 = nullptr, *e2 = nullptr;
                    float lat = std::strtof(buf, &e1), lon = std::strtof(comma + 1, &e2);
                    if (e1 != buf && e2 != comma + 1) sim::set_rx(lat, lon);
                }
            }
        }
        if (FD_ISSET(ctl, &rd)) {
            fl = sizeof from;
            int n = recvfrom(ctl, buf, sizeof buf - 1, 0, (sockaddr *)&from, &fl);
            if (n > 0) {
                buf[n] = 0;
                std::string m(buf);
                while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' ')) m.pop_back();
                if (m == "GET_CONFIG") reply(ctl, from, sim::dfscope_config_json());
                else if (m == "GET_TRANSMITTERS") reply(ctl, from, sim::dfscope_transmitters_json());
                else if (m.rfind("HDG:", 0) == 0) {
                    char *e = nullptr;
                    float h = std::strtof(m.c_str() + 4, &e);
                    if (e != m.c_str() + 4) sim::set_heading(h);
                }
            }
        }
    }
}

void udp_in_start() { xTaskCreatePinnedToCore(udp_in_task, "udp_in", 6144, nullptr, 4, nullptr, 1); }
