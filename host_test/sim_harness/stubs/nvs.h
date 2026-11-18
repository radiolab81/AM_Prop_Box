#pragma once
#include <cstddef>
typedef int nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
enum { NVS_READONLY, NVS_READWRITE };
inline esp_err_t nvs_open(const char *, int, nvs_handle_t *) { return 1; }
inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *) { return 1; }
inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t) { return 0; }
inline esp_err_t nvs_commit(nvs_handle_t) { return 0; }
inline void nvs_close(nvs_handle_t) {}
