/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-29 20:00:00
 * @FilePath: \xn_voice_wake_up\components\xn_audio_manager\include\afe_wrapper.h
 * @Description: AFE 管理模块 - 封装 AFE Manager，仅提供 VAD 和音频处理功能
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "audio_bsp.h"
#include "ring_buffer.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** AFE 事件类型 */
typedef enum {
    AFE_EVENT_VAD_START,        ///< 人声开始
    AFE_EVENT_VAD_END,          ///< 人声结束
} afe_event_type_t;

/** AFE 事件数据 */
typedef struct {
    afe_event_type_t type;
} afe_event_t;

/** AFE 事件回调 */
typedef void (*afe_event_callback_t)(const afe_event_t *event, void *user_ctx);

/** 录音数据回调 */
typedef void (*afe_record_callback_t)(const int16_t *pcm_data, size_t samples, void *user_ctx);

/** AFE VAD 配置 */
typedef struct {
    bool enabled;
    int vad_mode;
    int min_speech_ms;
    int min_silence_ms;
} afe_vad_config_t;

/** AFE 功能配置 */
typedef struct {
    bool aec_enabled;
    bool ns_enabled;
    bool agc_enabled;
    int afe_mode;
} afe_feature_config_t;

/** AFE 包装器配置 */
typedef struct {
    audio_bsp_handle_t bsp_handle;             ///< BSP 句柄
    ring_buffer_handle_t reference_rb;          ///< 回采缓冲区
    afe_vad_config_t vad_config;                ///< VAD 配置
    afe_feature_config_t feature_config;        ///< 功能配置
    afe_event_callback_t event_callback;        ///< 事件回调
    void *event_ctx;                            ///< 事件回调上下文
    afe_record_callback_t record_callback;      ///< 录音回调
    void *record_ctx;                           ///< 录音回调上下文
    bool *running_ptr;                          ///< 运行状态指针（外部管理）
    bool *recording_ptr;                        ///< 录音状态指针（外部管理）
} afe_wrapper_config_t;

/** AFE 包装器句柄 */
typedef struct afe_wrapper_s *afe_wrapper_handle_t;

/**
 * @brief 创建 AFE 包装器
 * @param config 配置参数
 * @return AFE 包装器句柄，失败返回 NULL
 */
afe_wrapper_handle_t afe_wrapper_create(const afe_wrapper_config_t *config);

/**
 * @brief 销毁 AFE 包装器
 * @param wrapper AFE 包装器句柄
 */
void afe_wrapper_destroy(afe_wrapper_handle_t wrapper);

#ifdef __cplusplus
}
#endif

