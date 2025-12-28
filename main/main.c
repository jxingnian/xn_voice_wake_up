/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-28 20:30:00
 * @FilePath: \xn_voice_wake_up\main\main.c
 * @Description: esp32 语音唤醒组件 By.星年 - 使用 MultiNet 命令词识别
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "xn_wifi_manage.h"
#include "http_ota_manager.h"
#include "audio_manager.h"

static const char *TAG = "app_main";

/* 仅在首次拿到 IP 后初始化一次 OTA 管理 */
static bool s_ota_inited = false;

/*
 * @brief 音频管理器事件回调
 */
static void on_audio_event(const audio_mgr_event_t *event, void *user_ctx)
{
	switch (event->type) {
	case AUDIO_MGR_EVENT_WAKEUP_DETECTED:
		ESP_LOGI(TAG, ">>> 检测到唤醒词! 索引=%d, 音量=%.1f dB <<<",
			 event->data.wakeup.wake_word_index,
			 event->data.wakeup.volume_db);
		// TODO: 在这里添加唤醒后的处理逻辑
		break;
	case AUDIO_MGR_EVENT_VAD_START:
		ESP_LOGI(TAG, "检测到人声开始");
		break;
	case AUDIO_MGR_EVENT_VAD_END:
		ESP_LOGI(TAG, "检测到人声结束");
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
	ESP_LOGI(TAG, "音频管理器状态: %s", state_str[state]);
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
	printf("esp32 语音唤醒组件 By.星年 - MultiNet 命令词识别\n");

	// 初始化音频管理器（使用 MultiNet 命令词识别）
	audio_mgr_config_t audio_cfg = AUDIO_MANAGER_DEFAULT_CONFIG();
	
	// 硬件引脚配置
	audio_cfg.hw_config.mic.bclk_gpio = 15;    // BCLK
	audio_cfg.hw_config.mic.lrck_gpio = 2;     // LRCK/WS
	audio_cfg.hw_config.mic.din_gpio = 39;     // DIN
	audio_cfg.hw_config.mic.sample_rate = 16000;
	audio_cfg.hw_config.mic.bits = 32;
	audio_cfg.hw_config.mic.bit_shift = 14;
	audio_cfg.hw_config.button.gpio = -1;      // 不使用按键
	
	// 唤醒词配置 - 使用 MultiNet 命令词识别
	audio_cfg.wakeup_config.enabled = true;
	audio_cfg.wakeup_config.use_multinet = true;  // 使用 MultiNet
	audio_cfg.wakeup_config.wake_word_name = "ni hao xing nian";  // 拼音格式
	audio_cfg.wakeup_config.model_partition = "model";
	audio_cfg.wakeup_config.sensitivity = 2;
	
	// 回调配置
	audio_cfg.event_callback = on_audio_event;
	audio_cfg.state_callback = on_audio_state;
	audio_cfg.user_ctx = NULL;

	esp_err_t ret = audio_manager_init(&audio_cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "audio_manager_init failed: %s", esp_err_to_name(ret));
	} else {
		// 开始监听
		ret = audio_manager_start();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "audio_manager_start failed: %s", esp_err_to_name(ret));
		}
	}

	// 初始化 WiFi 管理
	wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
	wifi_cfg.wifi_event_cb        = wifi_manage_event_cb;

	ret = wifi_manage_init(&wifi_cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
	}
}
