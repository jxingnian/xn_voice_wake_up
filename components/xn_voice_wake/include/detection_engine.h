/**
 * @file detection_engine.h
 * @brief 检测引擎接口
 */

#ifndef __DETECTION_ENGINE_H__
#define __DETECTION_ENGINE_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 检测引擎配置
 */
typedef struct {
    float threshold;            ///< 检测阈值
    int window_size;            ///< 滑动窗口大小
    int cooldown_ms;            ///< 冷却时间
} detection_engine_config_t;

/**
 * @brief 检测结果回调
 * @param model_index 模型索引
 * @param confidence 置信度
 */
typedef void (*detection_result_cb_t)(int model_index, float confidence);

/**
 * @brief 默认检测配置
 */
#define DETECTION_ENGINE_DEFAULT_CONFIG() { \
    .threshold = 0.6f, \
    .window_size = 5, \
    .cooldown_ms = 1000, \
}

/**
 * @brief 初始化检测引擎
 * @param config 配置参数
 * @return ESP_OK 成功
 */
esp_err_t detection_engine_init(const detection_engine_config_t *config);

/**
 * @brief 处理音频数据
 * @param audio_data 音频数据 (int16_t)
 * @param samples 采样点数
 * @return ESP_OK 成功
 */
esp_err_t detection_engine_process(const int16_t *audio_data, size_t samples);

/**
 * @brief 设置结果回调
 * @param callback 回调函数
 * @return ESP_OK 成功
 */
esp_err_t detection_engine_set_callback(detection_result_cb_t callback);

/**
 * @brief 反初始化
 * @return ESP_OK 成功
 */
esp_err_t detection_engine_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __DETECTION_ENGINE_H__ */
