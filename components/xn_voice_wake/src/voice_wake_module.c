/**
 * @file voice_wake_module.c
 * @brief 语音唤醒主模块实现
 */

#include "voice_wake_module.h"
#include "audio_processor.h"
#include "detection_engine.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include <string.h>

static const char *TAG = "voice_wake";

// 模块状态
static struct {
    voice_wake_config_t config;
    voice_wake_state_t state;
    esp_err_t last_error;
    bool initialized;
} s_module = {0};

// 状态变化通知
static void notify_state_change(voice_wake_state_t new_state)
{
    if (s_module.state != new_state) {
        s_module.state = new_state;
        if (s_module.config.state_cb) {
            s_module.config.state_cb(new_state, s_module.config.user_data);
        }
    }
}

// 检测结果回调
static void on_detection(int model_index, float confidence)
{
    notify_state_change(VOICE_WAKE_STATE_DETECTED);
    
    if (s_module.config.detect_cb) {
        s_module.config.detect_cb(model_index, confidence, s_module.config.user_data);
    }
    
    // 检测后自动恢复到监听状态
    notify_state_change(VOICE_WAKE_STATE_LISTENING);
}

// 音频数据回调
static void on_audio_data(const int16_t *data, size_t samples)
{
    detection_engine_process(data, samples);
}

esp_err_t voice_wake_init(const voice_wake_config_t *config)
{
    if (s_module.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // 检查芯片类型
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    if (chip_info.model != CHIP_ESP32S3) {
        ESP_LOGE(TAG, "This module requires ESP32-S3");
        s_module.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    // 使用默认配置或用户配置
    if (config) {
        memcpy(&s_module.config, config, sizeof(voice_wake_config_t));
    } else {
        voice_wake_config_t default_cfg = VOICE_WAKE_DEFAULT_CONFIG();
        memcpy(&s_module.config, &default_cfg, sizeof(voice_wake_config_t));
    }

    // 初始化音频处理器
    audio_processor_config_t audio_cfg = {
        .bck_pin = s_module.config.i2s_bck_pin,
        .ws_pin = s_module.config.i2s_ws_pin,
        .data_pin = s_module.config.i2s_data_pin,
        .sample_rate = 16000,
        .bits_per_sample = 16,
    };

    esp_err_t ret = audio_processor_init(&audio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio processor: %s", esp_err_to_name(ret));
        s_module.last_error = ret;
        return ret;
    }

    // 初始化检测引擎
    detection_engine_config_t detect_cfg = {
        .threshold = s_module.config.detect_threshold,
        .window_size = s_module.config.sliding_window_size,
        .cooldown_ms = s_module.config.cooldown_ms,
    };

    ret = detection_engine_init(&detect_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init detection engine: %s", esp_err_to_name(ret));
        audio_processor_deinit();
        s_module.last_error = ret;
        return ret;
    }

    // 设置检测回调
    detection_engine_set_callback(on_detection);

    s_module.state = VOICE_WAKE_STATE_IDLE;
    s_module.initialized = true;
    
    ESP_LOGI(TAG, "Voice wake module initialized");
    return ESP_OK;
}

esp_err_t voice_wake_start(void)
{
    if (!s_module.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_module.state == VOICE_WAKE_STATE_LISTENING) {
        return ESP_OK;
    }

    esp_err_t ret = audio_processor_start(on_audio_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio: %s", esp_err_to_name(ret));
        notify_state_change(VOICE_WAKE_STATE_ERROR);
        s_module.last_error = ret;
        return ret;
    }

    notify_state_change(VOICE_WAKE_STATE_LISTENING);
    ESP_LOGI(TAG, "Voice wake started listening");
    
    return ESP_OK;
}

esp_err_t voice_wake_stop(void)
{
    if (!s_module.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = audio_processor_stop();
    if (ret != ESP_OK) {
        s_module.last_error = ret;
        return ret;
    }

    notify_state_change(VOICE_WAKE_STATE_IDLE);
    ESP_LOGI(TAG, "Voice wake stopped");
    
    return ESP_OK;
}

voice_wake_state_t voice_wake_get_state(void)
{
    return s_module.state;
}

esp_err_t voice_wake_deinit(void)
{
    if (!s_module.initialized) {
        return ESP_OK;
    }

    voice_wake_stop();
    detection_engine_deinit();
    audio_processor_deinit();

    s_module.initialized = false;
    s_module.state = VOICE_WAKE_STATE_IDLE;
    
    ESP_LOGI(TAG, "Voice wake module deinitialized");
    return ESP_OK;
}

esp_err_t voice_wake_get_last_error(void)
{
    return s_module.last_error;
}
