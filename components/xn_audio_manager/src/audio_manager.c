/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-29 20:21:41
 * @FilePath: \xn_voice_wake_up\components\xn_audio_manager\src\audio_manager.c
 * @Description: 音频管理器实现 - 硬件配置、VAD、录音、播放
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "audio_manager.h"
#include "ring_buffer.h"
#include "playback_controller.h"
#include "button_handler.h"
#include "afe_wrapper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "AUDIO_MGR";

typedef enum {
    AUDIO_INT_EVT_START_LISTEN = 0,
    AUDIO_INT_EVT_STOP_LISTEN,
    AUDIO_INT_EVT_BUTTON_PRESS,
    AUDIO_INT_EVT_BUTTON_RELEASE,
    AUDIO_INT_EVT_VAD_START,
    AUDIO_INT_EVT_VAD_END,
    AUDIO_INT_EVT_VAD_TIMEOUT,
} audio_mgr_internal_event_t;

typedef struct {
    audio_mgr_internal_event_t type;
} audio_mgr_internal_msg_t;

typedef struct {
    audio_mgr_config_t config;
    audio_bsp_handle_t bsp;
    playback_controller_handle_t playback_ctrl;
    button_handler_handle_t button_handler;
    afe_wrapper_handle_t afe_wrapper;
    ring_buffer_handle_t reference_rb;
    bool initialized;
    bool running;
    bool recording;
    bool playing;
    uint8_t volume;
    audio_mgr_state_t state;
    bool vad_active;
    TickType_t vad_deadline_tick;
    audio_record_callback_t record_callback;
    void *record_ctx;
    QueueHandle_t event_queue;
    TaskHandle_t manager_task;
} audio_manager_ctx_t;

static audio_manager_ctx_t s_ctx = {0};

static void audio_manager_set_state(audio_mgr_state_t new_state);
static void audio_manager_refresh_state(void);
static void audio_manager_notify_event(const audio_mgr_event_t *event);
static bool audio_manager_post_event(const audio_mgr_internal_msg_t *msg);
static void audio_manager_handle_internal_event(const audio_mgr_internal_msg_t *msg);
static void audio_manager_task(void *arg);
static void audio_manager_tick(void);
static void audio_manager_arm_vad_timer(int duration_ms);
static void audio_manager_clear_vad_timer(void);


static void audio_manager_set_state(audio_mgr_state_t new_state)
{
    if (s_ctx.state == new_state) return;
    s_ctx.state = new_state;
    ESP_LOGD(TAG, "state -> %d", new_state);
    if (s_ctx.config.state_callback) {
        s_ctx.config.state_callback(new_state, s_ctx.config.user_ctx);
    }
}

static void audio_manager_refresh_state(void)
{
    if (!s_ctx.initialized) {
        audio_manager_set_state(AUDIO_MGR_STATE_DISABLED);
        return;
    }
    if (s_ctx.playing) {
        audio_manager_set_state(AUDIO_MGR_STATE_PLAYBACK);
    } else if (s_ctx.recording) {
        audio_manager_set_state(AUDIO_MGR_STATE_RECORDING);
    } else if (s_ctx.running) {
        audio_manager_set_state(AUDIO_MGR_STATE_LISTENING);
    } else {
        audio_manager_set_state(AUDIO_MGR_STATE_IDLE);
    }
}

static void audio_manager_notify_event(const audio_mgr_event_t *event)
{
    if (!event || !s_ctx.config.event_callback) return;
    s_ctx.config.event_callback(event, s_ctx.config.user_ctx);
}

static bool audio_manager_post_event(const audio_mgr_internal_msg_t *msg)
{
    if (!s_ctx.event_queue || !msg) return false;
    if (xQueueSend(s_ctx.event_queue, msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, drop type=%d", msg->type);
        return false;
    }
    return true;
}

static void audio_manager_arm_vad_timer(int duration_ms)
{
    if (duration_ms <= 0) {
        audio_manager_clear_vad_timer();
        return;
    }
    s_ctx.vad_active = true;
    s_ctx.vad_deadline_tick = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
}

static void audio_manager_clear_vad_timer(void)
{
    s_ctx.vad_active = false;
    s_ctx.vad_deadline_tick = 0;
}

static void audio_manager_tick(void)
{
    if (!s_ctx.vad_active) return;
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_ctx.vad_deadline_tick) >= 0) {
        s_ctx.vad_active = false;
        audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_VAD_TIMEOUT };
        audio_manager_handle_internal_event(&msg);
    }
}

static void button_event_handler(button_event_type_t event, void *user_ctx)
{
    audio_mgr_internal_msg_t msg = {
        .type = (event == BUTTON_EVENT_PRESS) ? AUDIO_INT_EVT_BUTTON_PRESS
                                              : AUDIO_INT_EVT_BUTTON_RELEASE,
    };
    audio_manager_post_event(&msg);
}

static void afe_event_handler(const afe_event_t *event, void *user_ctx)
{
    if (!event) return;
    audio_mgr_internal_msg_t msg = {0};
    switch (event->type) {
        case AFE_EVENT_VAD_START:
            msg.type = AUDIO_INT_EVT_VAD_START;
            break;
        case AFE_EVENT_VAD_END:
            msg.type = AUDIO_INT_EVT_VAD_END;
            break;
        default:
            return;
    }
    audio_manager_post_event(&msg);
}

static void afe_record_handler(const int16_t *pcm_data, size_t samples, void *user_ctx)
{
    if (s_ctx.record_callback) {
        s_ctx.record_callback(pcm_data, samples, s_ctx.record_ctx);
    }
}


static void audio_manager_handle_internal_event(const audio_mgr_internal_msg_t *msg)
{
    if (!msg) return;
    audio_mgr_event_t evt = {0};

    switch (msg->type) {
    case AUDIO_INT_EVT_START_LISTEN:
        if (!s_ctx.running) ESP_LOGI(TAG, "🎧 启动音频监听");
        s_ctx.running = true;
        audio_manager_clear_vad_timer();
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_STOP_LISTEN:
        if (s_ctx.running) ESP_LOGI(TAG, "🛑 停止音频监听");
        s_ctx.running = false;
        s_ctx.recording = false;
        audio_manager_clear_vad_timer();
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_BUTTON_PRESS:
        ESP_LOGI(TAG, "🔘 按键按下");
        evt.type = AUDIO_MGR_EVENT_BUTTON_TRIGGER;
        audio_manager_notify_event(&evt);
        s_ctx.recording = true;
        audio_manager_arm_vad_timer(s_ctx.config.vad_config.vad_timeout_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_BUTTON_RELEASE:
        evt.type = AUDIO_MGR_EVENT_BUTTON_RELEASE;
        audio_manager_notify_event(&evt);
        break;

    case AUDIO_INT_EVT_VAD_START:
        evt.type = AUDIO_MGR_EVENT_VAD_START;
        audio_manager_notify_event(&evt);
        s_ctx.recording = true;
        audio_manager_arm_vad_timer(s_ctx.config.vad_config.vad_timeout_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_VAD_END:
        evt.type = AUDIO_MGR_EVENT_VAD_END;
        audio_manager_notify_event(&evt);
        s_ctx.recording = false;
        audio_manager_arm_vad_timer(s_ctx.config.vad_config.vad_end_delay_ms);
        audio_manager_refresh_state();
        break;

    case AUDIO_INT_EVT_VAD_TIMEOUT:
        evt.type = AUDIO_MGR_EVENT_VAD_TIMEOUT;
        audio_manager_notify_event(&evt);
        s_ctx.recording = false;
        audio_manager_clear_vad_timer();
        audio_manager_refresh_state();
        break;
    }
}

static void audio_manager_task(void *arg)
{
    audio_mgr_internal_msg_t msg = {0};
    while (true) {
        if (xQueueReceive(s_ctx.event_queue, &msg, pdMS_TO_TICKS(AUDIO_MANAGER_STEP_INTERVAL_MS)) == pdTRUE) {
            audio_manager_handle_internal_event(&msg);
        }
        audio_manager_tick();
    }
}


esp_err_t audio_manager_init(const audio_mgr_config_t *config)
{
    esp_err_t ret = ESP_OK;

    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "音频管理器已初始化");
        return ESP_OK;
    }
    if (!config) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "======== 初始化音频管理器 ========");
    memset(&s_ctx, 0, sizeof(s_ctx));
    memcpy(&s_ctx.config, config, sizeof(audio_mgr_config_t));
    s_ctx.volume = AUDIO_MANAGER_DEFAULT_VOLUME;
    s_ctx.state = AUDIO_MGR_STATE_DISABLED;

    audio_bsp_hw_config_t bsp_cfg = {
        .mic = s_ctx.config.hw_config.mic,
        .speaker = s_ctx.config.hw_config.speaker,
    };

    s_ctx.bsp = audio_bsp_create(&bsp_cfg);
    if (!s_ctx.bsp) {
        ESP_LOGE(TAG, "BSP 创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    playback_controller_config_t playback_cfg = {
        .bsp_handle = s_ctx.bsp,
        .playback_buffer_samples = AUDIO_MANAGER_PLAYBACK_BUFFER_BYTES / sizeof(int16_t),
        .reference_buffer_samples = AUDIO_MANAGER_REFERENCE_BUFFER_BYTES / sizeof(int16_t),
        .frame_samples = AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES,
        .reference_callback = NULL,
        .reference_ctx = NULL,
        .volume_ptr = &s_ctx.volume,
    };

    s_ctx.playback_ctrl = playback_controller_create(&playback_cfg);
    if (!s_ctx.playback_ctrl) {
        ESP_LOGE(TAG, "播放控制器创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_ctx.reference_rb = playback_controller_get_reference_buffer(s_ctx.playback_ctrl);

    s_ctx.event_queue = xQueueCreate(AUDIO_MANAGER_EVENT_QUEUE_LENGTH, sizeof(audio_mgr_internal_msg_t));
    if (!s_ctx.event_queue) {
        ESP_LOGE(TAG, "事件队列创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    if (xTaskCreatePinnedToCore(audio_manager_task, "audio_mgr", AUDIO_MANAGER_TASK_STACK_SIZE,
                                NULL, AUDIO_MANAGER_TASK_PRIORITY, &s_ctx.manager_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "状态机任务创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    afe_wrapper_config_t afe_cfg = {
        .bsp_handle = s_ctx.bsp,
        .reference_rb = s_ctx.reference_rb,
        .vad_config = (afe_vad_config_t){
            .enabled = s_ctx.config.vad_config.enabled,
            .vad_mode = s_ctx.config.vad_config.vad_mode,
            .min_speech_ms = s_ctx.config.vad_config.min_speech_ms,
            .min_silence_ms = s_ctx.config.vad_config.min_silence_ms,
        },
        .feature_config = (afe_feature_config_t){
            .aec_enabled = s_ctx.config.afe_config.aec_enabled,
            .ns_enabled = s_ctx.config.afe_config.ns_enabled,
            .agc_enabled = s_ctx.config.afe_config.agc_enabled,
            .afe_mode = s_ctx.config.afe_config.afe_mode,
        },
        .event_callback = afe_event_handler,
        .event_ctx = NULL,
        .record_callback = afe_record_handler,
        .record_ctx = NULL,
        .running_ptr = &s_ctx.running,
        .recording_ptr = &s_ctx.recording,
    };

    s_ctx.afe_wrapper = afe_wrapper_create(&afe_cfg);
    if (!s_ctx.afe_wrapper) {
        ESP_LOGE(TAG, "AFE 包装器创建失败");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    if (s_ctx.config.hw_config.button.gpio >= 0) {
        button_handler_config_t button_cfg = {
            .gpio = s_ctx.config.hw_config.button.gpio,
            .active_low = s_ctx.config.hw_config.button.active_low,
            .debounce_ms = 50,
            .callback = button_event_handler,
            .user_ctx = NULL,
        };
        s_ctx.button_handler = button_handler_create(&button_cfg);
        if (!s_ctx.button_handler) {
            ESP_LOGW(TAG, "按键处理器创建失败，继续运行");
        }
    } else {
        ESP_LOGI(TAG, "未配置按键 GPIO，跳过按键处理器");
        s_ctx.button_handler = NULL;
    }

    s_ctx.initialized = true;
    s_ctx.state = AUDIO_MGR_STATE_IDLE;
    audio_manager_refresh_state();
    ESP_LOGI(TAG, "✅ 音频管理器初始化完成");
    return ESP_OK;

fail:
    audio_manager_deinit();
    return ret;
}


void audio_manager_deinit(void)
{
    if (!s_ctx.initialized && !s_ctx.bsp) return;

    audio_manager_stop();
    audio_manager_stop_playback();

    if (s_ctx.manager_task) {
        vTaskDelete(s_ctx.manager_task);
        s_ctx.manager_task = NULL;
    }
    if (s_ctx.event_queue) {
        vQueueDelete(s_ctx.event_queue);
        s_ctx.event_queue = NULL;
    }
    if (s_ctx.button_handler) {
        button_handler_destroy(s_ctx.button_handler);
        s_ctx.button_handler = NULL;
    }
    if (s_ctx.afe_wrapper) {
        afe_wrapper_destroy(s_ctx.afe_wrapper);
        s_ctx.afe_wrapper = NULL;
    }
    if (s_ctx.playback_ctrl) {
        playback_controller_destroy(s_ctx.playback_ctrl);
        s_ctx.playback_ctrl = NULL;
    }
    if (s_ctx.bsp) {
        audio_bsp_destroy(s_ctx.bsp);
        s_ctx.bsp = NULL;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "音频管理器已销毁");
}

esp_err_t audio_manager_start(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_START_LISTEN };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

esp_err_t audio_manager_stop(void)
{
    if (!s_ctx.initialized) return ESP_OK;
    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_STOP_LISTEN };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

esp_err_t audio_manager_trigger_recording(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    audio_mgr_internal_msg_t msg = { .type = AUDIO_INT_EVT_BUTTON_PRESS };
    audio_manager_post_event(&msg);
    return ESP_OK;
}

esp_err_t audio_manager_start_recording(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "📼 开始录音");
    s_ctx.recording = true;
    audio_manager_refresh_state();
    return ESP_OK;
}

esp_err_t audio_manager_stop_recording(void)
{
    if (!s_ctx.recording) return ESP_OK;
    ESP_LOGI(TAG, "⏹️ 停止录音");
    s_ctx.recording = false;
    audio_manager_refresh_state();
    return ESP_OK;
}

esp_err_t audio_manager_play_audio(const int16_t *pcm_data, size_t sample_count)
{
    if (!s_ctx.initialized || !pcm_data || sample_count == 0) return ESP_ERR_INVALID_ARG;
    return playback_controller_write(s_ctx.playback_ctrl, pcm_data, sample_count);
}

size_t audio_manager_get_playback_free_space(void)
{
    if (!s_ctx.initialized || !s_ctx.playback_ctrl) return 0;
    return playback_controller_get_free_space(s_ctx.playback_ctrl);
}

esp_err_t audio_manager_start_playback(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = playback_controller_start(s_ctx.playback_ctrl);
    if (ret == ESP_OK) {
        s_ctx.playing = true;
        audio_manager_refresh_state();
    }
    return ret;
}

esp_err_t audio_manager_stop_playback(void)
{
    if (!s_ctx.initialized) return ESP_OK;
    esp_err_t ret = playback_controller_stop(s_ctx.playback_ctrl);
    if (ret == ESP_OK) {
        s_ctx.playing = false;
        audio_manager_refresh_state();
    }
    return ret;
}

esp_err_t audio_manager_clear_playback_buffer(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    return playback_controller_clear(s_ctx.playback_ctrl);
}

void audio_manager_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_ctx.volume = volume;
    ESP_LOGI(TAG, "🔊 音量: %d%%", volume);
}

uint8_t audio_manager_get_volume(void) { return s_ctx.volume; }
bool audio_manager_is_running(void) { return s_ctx.running; }
bool audio_manager_is_recording(void) { return s_ctx.recording; }
bool audio_manager_is_playing(void) { return playback_controller_is_running(s_ctx.playback_ctrl); }
audio_mgr_state_t audio_manager_get_state(void) { return s_ctx.state; }

void audio_manager_set_record_callback(audio_record_callback_t callback, void *user_ctx)
{
    s_ctx.record_callback = callback;
    s_ctx.record_ctx = user_ctx;
}
