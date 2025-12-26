/**
 * @file audio_processor.h
 * @brief 音频处理模块接口
 */

#ifndef __AUDIO_PROCESSOR_H__
#define __AUDIO_PROCESSOR_H__

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 音频处理器配置
 */
typedef struct {
    int bck_pin;            ///< I2S BCK 引脚
    int ws_pin;             ///< I2S WS 引脚
    int data_pin;           ///< I2S DATA 引脚
    int sample_rate;        ///< 采样率 (默认 16000)
    int bits_per_sample;    ///< 位深度 (默认 16)
} audio_processor_config_t;

/**
 * @brief 音频数据回调
 * @param data 音频数据
 * @param samples 采样点数
 */
typedef void (*audio_data_cb_t)(const int16_t *data, size_t samples);

/**
 * @brief 默认音频配置
 */
#define AUDIO_PROCESSOR_DEFAULT_CONFIG() { \
    .bck_pin = 41, \
    .ws_pin = 42, \
    .data_pin = 2, \
    .sample_rate = 16000, \
    .bits_per_sample = 16, \
}

/**
 * @brief 初始化音频处理器
 * @param config 配置参数
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_init(const audio_processor_config_t *config);

/**
 * @brief 开始采集
 * @param callback 音频数据回调
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_start(audio_data_cb_t callback);

/**
 * @brief 暂停采集
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_pause(void);

/**
 * @brief 恢复采集
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_resume(void);

/**
 * @brief 停止采集
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_stop(void);

/**
 * @brief 反初始化
 * @return ESP_OK 成功
 */
esp_err_t audio_processor_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_PROCESSOR_H__ */
