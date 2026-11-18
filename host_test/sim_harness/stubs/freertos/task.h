#pragma once
#include "FreeRTOS.h"
#include <thread>
typedef void (*TaskFunction_t)(void *);
inline int xTaskCreatePinnedToCore(TaskFunction_t f, const char *, int, void *a, int, void *, int) {
    std::thread([f, a] { f(a); }).detach();
    return 1;
}
