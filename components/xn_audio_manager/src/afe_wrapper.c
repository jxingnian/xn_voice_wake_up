/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-27 19:17:04
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-28 20:00:00
 * @FilePath: \xn_esp32_audio\components\xn_audio_manager\src\afe_wrapper.c
 * @Description: AFE 管理模块实现 - 支持 WakeNet 和 MultiNet 命令词唤醒
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "afe_wrapper.h"
#include "esp_log.h"
#include "esp_gmf_afe_manager.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "model_path.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "AFE_WRAPPER";

/**
 * @brief AFE 包装器上下文结构体
 * 
 * 封装了 AFE Manager 和语音识别相关的所有状态和资源
 */
typedef struct afe_wrapper_s {
    esp_gmf_afe_manager_handle_t afe_manager;  ///< AFE Manager 句柄
    esp_afe_sr_iface_t *afe_handle;            ///< AFE 接口句柄
    srmodel_list_t *models;                     ///< 语音识别模型列表
    
    // MultiNet 命令词识别
    esp_mn_iface_t *multinet;                  ///< MultiNet 接口
    model_iface_data_t *mn_model_data;         ///< MultiNet 模型数据
    bool use_multinet;                          ///< 是否使用 MultiNet 作为唤醒
    
    audio_bsp_handle_t bsp_handle;              ///< BSP 句柄，用于读取麦克风数据
    ring_buffer_handle_t reference_rb;         ///< 回采数据环形缓冲区
    
    afe_wakeup_config_t wakeup_config;         ///< 唤醒词配置
    afe_event_callback_t event_callback;       ///< 事件回调函数
    void *event_ctx;                            ///< 事件回调上下文
    afe_record_callback_t record_callback;      ///< 录音数据回调函数
    void *record_ctx;                           ///< 录音回调上下文
    
    bool *running_ptr;                          ///< 指向运行状态标志的指针
    bool *recording_ptr;                        ///< 指向录音状态标志的指针
    
    // 静态缓冲区（避免频繁 malloc）
    int16_t mic_buffer[512];                    ///< 麦克风数据缓冲区
    int16_t ref_buffer[512];                    ///< 回采数据缓冲区
} afe_wrapper_t;

/**
 * @brief AFE 读取回调函数
 * 
 * 从 I2S HAL 读取麦克风数据，从环形缓冲区读取回采数据，
 * 并将两者交织成 MR（麦克风+回采）格式供 AFE 处理
 * 
 * @param buffer 输出缓冲区，用于存放交织后的音频数据
 * @param buf_sz 缓冲区大小（字节）
 * @param user_ctx 用户上下文，指向 afe_wrapper_t 结构体
 * @param ticks 超时时间（未使用）
 * @return int32_t 实际读取的字节数
 */
static int32_t afe_read_callback(void *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    afe_wrapper_t *wrapper = (afe_wrapper_t *)user_ctx;
    if (!buffer || buf_sz == 0 || !wrapper) return 0;

    int16_t *out_buf = (int16_t *)buffer;
    const size_t total_samples = buf_sz / sizeof(int16_t);
    const size_t channels = 2;  // MR: 麦克风+回采
    const size_t frame_samples = total_samples / channels;

    // 检查帧大小是否超出缓冲区限制
    if (frame_samples > 512) {
        ESP_LOGE(TAG, "AFE 读取帧过大: %d", (int)frame_samples);
        memset(out_buf, 0, buf_sz);
        return buf_sz;
    }

    size_t mic_got = 0;

    // 仅在运行状态下读取数据
    if (wrapper->running_ptr && *wrapper->running_ptr) {
        // 读取麦克风数据
        esp_err_t ret = audio_bsp_read_mic(wrapper->bsp_handle, wrapper->mic_buffer, 
                                         frame_samples, &mic_got);

        if (ret != ESP_OK || mic_got == 0) {
            memset(out_buf, 0, buf_sz);
            return buf_sz;
        }

        // 读取回采数据（用于回声消除）
        size_t ref_got = ring_buffer_read(wrapper->reference_rb, wrapper->ref_buffer, mic_got, 0);

        // 如果回采数据不足，用静音填充
        if (ref_got < mic_got) {
            memset(wrapper->ref_buffer + ref_got, 0, (mic_got - ref_got) * sizeof(int16_t));
        }

        // 交织数据: MR 格式（M=麦克风，R=回采）
        for (size_t i = 0; i < mic_got; i++) {
            out_buf[i * 2 + 0] = wrapper->mic_buffer[i];  // M: 麦克风
            out_buf[i * 2 + 1] = wrapper->ref_buffer[i];  // R: 回采
        }
    } else {
        // 未运行时填充静音数据
        memset(out_buf, 0, buf_sz);
    }

    return buf_sz;
}

/**
 * @brief AFE 结果回调函数
 * 
 * 处理 AFE 的处理结果，包括唤醒词检测、VAD 状态变化和录音数据
 * 支持 WakeNet 和 MultiNet 两种唤醒方式
 * 
 * @param result AFE 处理结果
 * @param user_ctx 用户上下文，指向 afe_wrapper_t 结构体
 */
static void afe_result_callback(afe_fetch_result_t *result, void *user_ctx)
{
    afe_wrapper_t *wrapper = (afe_wrapper_t *)user_ctx;
    if (!result || !wrapper || !wrapper->event_callback) return;

    afe_event_t event = {0};

    // 使用 MultiNet 命令词识别作为唤醒
    if (wrapper->use_multinet && wrapper->multinet && wrapper->mn_model_data) {
        // 将 AFE 处理后的音频送入 MultiNet 进行命令词识别
        if (result->data && result->data_size > 0) {
            esp_mn_state_t mn_state = wrapper->multinet->detect(wrapper->mn_model_data, (int16_t *)result->data);
            
            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = wrapper->multinet->get_results(wrapper->mn_model_data);
                if (mn_result && mn_result->num > 0) {
                    // 命令词识别成功，触发唤醒事件
                    event.type = AFE_EVENT_WAKEUP_DETECTED;
                    event.data.wakeup.wake_word_index = mn_result->command_id[0];
                    event.data.wakeup.volume_db = result->data_volume;

                    ESP_LOGI(TAG, "🎤 MultiNet 命令词检测: ID=%d, 词=%s, 置信度=%.2f",
                             mn_result->command_id[0],
                             mn_result->string,
                             mn_result->prob[0]);

                    wrapper->event_callback(&event, wrapper->event_ctx);
                }
            }
        }
    } else {
        // 使用 WakeNet 唤醒词检测
        if (result->wakeup_state == WAKENET_DETECTED) {
            event.type = AFE_EVENT_WAKEUP_DETECTED;
            event.data.wakeup.wake_word_index = result->wake_word_index;
            event.data.wakeup.volume_db = result->data_volume;

            ESP_LOGI(TAG, "🎤 WakeNet 唤醒词检测: 索引=%d, 音量=%.1f dB",
                     result->wake_word_index, result->data_volume);

            wrapper->event_callback(&event, wrapper->event_ctx);
        }
    }

    // 处理 VAD（语音活动检测）状态变化
    static bool vad_active = false;

    if (result->vad_state == VAD_SPEECH && !vad_active) {
        // 检测到语音开始
        vad_active = true;
        event.type = AFE_EVENT_VAD_START;
        wrapper->event_callback(&event, wrapper->event_ctx);
    } else if (result->vad_state == VAD_SILENCE && vad_active) {
        // 检测到语音结束
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
 * 
 * 初始化 AFE Manager，加载唤醒词模型，配置各种音频处理功能
 * 
 * @param config AFE 包装器配置
 * @return afe_wrapper_handle_t AFE 包装器句柄，失败返回 NULL
 */
afe_wrapper_handle_t afe_wrapper_create(const afe_wrapper_config_t *config)
{
    if (!config || !config->bsp_handle || !config->reference_rb || !config->event_callback) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }

    // 分配包装器上下文内存
    afe_wrapper_t *wrapper = (afe_wrapper_t *)calloc(1, sizeof(afe_wrapper_t));
    if (!wrapper) {
        ESP_LOGE(TAG, "AFE 包装器分配失败");
        return NULL;
    }

    // 保存配置参数
    wrapper->bsp_handle = config->bsp_handle;
    wrapper->reference_rb = config->reference_rb;
    wrapper->wakeup_config = config->wakeup_config;
    wrapper->event_callback = config->event_callback;
    wrapper->event_ctx = config->event_ctx;
    wrapper->record_callback = config->record_callback;
    wrapper->record_ctx = config->record_ctx;
    wrapper->running_ptr = config->running_ptr;
    wrapper->recording_ptr = config->recording_ptr;

    // 加载唤醒词模型
    if (config->wakeup_config.enabled) {
        ESP_LOGI(TAG, "加载唤醒词模型: %s", config->wakeup_config.wake_word_name);
        wrapper->models = esp_srmodel_init(config->wakeup_config.model_partition);
        if (!wrapper->models) {
            ESP_LOGE(TAG, "模型加载失败");
            free(wrapper);
            return NULL;
        }
        ESP_LOGI(TAG, "✅ 加载了 %d 个模型", wrapper->models->num);
    }

    // 配置 AFE
    ESP_LOGI(TAG, "配置 AFE Manager...");
    afe_config_t *afe_config = afe_config_init("MR", wrapper->models, AFE_TYPE_SR, 
                                                config->feature_config.afe_mode);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE 配置失败");
        if (wrapper->models) esp_srmodel_deinit(wrapper->models);
        free(wrapper);
        return NULL;
    }

    // 配置音频处理功能
    afe_config->aec_init = config->feature_config.aec_enabled;      // 回声消除
    afe_config->se_init = false;                                    // 语音增强（未启用）
    afe_config->vad_init = config->vad_config.enabled;              // 语音活动检测
    afe_config->vad_mode = config->vad_config.vad_mode;             // VAD 模式
    afe_config->vad_min_speech_ms = config->vad_config.min_speech_ms;   // 最小语音时长
    afe_config->vad_min_noise_ms = config->vad_config.min_silence_ms;   // 最小静音时长
    
    // 如果使用 MultiNet，禁用 WakeNet
    wrapper->use_multinet = config->wakeup_config.use_multinet;
    if (wrapper->use_multinet) {
        afe_config->wakenet_init = false;  // 禁用 WakeNet
        ESP_LOGI(TAG, "使用 MultiNet 命令词识别作为唤醒");
    } else {
        afe_config->wakenet_init = config->wakeup_config.enabled;  // 使用 WakeNet
        ESP_LOGI(TAG, "使用 WakeNet 唤醒词检测");
    }
    afe_config->wakenet_mode = config->wakeup_config.sensitivity;   // 唤醒词灵敏度
    afe_config->afe_perferred_core = 0;                             // 优先运行在核心 0
    afe_config->afe_perferred_priority = 8;                         // 任务优先级
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;    // 优先使用 PSRAM
    afe_config->agc_init = config->feature_config.agc_enabled;     // 自动增益控制
    afe_config->ns_init = config->feature_config.ns_enabled;       // 噪声抑制
    afe_config->afe_ringbuf_size = 120;                             // 环形缓冲区大小（加大以提供更多缓冲空间）

    // 验证配置并创建 AFE 句柄
    afe_config = afe_config_check(afe_config);
    wrapper->afe_handle = esp_afe_handle_from_config(afe_config);

    // 创建 AFE Manager
    esp_gmf_afe_manager_cfg_t mgr_cfg = {
        .afe_cfg = afe_config,
        .read_cb = afe_read_callback,              // 数据读取回调
        .read_ctx = wrapper,                       // 读取回调上下文
        .feed_task_setting = {
            .stack_size = 10 * 1024,               // Feed 任务栈大小（缩减以降低内部RAM占用）
            .prio = 8,                             // Feed 任务优先级
            .core = 1,                             // Feed 任务运行核心（保持在 CPU1）
        },
        .fetch_task_setting = {
            .stack_size = 10 * 1024,                // Fetch 任务栈大小（缩减占用）
            .prio = 8,                             // Fetch 任务优先级（与Feed相同，时间片轮转）
            .core = 0,                             // Fetch 任务运行在 CPU0，与 Feed 分核
        },
    };

    esp_err_t ret = esp_gmf_afe_manager_create(&mgr_cfg, &wrapper->afe_manager);
    afe_config_free(afe_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AFE Manager 创建失败");
        if (wrapper->models) esp_srmodel_deinit(wrapper->models);
        free(wrapper);
        return NULL;
    }

    // 设置结果回调
    esp_gmf_afe_manager_set_result_cb(wrapper->afe_manager, afe_result_callback, wrapper);

    // 初始化 MultiNet 命令词识别
    if (wrapper->use_multinet && config->wakeup_config.wake_word_name) {
        ESP_LOGI(TAG, "初始化 MultiNet 命令词识别...");
        
        // 获取 MultiNet 模型
        char *mn_name = esp_srmodel_filter(wrapper->models, ESP_MN_PREFIX, NULL);
        if (!mn_name) {
            ESP_LOGE(TAG, "未找到 MultiNet 模型");
            esp_gmf_afe_manager_destroy(wrapper->afe_manager);
            esp_srmodel_deinit(wrapper->models);
            free(wrapper);
            return NULL;
        }
        ESP_LOGI(TAG, "使用 MultiNet 模型: %s", mn_name);
        
        // 创建 MultiNet 实例
        wrapper->multinet = esp_mn_handle_from_name(mn_name);
        if (!wrapper->multinet) {
            ESP_LOGE(TAG, "MultiNet 接口获取失败");
            esp_gmf_afe_manager_destroy(wrapper->afe_manager);
            esp_srmodel_deinit(wrapper->models);
            free(wrapper);
            return NULL;
        }
        
        // 创建 MultiNet 模型数据
        wrapper->mn_model_data = wrapper->multinet->create(mn_name, 5760);
        if (!wrapper->mn_model_data) {
            ESP_LOGE(TAG, "MultiNet 模型数据创建失败");
            esp_gmf_afe_manager_destroy(wrapper->afe_manager);
            esp_srmodel_deinit(wrapper->models);
            free(wrapper);
            return NULL;
        }
        
        // 清空默认命令词并添加自定义命令词
        // wake_word_name 格式: "ni hao xing nian"（拼音用空格分隔）
        esp_mn_commands_clear();
        esp_mn_commands_add(1, (char *)config->wakeup_config.wake_word_name);
        ESP_LOGI(TAG, "添加命令词: ID=1, 拼音=%s", config->wakeup_config.wake_word_name);
        
        // 更新命令词到 MultiNet 模型
        esp_mn_commands_update(wrapper->multinet, wrapper->mn_model_data);
        
        ESP_LOGI(TAG, "✅ MultiNet 命令词识别初始化成功");
    }

    ESP_LOGI(TAG, "✅ AFE 包装器创建成功");
    return wrapper;
}

/**
 * @brief 销毁 AFE 包装器
 * 
 * 释放 AFE Manager 和模型资源
 * 
 * @param wrapper AFE 包装器句柄
 */
void afe_wrapper_destroy(afe_wrapper_handle_t wrapper)
{
    if (!wrapper) return;

    // 销毁 MultiNet
    if (wrapper->multinet && wrapper->mn_model_data) {
        wrapper->multinet->destroy(wrapper->mn_model_data);
        wrapper->mn_model_data = NULL;
        wrapper->multinet = NULL;
        ESP_LOGI(TAG, "MultiNet 已销毁");
    }

    // 销毁 AFE Manager
    if (wrapper->afe_manager) {
        esp_gmf_afe_manager_destroy(wrapper->afe_manager);
    }

    // 释放模型资源
    if (wrapper->models) {
        esp_srmodel_deinit(wrapper->models);
    }

    // 释放包装器内存
    free(wrapper);
    ESP_LOGI(TAG, "AFE 包装器已销毁");
}

/**
 * @brief 更新唤醒词配置
 * 
 * @param wrapper AFE 包装器句柄
 * @param config 新的唤醒词配置
 * @return esp_err_t ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t afe_wrapper_update_wakeup_config(afe_wrapper_handle_t wrapper, 
                                            const afe_wakeup_config_t *config)
{
    if (!wrapper || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&wrapper->wakeup_config, config, sizeof(afe_wakeup_config_t));
    ESP_LOGI(TAG, "唤醒词配置已更新: %s", config->wake_word_name);

    return ESP_OK;
}

/**
 * @brief 获取唤醒词配置
 * 
 * @param wrapper AFE 包装器句柄
 * @param config 用于返回配置的缓冲区
 * @return esp_err_t ESP_OK 成功，ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t afe_wrapper_get_wakeup_config(afe_wrapper_handle_t wrapper, 
                                         afe_wakeup_config_t *config)
{
    if (!wrapper || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &wrapper->wakeup_config, sizeof(afe_wakeup_config_t));
    return ESP_OK;
}

