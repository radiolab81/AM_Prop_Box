#pragma once
#include <chrono>
#include <thread>
#define pdMS_TO_TICKS(x) (x)
typedef int BaseType_t;
inline void vTaskDelay(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
