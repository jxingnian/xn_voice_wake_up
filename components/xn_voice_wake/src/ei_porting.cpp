/**
 * @file ei_porting.cpp
 * @brief Edge Impulse SDK ESP32 平台适配层
 */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Edge Impulse 需要的函数实现

extern "C" void *ei_malloc(size_t size) {
    // 优先使用 PSRAM
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

extern "C" void *ei_calloc(size_t nitems, size_t size) {
    // 优先使用 PSRAM
    void *ptr = heap_caps_calloc(nitems, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = calloc(nitems, size);
    }
    return ptr;
}

extern "C" void ei_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

extern "C" uint64_t ei_read_timer_us() {
    return (uint64_t)esp_timer_get_time();
}

extern "C" uint64_t ei_read_timer_ms() {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

extern "C" void ei_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

extern "C" void ei_printf_float(float f) {
    printf("%.6f", f);
}

extern "C" void ei_sleep(int32_t time_ms) {
    vTaskDelay(pdMS_TO_TICKS(time_ms));
}

extern "C" EI_IMPULSE_ERROR ei_run_impulse_check_canceled() {
    return EI_IMPULSE_OK;
}

// FreeRTOS 头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
