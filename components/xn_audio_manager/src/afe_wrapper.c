/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-29 21:00:00
 * @FilePath: \xn_voice_wake_up\components\xn_audio_manager\src\afe_wrapper.c
 * @Description: AFE 管理模块实现 - 仅提供 VAD 和音频处理功能
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "afe_wrapper.h"
#include "esp_log.h"
#include "esp_gmf_afe_manager.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "AFE_WRAPPER";

/**
 * @brief AFE 包装器上下文结构体
 */
typedef struct afe_wrapper_s {
    esp_gmf_afe_manager_handle_t afe_manager;  ///< AFE Manager 句柄
    esp_afe_sr_iface_t *afe_handle;            ///< AFE 接口句柄
    
    audio_bsp_handle_t bsp_handle;              ///< BSP 句柄
    ring_buffer_handle_t reference_rb;          ///< 回采数据环形缓冲区
    
    afe_event_callback_t event_callback;        ///< 事件回调函数
    void *event_ctx;                            ///< 事件回调上下文
    afe_record_callback_t record_callback;      ///< 录音数据回调函数
    void *record_ctx;                           ///< 录音回调上下文
    
    bool *running_ptr;                          ///< 指向运行状态标志的指针
    bool *recording_ptr;                        ///< 指向录音状态标志的指针
    
    // 静态缓冲区
    int16_t mic_buffer[512];                    ///< 麦克风数据缓冲区
    int16_t ref_buffer[512];                    ///< 回采数据缓冲区
} afe_wrapper_t;

/**
 * @brief AFE 读取回调函数
 */
static int32_t afe_read_callback(void *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    afe_wrapper_t *wrapper = (afe_wrapper_t *)user_ctx;
    if (!buffer || buf_sz == 0 || !wrapper) return 0;

    int16_t *out_buf = (int16_t *)buffer;
    const size_t total_samples = buf_sz / sizeof(int16_t);
    const size_t channels = 2;  // MR: 麦克风+回采
    const size_t frame_samples = total_samples / channels;

    if (frame_samples > 512) {
        ESP_LOGE(TAG, "AFE 读取帧过大: %d", (int)frame_samples);
        memset(out_buf, 0, buf_sz);
        return buf_sz;
    }

    size_t mic_got = 0;

    if (wrapper->running_ptr && *wrapper->running_ptr) {
        esp_err_t ret = audio_bsp_read_mic(wrapper->bsp_handle, wrapper->mic_buffer, 
                                         frame_samples, &mic_got);

        if (ret != ESP_OK || mic_got == 0) {
            memset(out_buf, 0, buf_sz);
            return buf_sz;
        }

        // 调试：每秒打印一次麦克风数据统计
        static int debug_cnt = 0;
        if (++debug_cnt >= 31) {
            debug_cnt = 0;
            int16_t max_val = 0, min_val = 0;
            for (size_t i = 0; i < mic_got; i++) {
                if (wrapper->mic_buffer[i] > max_val) max_val = wrapper->mic_buffer[i];
                if (wrapper->mic_buffer[i] < min_val) min_val = wrapper->mic_buffer[i];
            }
            ESP_LOGI(TAG, "MIC 数据: samples=%d, min=%d, max=%d", (int)mic_got, min_val, max_val);
        }

        size_t ref_got = ring_buffer_read(wrapper->reference_rb, wrapper->ref_buffer, mic_got, 0);
        if (ref_got < mic_got) {
            memset(wrapper->ref_buffer + ref_got, 0, (mic_got - ref_got) * sizeof(int16_t));
        }

        // 交织数据: MR 格式
        for (size_t i = 0; i < mic_got; i++) {
            out_buf[i * 2 + 0] = wrapper->mic_buffer[i];
            out_buf[i * 2 + 1] = wrapper->ref_buffer[i];
        }
    } else {
        memset(out_buf, 0, buf_sz);
    }

    return buf_sz;
}

/**
 * @brief AFE 结果回调函数 - 仅处理 VAD 事件
 */
static void afe_result_callback(afe_fetch_result_t *result, void *user_ctx)
{
    afe_wrapper_t *wrapper = (afe_wrapper_t *)user_ctx;
    if (!result || !wrapper || !wrapper->event_callback) return;

    afe_event_t event = {0};
    static bool vad_active = false;

    // VAD 状态处理
    if (result->vad_state == VAD_SPEECH && !vad_active) {
        vad_active = true;
        event.type = AFE_EVENT_VAD_START;
        wrapper->event_callback(&event, wrapper->event_ctx);
    } else if (result->vad_state == VAD_SILENCE && vad_active) {
        vad_active = false;
        event.type = AFE_EVENT_VAD_END;
        wrapper->event_callback(&event, wrapper->event_ctx);
    }

    // 处理录音数据回调
    if (wrapper->recording_ptr && *wrapper->recording_ptr && 
        result->data && result->data_size > 0 && wrapper->record_callback) {
        size_t samples = result->data_size / sizeof(int16_t);
        wrapper->record_callback((const int16_t *)result->data, samples, wrapper->record_ctx);
    }
}

/**
 * @brief 创建 AFE 包装器
 */
afe_wrapper_handle_t afe_wrapper_create(const afe_wrapper_config_t *config)
{
    if (!config || !config->bsp_handle || !config->reference_rb || !config->event_callback) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }

    afe_wrapper_t *wrapper = (afe_wrapper_t *)calloc(1, sizeof(afe_wrapper_t));
    if (!wrapper) {
        ESP_LOGE(TAG, "AFE 包装器分配失败");
        return NULL;
    }

    wrapper->bsp_handle = config->bsp_handle;
    wrapper->reference_rb = config->reference_rb;
    wrapper->event_callback = config->event_callback;
    wrapper->event_ctx = config->event_ctx;
    wrapper->record_callback = config->record_callback;
    wrapper->record_ctx = config->record_ctx;
    wrapper->running_ptr = config->running_ptr;
    wrapper->recording_ptr = config->recording_ptr;

    // 配置 AFE（无唤醒词模型）
    ESP_LOGI(TAG, "配置 AFE Manager（仅 VAD）...");
    afe_config_t *afe_config = afe_config_init("MR", NULL, AFE_TYPE_SR, 
                                                config->feature_config.afe_mode);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE 配置失败");
        free(wrapper);
        return NULL;
    }

    // 配置音频处理功能
    afe_config->aec_init = config->feature_config.aec_enabled;
    afe_config->se_init = false;
    afe_config->vad_init = config->vad_config.enabled;
    afe_config->vad_mode = config->vad_config.vad_mode;
    afe_config->vad_min_speech_ms = config->vad_config.min_speech_ms;
    afe_config->vad_min_noise_ms = config->vad_config.min_silence_ms;
    afe_config->wakenet_init = false;  // 禁用唤醒词
    afe_config->afe_perferred_core = 0;
    afe_config->afe_perferred_priority = 8;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config->agc_init = config->feature_config.agc_enabled;
    afe_config->ns_init = config->feature_config.ns_enabled;
    afe_config->afe_ringbuf_size = 120;

    afe_config = afe_config_check(afe_config);
    wrapper->afe_handle = esp_afe_handle_from_config(afe_config);

    // 创建 AFE Manager
    esp_gmf_afe_manager_cfg_t mgr_cfg = {
        .afe_cfg = afe_config,
        .read_cb = afe_read_callback,
        .read_ctx = wrapper,
        .feed_task_setting = {
            .stack_size = 10 * 1024,
            .prio = 8,
            .core = 1,
        },
        .fetch_task_setting = {
            .stack_size = 8 * 1024,
            .prio = 8,
            .core = 0,
        },
    };

    esp_err_t ret = esp_gmf_afe_manager_create(&mgr_cfg, &wrapper->afe_manager);
    afe_config_free(afe_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AFE Manager 创建失败");
        free(wrapper);
        return NULL;
    }

    esp_gmf_afe_manager_set_result_cb(wrapper->afe_manager, afe_result_callback, wrapper);

    ESP_LOGI(TAG, "✅ AFE 包装器创建成功（仅 VAD）");
    return wrapper;
}

/**
 * @brief 销毁 AFE 包装器
 */
void afe_wrapper_destroy(afe_wrapper_handle_t wrapper)
{
    if (!wrapper) return;

    if (wrapper->afe_manager) {
        esp_gmf_afe_manager_destroy(wrapper->afe_manager);
    }

    free(wrapper);
    ESP_LOGI(TAG, "AFE 包装器已销毁");
}
