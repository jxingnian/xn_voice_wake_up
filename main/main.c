/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-30 09:00:57
 * @FilePath: \xn_voice_wake_up\main\main.c
 * @Description: esp32 语音唤醒组件 By.星年 - FunASR 云端唤醒词识别
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "xn_wifi_manage.h"
#include "http_ota_manager.h"
#include "audio_manager.h"

static const char *TAG = "app_main";

static bool s_ota_inited = false;

/*
 * @brief 音频管理器事件回调
 */
static void on_audio_event(const audio_mgr_event_t *event, void *user_ctx)
{
    switch (event->type) {
    case AUDIO_MGR_EVENT_VAD_START:
        ESP_LOGI(TAG, "🎤 检测到人声开始");
        break;
    case AUDIO_MGR_EVENT_VAD_END:
        ESP_LOGI(TAG, "🎤 检测到人声结束");
        break;
    case AUDIO_MGR_EVENT_VAD_TIMEOUT:
        ESP_LOGW(TAG, "⏰ VAD 超时");
        break;
    default:
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
    const char *state_names[] = {"IDLE", "CONNECTING", "CONNECTED", "DISCONNECTED", "AP_MODE"};
    ESP_LOGI(TAG, "📶 WiFi 状态: %s", state_names[state]);
    
    if (state != WIFI_MANAGE_STATE_CONNECTED) {
        return;
    }

    // 初始化 OTA
    if (!s_ota_inited) {
        xTaskCreate(ota_init_task, "ota_init", 1024*8, NULL, tskIDLE_PRIORITY + 2, NULL);
        s_ota_inited = true;
    }

    // TODO: WiFi 连接后初始化云端唤醒词服务
}

/*
 * @brief 应用入口
 */
void app_main(void)
{
    printf("esp32 语音唤醒组件 By.星年 - FunASR 云端唤醒词识别\n");

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
