/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-29 21:00:00
 * @FilePath: \xn_voice_wake_up\main\main.c
 * @Description: esp32 语音唤醒组件 By.星年 - 云端唤醒词识别
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "xn_wifi_manage.h"
#include "http_ota_manager.h"
#include "audio_manager.h"
#include "cloud_audio.h"

static const char *TAG = "app_main";

static bool s_ota_inited = false;
static bool s_cloud_inited = false;

// 音频缓冲区 (用于收集 VAD 期间的音频)
static int16_t *s_audio_buffer = NULL;
static size_t s_audio_buffer_samples = 0;
#define AUDIO_BUFFER_MAX_SAMPLES (16000 * 5)  // 最大 5 秒

/*
 * @brief 云端音频事件回调
 */
static void on_cloud_event(const cloud_audio_event_t *event, void *user_ctx)
{
    switch (event->type) {
    case CLOUD_AUDIO_EVENT_CONNECTED:
        ESP_LOGI(TAG, "☁️ 云端已连接");
        break;
    case CLOUD_AUDIO_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "☁️ 云端断开连接");
        break;
    case CLOUD_AUDIO_EVENT_WAKE_DETECTED:
        ESP_LOGI(TAG, ">>> 检测到唤醒词: %s <<<", event->data.wake.text);
        if (event->data.wake.speaker_verified) {
            ESP_LOGI(TAG, "✅ 声纹验证通过 (%.2f)", event->data.wake.speaker_score);
        }
        // TODO: 在这里添加唤醒后的处理逻辑
        break;
    case CLOUD_AUDIO_EVENT_VOICE_VERIFIED:
        ESP_LOGI(TAG, "✅ 声纹验证通过");
        break;
    case CLOUD_AUDIO_EVENT_VOICE_REJECTED:
        ESP_LOGW(TAG, "❌ 声纹验证失败");
        break;
    case CLOUD_AUDIO_EVENT_ERROR:
        ESP_LOGE(TAG, "☁️ 云端错误: %d", event->data.error_code);
        break;
    }
}

/*
 * @brief 录音数据回调 - 收集 VAD 期间的音频
 */
static void on_record_data(const int16_t *pcm_data, size_t sample_count, void *user_ctx)
{
    if (!s_audio_buffer) return;
    
    size_t remaining = AUDIO_BUFFER_MAX_SAMPLES - s_audio_buffer_samples;
    size_t to_copy = (sample_count < remaining) ? sample_count : remaining;
    
    if (to_copy > 0) {
        memcpy(s_audio_buffer + s_audio_buffer_samples, pcm_data, to_copy * sizeof(int16_t));
        s_audio_buffer_samples += to_copy;
    }
}

/*
 * @brief 音频管理器事件回调
 */
static void on_audio_event(const audio_mgr_event_t *event, void *user_ctx)
{
    switch (event->type) {
    case AUDIO_MGR_EVENT_VAD_START:
        ESP_LOGI(TAG, "🎤 检测到人声开始");
        s_audio_buffer_samples = 0;  // 清空缓冲区
        break;
        
    case AUDIO_MGR_EVENT_VAD_END:
        ESP_LOGI(TAG, "🎤 检测到人声结束, 采样数: %d", s_audio_buffer_samples);
        // 发送音频到云端
        if (s_audio_buffer_samples > 0 && cloud_audio_is_connected()) {
            cloud_audio_send(s_audio_buffer, s_audio_buffer_samples);
        }
        break;
        
    case AUDIO_MGR_EVENT_VAD_TIMEOUT:
        ESP_LOGW(TAG, "⏰ VAD 超时");
        break;
        
    case AUDIO_MGR_EVENT_BUTTON_TRIGGER:
        ESP_LOGI(TAG, "🔘 按键触发");
        s_audio_buffer_samples = 0;
        break;
        
    case AUDIO_MGR_EVENT_BUTTON_RELEASE:
        ESP_LOGI(TAG, "🔘 按键松开");
        if (s_audio_buffer_samples > 0 && cloud_audio_is_connected()) {
            cloud_audio_send(s_audio_buffer, s_audio_buffer_samples);
        }
        break;
    }
}

/*
 * @brief 音频管理器状态回调
 */
static void on_audio_state(audio_mgr_state_t state, void *user_ctx)
{
    const char *state_str[] = {"DISABLED", "IDLE", "LISTENING", "RECORDING", "PLAYBACK"};
    ESP_LOGI(TAG, "音频状态: %s", state_str[state]);
}

/*
 * @brief 云端初始化任务
 */
static void cloud_init_task(void *arg)
{
    // 初始化云端音频
    cloud_audio_config_t cloud_cfg = CLOUD_AUDIO_DEFAULT_CONFIG();
    cloud_cfg.server_host = "117.50.176.26";
    cloud_cfg.server_port = 8000;
    cloud_cfg.user_id = "esp32_device";
    cloud_cfg.event_cb = on_cloud_event;

    esp_err_t ret = cloud_audio_init(&cloud_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cloud_audio_init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // 设置唤醒词
    ret = cloud_audio_set_wake_word("你好星年");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "设置唤醒词失败");
    }

    // 连接 WebSocket
    ret = cloud_audio_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cloud_audio_connect failed: %s", esp_err_to_name(ret));
    }

    vTaskDelete(NULL);
}

/*
 * @brief OTA 初始化任务
 */
static void ota_init_task(void *arg)
{
    http_ota_manager_config_t cfg = HTTP_OTA_MANAGER_DEFAULT_CONFIG();
    snprintf(cfg.version_url, sizeof(cfg.version_url),
             "http://win.xingnian.vip:16623/firmware/version.json");

    esp_err_t ret = http_ota_manager_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_ota_manager_init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = http_ota_manager_check_now();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_ota_manager_check_now failed: %s", esp_err_to_name(ret));
    }

    vTaskDelete(NULL);
}

/*
 * @brief WiFi 管理状态回调
 */
static void wifi_manage_event_cb(wifi_manage_state_t state)
{
    if (state != WIFI_MANAGE_STATE_CONNECTED) {
        return;
    }

    // 初始化 OTA
    if (!s_ota_inited) {
        xTaskCreate(ota_init_task, "ota_init", 1024*8, NULL, tskIDLE_PRIORITY + 2, NULL);
        s_ota_inited = true;
    }

    // 初始化云端音频
    if (!s_cloud_inited) {
        xTaskCreate(cloud_init_task, "cloud_init", 1024*6, NULL, tskIDLE_PRIORITY + 3, NULL);
        s_cloud_inited = true;
    }
}

/*
 * @brief 应用入口
 */
void app_main(void)
{
    printf("esp32 语音唤醒组件 By.星年 - 云端唤醒词识别\n");

    // 分配音频缓冲区
    s_audio_buffer = heap_caps_malloc(AUDIO_BUFFER_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_buffer) {
        s_audio_buffer = malloc(AUDIO_BUFFER_MAX_SAMPLES * sizeof(int16_t));
    }
    if (!s_audio_buffer) {
        ESP_LOGE(TAG, "音频缓冲区分配失败");
        return;
    }

    // 初始化音频管理器
    audio_mgr_config_t audio_cfg = AUDIO_MANAGER_DEFAULT_CONFIG();
    
    // 硬件引脚配置
    audio_cfg.hw_config.mic.bclk_gpio = 15;
    audio_cfg.hw_config.mic.lrck_gpio = 2;
    audio_cfg.hw_config.mic.din_gpio = 39;
    audio_cfg.hw_config.mic.sample_rate = 16000;
    audio_cfg.hw_config.mic.bits = 32;
    audio_cfg.hw_config.mic.bit_shift = 14;
    audio_cfg.hw_config.button.gpio = -1;
    
    // VAD 配置
    audio_cfg.vad_config.enabled = true;
    audio_cfg.vad_config.vad_mode = 2;
    audio_cfg.vad_config.min_speech_ms = 200;
    audio_cfg.vad_config.min_silence_ms = 400;
    audio_cfg.vad_config.vad_timeout_ms = 8000;
    audio_cfg.vad_config.vad_end_delay_ms = 1200;
    
    // 回调配置
    audio_cfg.event_callback = on_audio_event;
    audio_cfg.state_callback = on_audio_state;
    audio_cfg.user_ctx = NULL;

    esp_err_t ret = audio_manager_init(&audio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_manager_init failed: %s", esp_err_to_name(ret));
    } else {
        // 注册录音数据回调
        audio_manager_set_record_callback(on_record_data, NULL);
        
        // 开始监听
        ret = audio_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "audio_manager_start failed: %s", esp_err_to_name(ret));
        }
    }

    // 初始化 WiFi 管理
    wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
    wifi_cfg.wifi_event_cb = wifi_manage_event_cb;

    ret = wifi_manage_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
    }
}
