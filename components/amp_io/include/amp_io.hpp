#pragma once

// UDP-Eingaenge 8886 (GPS "lat,lon") und 8887 (HDG:, GET_CONFIG, GET_TRANSMITTERS)
void udp_in_start();

// Jumper, Taster und Status-LED
void gpio_ui_init();
bool gpio_setup_requested();     // Taster beim Start gehalten -> Setup-AP erzwingen
void gpio_ui_start();            // Task: Jumper/Taster/LED

// Serielle Konsole (UART0)
void console_start();
