/**
 * @file voice_wake_module.h
 * @brief 语音唤醒模块接口
 * 
 * 基于 Edge Impulse 的语音唤醒组件，支持自定义唤醒词检测。
 */

#ifndef __VOICE_WAKE_MODULE_H__
#define __VOICE_WAKE_MODULE_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 语音唤醒模块状态
 */
typedef enum {
    VOICE_WAKE_STATE_IDLE = 0,      ///< 空闲状态
    VOICE_WAKE_STATE_LISTENING,     ///< 监听中
    VOICE_WAKE_STATE_DETECTED,      ///< 检测到唤醒词
    VOICE_WAKE_STATE_ERROR,         ///< 错误状态
} voice_wake_state_t;

/**
 * @brief 唤醒词检测回调
 * @param model_index 检测到的模型索引
 * @param confidence 置信度 (0.0-1.0)
 * @param user_data 用户数据
 */
typedef void (*voice_wake_detect_cb_t)(int model_index, float confidence, void *user_data);

/**
 * @brief 状态变化回调
 * @param state 新状态
 * @param user_data 用户数据
 */
typedef void (*voice_wake_state_cb_t)(voice_wake_state_t state, void *user_data);

/**
 * @brief 语音唤醒模块配置
 */
typedef struct {
    // I2S 配置
    int i2s_bck_pin;                ///< I2S BCK 引脚
    int i2s_ws_pin;                 ///< I2S WS 引脚
    int i2s_data_pin;               ///< I2S DATA 引脚
    
    // 检测参数
    float detect_threshold;         ///< 检测阈值 (0.0-1.0)
    int sliding_window_size;        ///< 滑动窗口大小 (帧数)
    int cooldown_ms;                ///< 检测冷却时间 (ms)
    
    // 回调函数
    voice_wake_detect_cb_t detect_cb;   ///< 检测回调
    voice_wake_state_cb_t state_cb;     ///< 状态回调
    void *user_data;                    ///< 用户数据
    
    // 任务配置
    int task_priority;              ///< 检测任务优先级
    int task_stack_size;            ///< 检测任务栈大小
} voice_wake_config_t;

/**
 * @brief 默认配置宏
 */
#define VOICE_WAKE_DEFAULT_CONFIG() { \
    .i2s_bck_pin = 15, \
    .i2s_ws_pin = 2, \
    .i2s_data_pin = 39, \
    .detect_threshold = 0.6f, \
    .sliding_window_size = 5, \
    .cooldown_ms = 1000, \
    .detect_cb = NULL, \
    .state_cb = NULL, \
    .user_data = NULL, \
    .task_priority = 5, \
    .task_stack_size = 8192, \
}

/**
 * @brief 初始化语音唤醒模块
 * @param config 配置参数，NULL 使用默认配置
 * @return ESP_OK 成功，其他错误码表示失败
 */
esp_err_t voice_wake_init(const voice_wake_config_t *config);

/**
 * @brief 开始监听
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_start(void);

/**
 * @brief 停止监听
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_stop(void);

/**
 * @brief 获取当前状态
 * @return 当前状态
 */
voice_wake_state_t voice_wake_get_state(void);

/**
 * @brief 反初始化模块
 * @return ESP_OK 成功
 */
esp_err_t voice_wake_deinit(void);

/**
 * @brief 获取最后错误码
 * @return 最后的错误码
 */
esp_err_t voice_wake_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_WAKE_MODULE_H__ */
