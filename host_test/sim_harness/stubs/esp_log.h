#pragma once
#include <cstdio>
#define ESP_LOGI(t, f, ...) std::printf("I (%s) " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) std::printf("W (%s) " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) std::printf("E (%s) " f "\n", t, ##__VA_ARGS__)
