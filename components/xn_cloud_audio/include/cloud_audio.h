/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-12-29 20:30:00
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-29 20:25:37
 * @FilePath: \xn_voice_wake_up\components\xn_cloud_audio\include\cloud_audio.h
 * @Description: 云端音频管理 - WebSocket 连接、VAD 音频上传、唤醒词检测
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ 配置宏 ============

#define CLOUD_AUDIO_TASK_STACK_SIZE     (8 * 1024)
#define CLOUD_AUDIO_TASK_PRIORITY       5
#define CLOUD_AUDIO_BUFFER_SIZE         (32 * 1024)     ///< 音频缓冲区大小 (2秒 16kHz 16bit)
#define CLOUD_AUDIO_RECONNECT_DELAY_MS  3000            ///< 重连延迟

// ============ 事件定义 ============

/** 云端音频事件类型 */
typedef enum {
    CLOUD_AUDIO_EVENT_CONNECTED,        ///< WebSocket 连接成功
    CLOUD_AUDIO_EVENT_DISCONNECTED,     ///< WebSocket 断开连接
    CLOUD_AUDIO_EVENT_WAKE_DETECTED,    ///< 检测到唤醒词
    CLOUD_AUDIO_EVENT_VOICE_VERIFIED,   ///< 声纹验证通过
    CLOUD_AUDIO_EVENT_VOICE_REJECTED,   ///< 声纹验证失败
    CLOUD_AUDIO_EVENT_ERROR,            ///< 错误
} cloud_audio_event_type_t;

/** 唤醒检测结果 */
typedef struct {
    char text[256];                     ///< 识别文本
    bool wake_detected;                 ///< 是否检测到唤醒词
    bool speaker_verified;              ///< 声纹是否验证通过
    float speaker_score;                ///< 声纹相似度分数
} cloud_audio_wake_result_t;

/** 云端音频事件数据 */
typedef struct {
    cloud_audio_event_type_t type;      ///< 事件类型
    union {
        cloud_audio_wake_result_t wake; ///< 唤醒检测结果
        int error_code;                 ///< 错误码
    } data;
} cloud_audio_event_t;

/** 事件回调函数类型 */
typedef void (*cloud_audio_event_cb_t)(const cloud_audio_event_t *event, void *user_ctx);

// ============ 配置结构 ============

/** 云端音频配置 */
typedef struct {
    const char *server_host;            ///< 服务器地址
    uint16_t server_port;               ///< 服务器端口
    const char *user_id;                ///< 用户 ID
    cloud_audio_event_cb_t event_cb;    ///< 事件回调
    void *user_ctx;                     ///< 用户上下文
} cloud_audio_config_t;

#define CLOUD_AUDIO_DEFAULT_CONFIG()                    \
    (cloud_audio_config_t){                             \
        .server_host = "117.50.176.26",                 \
        .server_port = 8000,                            \
        .user_id = "default",                           \
        .event_cb = NULL,                               \
        .user_ctx = NULL,                               \
    }

// ============ API 接口 ============

/**
 * @brief 初始化云端音频管理器
 */
esp_err_t cloud_audio_init(const cloud_audio_config_t *config);

/**
 * @brief 反初始化云端音频管理器
 */
void cloud_audio_deinit(void);

/**
 * @brief 连接到云端服务器
 */
esp_err_t cloud_audio_connect(void);

/**
 * @brief 断开云端连接
 */
esp_err_t cloud_audio_disconnect(void);

/**
 * @brief 发送音频数据到云端
 * @param pcm_data PCM 音频数据 (16bit, 16kHz)
 * @param sample_count 采样点数
 */
esp_err_t cloud_audio_send(const int16_t *pcm_data, size_t sample_count);

/**
 * @brief 设置唤醒词
 * @param wake_word 唤醒词文本
 */
esp_err_t cloud_audio_set_wake_word(const char *wake_word);

/**
 * @brief 注册声纹
 * @param pcm_data PCM 音频数据
 * @param sample_count 采样点数
 */
esp_err_t cloud_audio_register_voice(const int16_t *pcm_data, size_t sample_count);

/**
 * @brief 检查是否已连接
 */
bool cloud_audio_is_connected(void);

#ifdef __cplusplus
}
#endif
