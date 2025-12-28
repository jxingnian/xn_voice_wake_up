/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-26 23:30:00
 * @FilePath: \xn_voice_wake_up\main\main.c
 * @Description: esp32 语音唤醒组件 By.星年
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "xn_wifi_manage.h"
#include "http_ota_manager.h"
#include "voice_wake_module.h"

static const char *TAG = "app_main";

/* 仅在首次拿到 IP 后初始化一次 OTA 管理 */
static bool s_ota_inited = false;

/*
 * @brief 唤醒词检测回调
 */
static void on_wake_word_detected(int model_index, float confidence, void *user_data)
{
	ESP_LOGI(TAG, ">>> 检测到唤醒词! 置信度: %.2f <<<", confidence);
	// TODO: 在这里添加唤醒后的处理逻辑
}

/*
 * @brief 状态变化回调
 */
static void on_wake_state_changed(voice_wake_state_t state, void *user_data)
{
	const char *state_str[] = {"IDLE", "LISTENING", "DETECTED", "ERROR"};
	ESP_LOGI(TAG, "语音唤醒状态: %s", state_str[state]);
}

/*
 * @brief OTA 初始化任务
 *
 * 在独立任务栈中调用 ota_manage_init，避免在 sys_evt 任务中发生栈溢出。
 */
static void ota_init_task(void *arg)
{
	(void)arg;

	http_ota_manager_config_t cfg = HTTP_OTA_MANAGER_DEFAULT_CONFIG();
	snprintf(cfg.version_url,
		 sizeof(cfg.version_url),
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
 *
 * 当 WiFi 管理状态变为 CONNECTED（已拿到 IP）时，创建任务初始化 OTA 管理模块。
 */
static void wifi_manage_event_cb(wifi_manage_state_t state)
{
	if (state != WIFI_MANAGE_STATE_CONNECTED || s_ota_inited) {
		return;
	}

	BaseType_t ret = xTaskCreate(ota_init_task,
				    "ota_init",
				    1024*8,
				    NULL,
				    tskIDLE_PRIORITY + 2,
				    NULL);
	if (ret != pdPASS) {
		ESP_LOGE(TAG, "create ota_init task failed");
		return;
	}

	s_ota_inited = true;
}

/*
 * @brief 应用入口：初始化 WiFi 管理，WiFi 连上后再初始化 OTA 管理。
 */
void app_main(void)
{
	printf("esp32 语音唤醒组件 By.星年\n");

	// 初始化语音唤醒模块
	voice_wake_config_t wake_cfg = VOICE_WAKE_DEFAULT_CONFIG();
	// 硬件引脚配置（与 xn_audio_manager 一致）
	wake_cfg.i2s_bck_pin = 15;      // BCLK
	wake_cfg.i2s_ws_pin = 2;        // LRCK
	wake_cfg.i2s_data_pin = 39;     // DIN
	wake_cfg.detect_threshold = 0.85f;  // 提高阈值减少误触发
	wake_cfg.detect_cb = on_wake_word_detected;
	wake_cfg.state_cb = on_wake_state_changed;

	esp_err_t ret = voice_wake_init(&wake_cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "voice_wake_init failed: %s", esp_err_to_name(ret));
	} else {
		// 开始监听
		voice_wake_start();
	}

	// 初始化 WiFi 管理
	wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
	wifi_cfg.wifi_event_cb        = wifi_manage_event_cb;

	ret = wifi_manage_init(&wifi_cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
	}
}
