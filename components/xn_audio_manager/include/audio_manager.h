/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-29 20:18:59
 * @FilePath: \xn_voice_wake_up\components\xn_audio_manager\include\audio_manager.h
 * @Description: 音频管理器 - 硬件配置、VAD、录音、播放功能
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include "audio_bsp.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ 调度与缓冲配置宏 ============

#define AUDIO_MANAGER_TASK_STACK_SIZE        (6 * 1024)
#define AUDIO_MANAGER_TASK_PRIORITY          7
#define AUDIO_MANAGER_EVENT_QUEUE_LENGTH     16
#define AUDIO_MANAGER_STEP_INTERVAL_MS       100
#define AUDIO_MANAGER_DEFAULT_VOLUME         80

#define AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES 1024
#define AUDIO_MANAGER_PLAYBACK_BUFFER_BYTES  (512 * 1024)
#define AUDIO_MANAGER_REFERENCE_BUFFER_BYTES (16 * 1024)

// ============ 状态机定义 ============

typedef enum {
    AUDIO_MGR_STATE_DISABLED = 0,
    AUDIO_MGR_STATE_IDLE,
    AUDIO_MGR_STATE_LISTENING,
    AUDIO_MGR_STATE_RECORDING,
    AUDIO_MGR_STATE_PLAYBACK,
} audio_mgr_state_t;

typedef void (*audio_mgr_state_cb_t)(audio_mgr_state_t state, void *user_ctx);

// ============ 事件定义 ============

/** 音频管理器事件类型 */
typedef enum {
    AUDIO_MGR_EVENT_VAD_START,          ///< 人声开始
    AUDIO_MGR_EVENT_VAD_END,            ///< 人声结束
    AUDIO_MGR_EVENT_VAD_TIMEOUT,        ///< VAD 超时（无人说话）
    AUDIO_MGR_EVENT_BUTTON_TRIGGER,     ///< 按键手动触发（按下）
    AUDIO_MGR_EVENT_BUTTON_RELEASE,     ///< 按键松开
} audio_mgr_event_type_t;

/** 音频管理器事件数据 */
typedef struct {
    audio_mgr_event_type_t type;        ///< 事件类型
} audio_mgr_event_t;

/** 事件回调函数类型 */
typedef void (*audio_mgr_event_cb_t)(const audio_mgr_event_t *event, void *user_ctx);

// ============ 配置结构 ============

/** 硬件配置 */
typedef struct {
    audio_bsp_mic_config_t     mic;     ///< 麦克风 I2S 配置
    audio_bsp_speaker_config_t speaker; ///< 扬声器 I2S 配置
    struct {
        int  gpio;                      ///< 按键 GPIO
        bool active_low;                ///< 低电平有效
    } button;
} audio_mgr_hw_config_t;

/** VAD 配置 */
typedef struct {
    bool enabled;                   ///< 是否启用 VAD
    int vad_mode;                   ///< VAD 模式 (0-3)
    int min_speech_ms;              ///< 最小语音持续时间
    int min_silence_ms;             ///< 最小静音持续时间
    int vad_timeout_ms;             ///< VAD 超时时间
    int vad_end_delay_ms;           ///< 说话结束后延迟
} audio_mgr_vad_config_t;

/** AFE 功能配置 */
typedef struct {
    bool aec_enabled;               ///< 回声消除
    bool ns_enabled;                ///< 降噪
    bool agc_enabled;               ///< 自动增益
    int afe_mode;                   ///< AFE 模式（0=LOW_COST, 1=HIGH_QUALITY）
} audio_mgr_afe_config_t;

/** 音频管理器配置 */
typedef struct {
    audio_mgr_hw_config_t      hw_config;       ///< 硬件配置
    audio_mgr_vad_config_t     vad_config;      ///< VAD 配置
    audio_mgr_afe_config_t     afe_config;      ///< AFE 配置
    audio_mgr_event_cb_t       event_callback;  ///< 事件回调
    audio_mgr_state_cb_t       state_callback;  ///< 状态机回调
    void                      *user_ctx;        ///< 用户上下文
} audio_mgr_config_t;

#define AUDIO_MANAGER_DEFAULT_HW_CONFIG()                            \
    (audio_mgr_hw_config_t){                                         \
        .mic = {                                                     \
            .port = 0, .bclk_gpio = -1, .lrck_gpio = -1, .din_gpio = -1, \
            .sample_rate = 16000, .bits = 32,                        \
            .max_frame_samples = 512, .bit_shift = 14,               \
        },                                                           \
        .speaker = {                                                 \
            .port = 0, .bclk_gpio = -1, .lrck_gpio = -1, .dout_gpio = -1, \
            .sample_rate = 16000, .bits = 16,                        \
            .max_frame_samples = AUDIO_MANAGER_PLAYBACK_FRAME_SAMPLES,\
        },                                                           \
        .button = { .gpio = -1, .active_low = true },                \
    }

#define AUDIO_MANAGER_DEFAULT_VAD_CONFIG()                           \
    (audio_mgr_vad_config_t){                                        \
        .enabled = true,                                             \
        .vad_mode = 2,                                               \
        .min_speech_ms = 200,                                        \
        .min_silence_ms = 400,                                       \
        .vad_timeout_ms = 8000,                                      \
        .vad_end_delay_ms = 1200,                                    \
    }

#define AUDIO_MANAGER_DEFAULT_AFE_CONFIG()                           \
    (audio_mgr_afe_config_t){                                        \
        .aec_enabled = true,                                         \
        .ns_enabled = true,                                          \
        .agc_enabled = true,                                         \
        .afe_mode = 1,                                               \
    }

#define AUDIO_MANAGER_DEFAULT_CONFIG()                               \
    (audio_mgr_config_t){                                            \
        .hw_config = AUDIO_MANAGER_DEFAULT_HW_CONFIG(),              \
        .vad_config = AUDIO_MANAGER_DEFAULT_VAD_CONFIG(),            \
        .afe_config = AUDIO_MANAGER_DEFAULT_AFE_CONFIG(),            \
        .event_callback = NULL,                                      \
        .state_callback = NULL,                                      \
        .user_ctx = NULL,                                            \
    }

// ============ API 接口 ============

/**
 * @brief 初始化音频管理器
 */
esp_err_t audio_manager_init(const audio_mgr_config_t *config);

/**
 * @brief 反初始化音频管理器
 */
void audio_manager_deinit(void);

/**
 * @brief 启动音频管理器（开始监听）
 */
esp_err_t audio_manager_start(void);

/**
 * @brief 停止音频管理器
 */
esp_err_t audio_manager_stop(void);

/**
 * @brief 手动触发录音（按键触发）
 */
esp_err_t audio_manager_trigger_recording(void);

/**
 * @brief 开始录音
 */
esp_err_t audio_manager_start_recording(void);

/**
 * @brief 停止录音
 */
esp_err_t audio_manager_stop_recording(void);

/**
 * @brief 播放音频数据
 */
esp_err_t audio_manager_play_audio(const int16_t *pcm_data, size_t sample_count);

/**
 * @brief 获取播放缓冲区可用空间
 */
size_t audio_manager_get_playback_free_space(void);

/**
 * @brief 开始播放
 */
esp_err_t audio_manager_start_playback(void);

/**
 * @brief 停止播放
 */
esp_err_t audio_manager_stop_playback(void);

/**
 * @brief 清空播放缓冲区
 */
esp_err_t audio_manager_clear_playback_buffer(void);

/**
 * @brief 设置音量
 */
void audio_manager_set_volume(uint8_t volume);

/**
 * @brief 获取当前音量
 */
uint8_t audio_manager_get_volume(void);

/**
 * @brief 检查是否正在运行
 */
bool audio_manager_is_running(void);

/**
 * @brief 检查是否正在录音
 */
bool audio_manager_is_recording(void);

/**
 * @brief 检查是否正在播放
 */
bool audio_manager_is_playing(void);

/**
 * @brief 获取状态机当前状态
 */
audio_mgr_state_t audio_manager_get_state(void);

// ============ 录音数据回调 ============

/**
 * @brief 录音数据回调函数类型
 */
typedef void (*audio_record_callback_t)(const int16_t *pcm_data, size_t sample_count, void *user_ctx);

/**
 * @brief 注册录音数据回调
 */
void audio_manager_set_record_callback(audio_record_callback_t callback, void *user_ctx);

#ifdef __cplusplus
}
#endif
