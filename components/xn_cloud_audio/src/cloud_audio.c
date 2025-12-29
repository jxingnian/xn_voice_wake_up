/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-12-29 20:30:00
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-12-29 20:31:04
 * @FilePath: \xn_voice_wake_up\components\xn_cloud_audio\src\cloud_audio.c
 * @Description: 云端音频管理实现
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "cloud_audio.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CLOUD_AUDIO";

typedef struct {
    cloud_audio_config_t config;
    esp_websocket_client_handle_t ws_client;
    bool initialized;
    bool connected;
    SemaphoreHandle_t mutex;
    char ws_uri[128];
    char http_uri[128];
} cloud_audio_ctx_t;

static cloud_audio_ctx_t s_ctx = {0};

static void cloud_audio_notify_event(cloud_audio_event_type_t type, const void *data);
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void parse_ws_response(const char *data, int len);

static void cloud_audio_notify_event(cloud_audio_event_type_t type, const void *data)
{
    if (!s_ctx.config.event_cb) return;
    
    cloud_audio_event_t event = { .type = type };
    if (type == CLOUD_AUDIO_EVENT_WAKE_DETECTED && data) {
        memcpy(&event.data.wake, data, sizeof(cloud_audio_wake_result_t));
    } else if (type == CLOUD_AUDIO_EVENT_ERROR && data) {
        event.data.error_code = *(int *)data;
    }
    s_ctx.config.event_cb(&event, s_ctx.config.user_ctx);
}

static void parse_ws_response(const char *data, int len)
{
    cJSON *json = cJSON_ParseWithLength(data, len);
    if (!json) {
        ESP_LOGW(TAG, "JSON 解析失败");
        return;
    }

    cloud_audio_wake_result_t result = {0};
    
    cJSON *text = cJSON_GetObjectItem(json, "text");
    if (cJSON_IsString(text) && text->valuestring) {
        strncpy(result.text, text->valuestring, sizeof(result.text) - 1);
    }
    
    cJSON *wake = cJSON_GetObjectItem(json, "wake_detected");
    result.wake_detected = cJSON_IsTrue(wake);
    
    cJSON *verified = cJSON_GetObjectItem(json, "speaker_verified");
    result.speaker_verified = cJSON_IsTrue(verified);
    
    cJSON *score = cJSON_GetObjectItem(json, "speaker_score");
    if (cJSON_IsNumber(score)) {
        result.speaker_score = (float)score->valuedouble;
    }

    cJSON_Delete(json);

    if (result.wake_detected) {
        ESP_LOGI(TAG, "🎤 唤醒词检测: %s (声纹: %.2f)", result.text, result.speaker_score);
        cloud_audio_notify_event(CLOUD_AUDIO_EVENT_WAKE_DETECTED, &result);
        
        if (result.speaker_verified) {
            cloud_audio_notify_event(CLOUD_AUDIO_EVENT_VOICE_VERIFIED, &result);
        } else if (result.speaker_score > 0) {
            cloud_audio_notify_event(CLOUD_AUDIO_EVENT_VOICE_REJECTED, &result);
        }
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                     int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "✅ WebSocket 已连接");
        s_ctx.connected = true;
        cloud_audio_notify_event(CLOUD_AUDIO_EVENT_CONNECTED, NULL);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "❌ WebSocket 断开连接");
        s_ctx.connected = false;
        cloud_audio_notify_event(CLOUD_AUDIO_EVENT_DISCONNECTED, NULL);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01 && data->data_len > 0) {  // 文本帧
            parse_ws_response((const char *)data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket 错误");
        int err = -1;
        cloud_audio_notify_event(CLOUD_AUDIO_EVENT_ERROR, &err);
        break;

    default:
        break;
    }
}

esp_err_t cloud_audio_init(const cloud_audio_config_t *config)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "云端音频已初始化");
        return ESP_OK;
    }
    if (!config || !config->server_host || !config->user_id) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "======== 初始化云端音频 ========");
    memset(&s_ctx, 0, sizeof(s_ctx));
    memcpy(&s_ctx.config, config, sizeof(cloud_audio_config_t));

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    // 构建 URI
    snprintf(s_ctx.ws_uri, sizeof(s_ctx.ws_uri), "ws://%s:%d/ws/%s",
             config->server_host, config->server_port, config->user_id);
    snprintf(s_ctx.http_uri, sizeof(s_ctx.http_uri), "http://%s:%d",
             config->server_host, config->server_port);

    ESP_LOGI(TAG, "WebSocket URI: %s", s_ctx.ws_uri);

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "✅ 云端音频初始化完成");
    return ESP_OK;
}

void cloud_audio_deinit(void)
{
    if (!s_ctx.initialized) return;

    cloud_audio_disconnect();

    if (s_ctx.mutex) {
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "云端音频已销毁");
}

esp_err_t cloud_audio_connect(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (s_ctx.connected) return ESP_OK;

    ESP_LOGI(TAG, "🔗 连接云端服务器...");

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_ctx.ws_uri,
        .buffer_size = CLOUD_AUDIO_BUFFER_SIZE,
        .reconnect_timeout_ms = CLOUD_AUDIO_RECONNECT_DELAY_MS,
        .network_timeout_ms = 10000,
    };

    s_ctx.ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ctx.ws_client) {
        ESP_LOGE(TAG, "WebSocket 客户端创建失败");
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_register_events(s_ctx.ws_client, WEBSOCKET_EVENT_ANY,
                                   websocket_event_handler, NULL);

    esp_err_t ret = esp_websocket_client_start(s_ctx.ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket 启动失败: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(s_ctx.ws_client);
        s_ctx.ws_client = NULL;
        return ret;
    }

    return ESP_OK;
}

esp_err_t cloud_audio_disconnect(void)
{
    if (!s_ctx.ws_client) return ESP_OK;

    ESP_LOGI(TAG, "断开云端连接");
    esp_websocket_client_stop(s_ctx.ws_client);
    esp_websocket_client_destroy(s_ctx.ws_client);
    s_ctx.ws_client = NULL;
    s_ctx.connected = false;

    return ESP_OK;
}

esp_err_t cloud_audio_send(const int16_t *pcm_data, size_t sample_count)
{
    if (!s_ctx.initialized || !s_ctx.connected || !s_ctx.ws_client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pcm_data || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes = sample_count * sizeof(int16_t);
    int ret = esp_websocket_client_send_bin(s_ctx.ws_client, (const char *)pcm_data, bytes, pdMS_TO_TICKS(1000));
    
    if (ret < 0) {
        ESP_LOGW(TAG, "音频发送失败");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t cloud_audio_set_wake_word(const char *wake_word)
{
    if (!s_ctx.initialized || !wake_word) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "设置唤醒词: %s", wake_word);

    char url[256];
    snprintf(url, sizeof(url), "%s/set_wake_word", s_ctx.http_uri);

    char post_data[256];
    snprintf(post_data, sizeof(post_data), "user_id=%s&wake_word=%s", 
             s_ctx.config.user_id, wake_word);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) return ESP_ERR_NO_MEM;

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "✅ 唤醒词设置成功");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "唤醒词设置失败: %s, status=%d", esp_err_to_name(ret), status);
    return ESP_FAIL;
}


esp_err_t cloud_audio_register_voice(const int16_t *pcm_data, size_t sample_count)
{
    if (!s_ctx.initialized || !pcm_data || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "注册声纹...");

    char url[256];
    snprintf(url, sizeof(url), "%s/register_voice", s_ctx.http_uri);

    // 构建 multipart/form-data
    size_t audio_bytes = sample_count * sizeof(int16_t);
    
    // 边界字符串
    const char *boundary = "----ESP32Boundary";
    
    // 计算总大小
    char header_part[512];
    int header_len = snprintf(header_part, sizeof(header_part),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"user_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"voice.pcm\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n",
        boundary, s_ctx.config.user_id, boundary);

    char footer_part[64];
    int footer_len = snprintf(footer_part, sizeof(footer_part), "\r\n--%s--\r\n", boundary);

    size_t total_len = header_len + audio_bytes + footer_len;

    // 分配缓冲区
    char *body = malloc(total_len);
    if (!body) {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_ERR_NO_MEM;
    }

    // 组装数据
    memcpy(body, header_part, header_len);
    memcpy(body + header_len, pcm_data, audio_bytes);
    memcpy(body + header_len + audio_bytes, footer_part, footer_len);

    // 发送请求
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char content_type[64];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_post_field(client, body, total_len);

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (ret == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "✅ 声纹注册成功");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "声纹注册失败: %s, status=%d", esp_err_to_name(ret), status);
    return ESP_FAIL;
}

bool cloud_audio_is_connected(void)
{
    return s_ctx.connected;
}
