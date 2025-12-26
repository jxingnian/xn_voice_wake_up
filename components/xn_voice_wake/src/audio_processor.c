/**
 * @file audio_processor.c
 * @brief 音频处理模块实现
 */

#include "audio_processor.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "audio_proc";

// I2S 配置
#define I2S_PORT            1           // 使用 I2S 端口 1
#define DMA_BUF_COUNT       4
#define DMA_BUF_LEN         512
#define BIT_SHIFT           14          // 32bit 转 16bit 的右移位数

// 模块状态
static struct {
    i2s_chan_handle_t rx_handle;
    audio_data_cb_t callback;
    TaskHandle_t task_handle;
    bool running;
    bool initialized;
    audio_processor_config_t config;
} s_audio = {0};

// 音频采集任务
static void audio_capture_task(void *arg)
{
    // 32 位原始缓冲区
    int32_t *raw_buffer = heap_caps_malloc(DMA_BUF_LEN * sizeof(int32_t), MALLOC_CAP_DMA);
    // 16 位输出缓冲区
    int16_t *out_buffer = heap_caps_malloc(DMA_BUF_LEN * sizeof(int16_t), MALLOC_CAP_DMA);
    
    if (!raw_buffer || !out_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        if (raw_buffer) free(raw_buffer);
        if (out_buffer) free(out_buffer);
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read;
    
    while (s_audio.running) {
        esp_err_t ret = i2s_channel_read(s_audio.rx_handle, raw_buffer, 
                                          DMA_BUF_LEN * sizeof(int32_t), 
                                          &bytes_read, portMAX_DELAY);
        
        if (ret == ESP_OK && s_audio.callback && s_audio.running) {
            size_t samples = bytes_read / sizeof(int32_t);
            
            // 32 位转 16 位
            for (size_t i = 0; i < samples; i++) {
                out_buffer[i] = (int16_t)(raw_buffer[i] >> BIT_SHIFT);
            }
            
            s_audio.callback(out_buffer, samples);
        }
    }

    free(raw_buffer);
    free(out_buffer);
    vTaskDelete(NULL);
}

esp_err_t audio_processor_init(const audio_processor_config_t *config)
{
    if (s_audio.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_audio.config, config, sizeof(audio_processor_config_t));

    // I2S 通道配置
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_audio.rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // I2S 标准模式配置 (32 位采样)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = config->bck_pin,
            .ws = config->ws_pin,
            .dout = I2S_GPIO_UNUSED,
            .din = config->data_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_audio.rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S std mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_audio.rx_handle);
        return ret;
    }

    s_audio.initialized = true;
    ESP_LOGI(TAG, "Audio processor initialized (BCK:%d, WS:%d, DATA:%d)", 
             config->bck_pin, config->ws_pin, config->data_pin);

    return ESP_OK;
}

esp_err_t audio_processor_start(audio_data_cb_t callback)
{
    if (!s_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio.running) {
        return ESP_OK;
    }

    s_audio.callback = callback;
    s_audio.running = true;

    // 启用 I2S 通道
    esp_err_t ret = i2s_channel_enable(s_audio.rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        s_audio.running = false;
        return ret;
    }

    // 创建采集任务
    BaseType_t xret = xTaskCreatePinnedToCore(
        audio_capture_task,
        "audio_cap",
        4096,
        NULL,
        5,
        &s_audio.task_handle,
        1  // 运行在 Core 1
    );

    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task");
        i2s_channel_disable(s_audio.rx_handle);
        s_audio.running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio capture started");
    return ESP_OK;
}

esp_err_t audio_processor_pause(void)
{
    if (!s_audio.initialized || !s_audio.running) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2s_channel_disable(s_audio.rx_handle);
}

esp_err_t audio_processor_resume(void)
{
    if (!s_audio.initialized || !s_audio.running) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2s_channel_enable(s_audio.rx_handle);
}

esp_err_t audio_processor_stop(void)
{
    if (!s_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_audio.running = false;

    // 等待任务结束
    if (s_audio.task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_audio.task_handle = NULL;
    }

    i2s_channel_disable(s_audio.rx_handle);
    
    ESP_LOGI(TAG, "Audio capture stopped");
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    if (!s_audio.initialized) {
        return ESP_OK;
    }

    audio_processor_stop();

    if (s_audio.rx_handle) {
        i2s_del_channel(s_audio.rx_handle);
        s_audio.rx_handle = NULL;
    }

    s_audio.initialized = false;
    ESP_LOGI(TAG, "Audio processor deinitialized");

    return ESP_OK;
}
