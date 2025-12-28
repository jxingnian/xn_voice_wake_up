/**
 * @file detection_engine.cpp
 * @brief 检测引擎实现 (使用 Edge Impulse SDK)
 */

#include "detection_engine.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "detect_eng";

// 音频缓冲区大小 (1秒 @ 16kHz)
#define AUDIO_BUFFER_SIZE   EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE

// 滑动窗口最大大小
#define MAX_WINDOW_SIZE     10

// 模块状态
static struct {
    detection_engine_config_t config;
    detection_result_cb_t callback;
    
    // 音频缓冲区
    float *audio_buffer;
    size_t buffer_index;
    
    // 滑动窗口
    float *window_buffer;
    int window_index;
    int window_count;
    
    // 冷却控制
    int64_t last_detect_time;
    
    bool initialized;
    SemaphoreHandle_t mutex;
} s_engine = {0};

// 获取音频数据回调 (Edge Impulse 需要)
static int get_audio_data(size_t offset, size_t length, float *out_ptr)
{
    if (!s_engine.audio_buffer) {
        return -1;
    }
    
    memcpy(out_ptr, s_engine.audio_buffer + offset, length * sizeof(float));
    return 0;
}

extern "C" esp_err_t detection_engine_init(const detection_engine_config_t *config)
{
    if (s_engine.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_engine.config, config, sizeof(detection_engine_config_t));

    // 分配音频缓冲区 (使用 PSRAM)
    s_engine.audio_buffer = (float *)heap_caps_malloc(
        AUDIO_BUFFER_SIZE * sizeof(float), 
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!s_engine.audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return ESP_ERR_NO_MEM;
    }
    memset(s_engine.audio_buffer, 0, AUDIO_BUFFER_SIZE * sizeof(float));

    // 分配滑动窗口缓冲区
    int window_size = config->window_size;
    if (window_size > MAX_WINDOW_SIZE) {
        window_size = MAX_WINDOW_SIZE;
    }
    
    s_engine.window_buffer = (float *)malloc(window_size * sizeof(float));
    if (!s_engine.window_buffer) {
        ESP_LOGE(TAG, "Failed to allocate window buffer");
        heap_caps_free(s_engine.audio_buffer);
        return ESP_ERR_NO_MEM;
    }
    memset(s_engine.window_buffer, 0, window_size * sizeof(float));

    // 创建互斥锁
    s_engine.mutex = xSemaphoreCreateMutex();
    if (!s_engine.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        heap_caps_free(s_engine.audio_buffer);
        free(s_engine.window_buffer);
        return ESP_ERR_NO_MEM;
    }

    s_engine.buffer_index = 0;
    s_engine.window_index = 0;
    s_engine.window_count = 0;
    s_engine.last_detect_time = 0;
    s_engine.initialized = true;

    ESP_LOGI(TAG, "Detection engine initialized (threshold: %.2f, window: %d)", 
             config->threshold, config->window_size);

    return ESP_OK;
}

extern "C" esp_err_t detection_engine_process(const int16_t *audio_data, size_t samples)
{
    if (!s_engine.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!audio_data || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_engine.mutex, portMAX_DELAY);

    // 将 int16 转换为 float 并添加到缓冲区
    for (size_t i = 0; i < samples; i++) {
        s_engine.audio_buffer[s_engine.buffer_index] = (float)audio_data[i];
        s_engine.buffer_index++;
        
        // 缓冲区满，执行推理
        if (s_engine.buffer_index >= AUDIO_BUFFER_SIZE) {
            // 准备信号
            signal_t signal;
            signal.total_length = AUDIO_BUFFER_SIZE;
            signal.get_data = &get_audio_data;

            // 执行推理
            ei_impulse_result_t result = {0};
            EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

            if (err == EI_IMPULSE_OK) {
                // 打印所有类别的概率（调试用）
                ESP_LOGI(TAG, "noise: %.2f, unknown: %.2f, wake_word: %.2f",
                    result.classification[0].value,
                    result.classification[1].value,
                    result.classification[2].value);
                
                // 查找 wake_word 标签的概率
                float wake_prob = 0.0f;
                int wake_index = -1;
                
                for (size_t j = 0; j < EI_CLASSIFIER_LABEL_COUNT; j++) {
                    if (strcmp(result.classification[j].label, "wake_word") == 0) {
                        wake_prob = result.classification[j].value;
                        wake_index = j;
                        break;
                    }
                }

                // 更新滑动窗口
                s_engine.window_buffer[s_engine.window_index] = wake_prob;
                s_engine.window_index = (s_engine.window_index + 1) % s_engine.config.window_size;
                if (s_engine.window_count < s_engine.config.window_size) {
                    s_engine.window_count++;
                }

                // 计算滑动窗口平均值
                float avg_prob = 0.0f;
                for (int j = 0; j < s_engine.window_count; j++) {
                    avg_prob += s_engine.window_buffer[j];
                }
                avg_prob /= s_engine.window_count;

                // 检查是否超过阈值
                int64_t now = esp_timer_get_time() / 1000;  // ms
                bool in_cooldown = (now - s_engine.last_detect_time) < s_engine.config.cooldown_ms;

                if (avg_prob >= s_engine.config.threshold && !in_cooldown) {
                    ESP_LOGI(TAG, "Wake word detected! confidence: %.2f", avg_prob);
                    s_engine.last_detect_time = now;
                    
                    if (s_engine.callback) {
                        s_engine.callback(wake_index, avg_prob);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Classifier error: %d", err);
            }

            // 滑动缓冲区 (保留后半部分)
            size_t keep_samples = AUDIO_BUFFER_SIZE / 2;
            memmove(s_engine.audio_buffer, 
                    s_engine.audio_buffer + keep_samples, 
                    keep_samples * sizeof(float));
            s_engine.buffer_index = keep_samples;
        }
    }

    xSemaphoreGive(s_engine.mutex);
    return ESP_OK;
}

extern "C" esp_err_t detection_engine_set_callback(detection_result_cb_t callback)
{
    s_engine.callback = callback;
    return ESP_OK;
}

extern "C" esp_err_t detection_engine_deinit(void)
{
    if (!s_engine.initialized) {
        return ESP_OK;
    }

    if (s_engine.mutex) {
        vSemaphoreDelete(s_engine.mutex);
        s_engine.mutex = NULL;
    }

    if (s_engine.audio_buffer) {
        heap_caps_free(s_engine.audio_buffer);
        s_engine.audio_buffer = NULL;
    }

    if (s_engine.window_buffer) {
        free(s_engine.window_buffer);
        s_engine.window_buffer = NULL;
    }

    s_engine.initialized = false;
    ESP_LOGI(TAG, "Detection engine deinitialized");

    return ESP_OK;
}
