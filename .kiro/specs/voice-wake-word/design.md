# 设计文档

## 概述

本文档描述了基于 microWakeWord 的语音唤醒组件（xn_voice_wake）的详细设计。该组件遵循现有项目架构风格，提供模块化、可配置的语音唤醒功能。

## 架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        应用层 (main.c)                          │
│                                                                 │
│  voice_wake_config_t cfg = VOICE_WAKE_DEFAULT_CONFIG();        │
│  voice_wake_init(&cfg);                                         │
│  voice_wake_start();                                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    xn_voice_wake 组件                           │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ voice_wake  │  │   model     │  │    audio_processor      │ │
│  │  _module    │  │  _manager   │  │                         │ │
│  │  (主模块)   │  │ (模型管理)  │  │  ┌─────────────────┐   │ │
│  └─────────────┘  └─────────────┘  │  │ i2s_capture     │   │ │
│         │               │          │  └─────────────────┘   │ │
│         │               │          │  ┌─────────────────┐   │ │
│         │               │          │  │ feature_extract │   │ │
│         │               │          │  └─────────────────┘   │ │
│         │               │          └─────────────────────────┘ │
│         │               │                    │                  │
│         ▼               ▼                    ▼                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              detection_engine (检测引擎)                 │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │   │
│  │  │ TFLite      │  │ sliding     │  │ callback        │  │   │
│  │  │ interpreter │  │ window      │  │ dispatcher      │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘  │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      ESP-IDF 底层                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ I2S Driver  │  │ PSRAM       │  │ FreeRTOS Task           │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### 目录结构

```
components/xn_voice_wake/
├── CMakeLists.txt
├── include/
│   ├── voice_wake_module.h      # 主模块对外接口
│   ├── model_manager.h          # 模型管理接口
│   ├── audio_processor.h        # 音频处理接口
│   └── detection_engine.h       # 检测引擎接口
├── src/
│   ├── voice_wake_module.c      # 主模块实现
│   ├── model_manager.c          # 模型管理实现
│   ├── audio_processor.c        # 音频处理实现
│   ├── feature_extractor.c      # 特征提取实现
│   └── detection_engine.c       # 检测引擎实现
└── models/
    └── (预置模型文件)
```


## 组件与接口

### 1. 主模块接口 (voice_wake_module.h)

```c
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
 */
typedef void (*voice_wake_detect_cb_t)(int model_index, float confidence);

/**
 * @brief 状态变化回调
 * @param state 新状态
 */
typedef void (*voice_wake_state_cb_t)(voice_wake_state_t state);

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
    
    // 模型配置
    const uint8_t *model_data;      ///< 嵌入式模型数据指针
    size_t model_size;              ///< 模型数据大小
    const char *model_path;         ///< 文件系统模型路径 (与 model_data 二选一)
    
    // 回调函数
    voice_wake_detect_cb_t detect_cb;   ///< 检测回调
    voice_wake_state_cb_t state_cb;     ///< 状态回调
    
    // 任务配置
    int task_priority;              ///< 检测任务优先级
    int task_stack_size;            ///< 检测任务栈大小
} voice_wake_config_t;

/**
 * @brief 默认配置宏
 */
#define VOICE_WAKE_DEFAULT_CONFIG() \
    (voice_wake_config_t) { \
        .i2s_bck_pin = 41, \
        .i2s_ws_pin = 42, \
        .i2s_data_pin = 2, \
        .detect_threshold = 0.5f, \
        .sliding_window_size = 5, \
        .cooldown_ms = 1000, \
        .model_data = NULL, \
        .model_size = 0, \
        .model_path = NULL, \
        .detect_cb = NULL, \
        .state_cb = NULL, \
        .task_priority = tskIDLE_PRIORITY + 3, \
        .task_stack_size = 4096, \
    }

/**
 * @brief 初始化语音唤醒模块
 */
esp_err_t voice_wake_init(const voice_wake_config_t *config);

/**
 * @brief 开始监听
 */
esp_err_t voice_wake_start(void);

/**
 * @brief 停止监听
 */
esp_err_t voice_wake_stop(void);

/**
 * @brief 获取当前状态
 */
voice_wake_state_t voice_wake_get_state(void);

/**
 * @brief 反初始化模块
 */
esp_err_t voice_wake_deinit(void);

/**
 * @brief 获取最后错误码
 */
esp_err_t voice_wake_get_last_error(void);
```


### 2. 模型管理接口 (model_manager.h)

```c
/**
 * @brief 模型信息结构
 */
typedef struct {
    char name[32];          ///< 模型名称
    size_t size;            ///< 模型大小 (字节)
    int version;            ///< 模型版本
} model_info_t;

/**
 * @brief 模型句柄
 */
typedef void* model_handle_t;

/**
 * @brief 从内存加载模型
 */
esp_err_t model_manager_load_from_memory(const uint8_t *data, size_t size, 
                                          model_handle_t *handle);

/**
 * @brief 从文件系统加载模型
 */
esp_err_t model_manager_load_from_file(const char *path, model_handle_t *handle);

/**
 * @brief 卸载模型
 */
esp_err_t model_manager_unload(model_handle_t handle);

/**
 * @brief 获取模型信息
 */
esp_err_t model_manager_get_info(model_handle_t handle, model_info_t *info);

/**
 * @brief 获取 TFLite 解释器
 */
void* model_manager_get_interpreter(model_handle_t handle);
```

### 3. 音频处理接口 (audio_processor.h)

```c
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
 */
typedef void (*audio_data_cb_t)(const int16_t *data, size_t samples);

/**
 * @brief 初始化音频处理器
 */
esp_err_t audio_processor_init(const audio_processor_config_t *config);

/**
 * @brief 开始采集
 */
esp_err_t audio_processor_start(audio_data_cb_t callback);

/**
 * @brief 暂停采集
 */
esp_err_t audio_processor_pause(void);

/**
 * @brief 恢复采集
 */
esp_err_t audio_processor_resume(void);

/**
 * @brief 停止采集
 */
esp_err_t audio_processor_stop(void);

/**
 * @brief 反初始化
 */
esp_err_t audio_processor_deinit(void);
```

### 4. 检测引擎接口 (detection_engine.h)

```c
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
 */
typedef void (*detection_result_cb_t)(int model_index, float confidence);

/**
 * @brief 初始化检测引擎
 */
esp_err_t detection_engine_init(const detection_engine_config_t *config);

/**
 * @brief 添加模型
 */
esp_err_t detection_engine_add_model(model_handle_t model);

/**
 * @brief 移除模型
 */
esp_err_t detection_engine_remove_model(model_handle_t model);

/**
 * @brief 处理特征数据
 */
esp_err_t detection_engine_process(const float *features, size_t feature_dim);

/**
 * @brief 设置结果回调
 */
esp_err_t detection_engine_set_callback(detection_result_cb_t callback);

/**
 * @brief 反初始化
 */
esp_err_t detection_engine_deinit(void);
```


## 数据模型

### 音频数据流

```
┌─────────────────────────────────────────────────────────────────┐
│                        数据流程                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  I2S 麦克风                                                     │
│      │                                                          │
│      ▼ (16kHz, 16bit, mono)                                    │
│  ┌─────────────────┐                                           │
│  │ DMA 缓冲区      │  512 samples/buffer                       │
│  │ (双缓冲)        │                                           │
│  └─────────────────┘                                           │
│      │                                                          │
│      ▼ (每 32ms 一帧)                                          │
│  ┌─────────────────┐                                           │
│  │ 特征提取器      │  30ms 窗口, 10ms 步进                     │
│  │                 │  输出: 40 维频谱特征                       │
│  └─────────────────┘                                           │
│      │                                                          │
│      ▼ (每 30ms)                                               │
│  ┌─────────────────┐                                           │
│  │ TFLite 推理     │  输入: 40 维特征                          │
│  │                 │  输出: 概率值 (0.0-1.0)                   │
│  └─────────────────┘                                           │
│      │                                                          │
│      ▼                                                          │
│  ┌─────────────────┐                                           │
│  │ 滑动窗口        │  窗口大小: 5 帧                           │
│  │                 │  计算平均概率                              │
│  └─────────────────┘                                           │
│      │                                                          │
│      ▼ (平均概率 > 阈值)                                       │
│  ┌─────────────────┐                                           │
│  │ 回调触发        │  传递模型索引和置信度                     │
│  └─────────────────┘                                           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 内存布局

```
┌─────────────────────────────────────────────────────────────────┐
│                        PSRAM 分配                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ TFLite Arena (每模型)                    ~100-150KB     │   │
│  │  - 模型权重                                              │   │
│  │  - 中间张量                                              │   │
│  │  - 输入/输出缓冲区                                       │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 音频缓冲区                               ~16KB          │   │
│  │  - DMA 双缓冲 (2 x 4KB)                                 │   │
│  │  - 特征提取缓冲区 (8KB)                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 滑动窗口缓冲区                           ~1KB           │   │
│  │  - 概率历史 (每模型 5 x float)                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  总计: 单模型约 120-170KB PSRAM                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 状态机

```
┌─────────────────────────────────────────────────────────────────┐
│                        状态转换图                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│                    ┌──────────────┐                            │
│                    │    IDLE      │                            │
│                    │   (空闲)     │                            │
│                    └──────────────┘                            │
│                           │                                     │
│                           │ voice_wake_start()                 │
│                           ▼                                     │
│                    ┌──────────────┐                            │
│         ┌─────────│  LISTENING   │◄────────┐                   │
│         │         │  (监听中)    │         │                   │
│         │         └──────────────┘         │                   │
│         │                │                 │                   │
│         │                │ 检测到唤醒词    │ 冷却时间结束      │
│         │                ▼                 │                   │
│         │         ┌──────────────┐         │                   │
│         │         │  DETECTED    │─────────┘                   │
│         │         │  (已检测)    │                             │
│         │         └──────────────┘                             │
│         │                                                       │
│         │ 发生错误                                              │
│         ▼                                                       │
│  ┌──────────────┐                                              │
│  │    ERROR     │                                              │
│  │   (错误)     │                                              │
│  └──────────────┘                                              │
│         │                                                       │
│         │ voice_wake_deinit() + voice_wake_init()              │
│         ▼                                                       │
│  ┌──────────────┐                                              │
│  │    IDLE      │                                              │
│  └──────────────┘                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```


## 正确性属性

正确性属性是系统在所有有效执行中都应保持为真的特征或行为。属性作为人类可读规格和机器可验证正确性保证之间的桥梁。

### 属性 1：初始化幂等性

**对于任意**有效配置，多次调用 `voice_wake_init()` 应当返回相同结果（ESP_OK），且不会重复分配资源。

**验证：需求 1.4**

### 属性 2：模型加载卸载往返

**对于任意**有效的 TFLite 模型数据，加载模型后再卸载，应当完全释放所有分配的内存，且可以重新加载相同模型。

**验证：需求 2.3, 2.5, 7.3**

### 属性 3：无效模型拒绝

**对于任意**无效的模型数据（非 TFLite 格式、损坏的数据、空数据），模型管理器应当返回 ESP_ERR_INVALID_ARG 且不分配任何资源。

**验证：需求 2.4**

### 属性 4：特征维度不变量

**对于任意**输入音频数据，特征提取器输出的特征维度应当始终为 40 维。

**验证：需求 4.1**

### 属性 5：滑动窗口检测逻辑

**对于任意**推理概率序列，当且仅当滑动窗口内的平均概率超过阈值时，检测引擎应当触发唤醒回调。

**验证：需求 5.2, 5.5**

### 属性 6：冷却时间约束

**对于任意**检测事件，在冷却时间内不应触发新的检测回调，即使推理概率持续超过阈值。

**验证：需求 5.6**

### 属性 7：状态一致性

**对于任意**状态转换序列，`voice_wake_get_state()` 返回的状态应当与最后一次状态回调传递的状态一致。

**验证：需求 6.2, 6.4**

### 属性 8：资源释放完整性

**对于任意**初始化后的模块，调用 `voice_wake_deinit()` 应当释放所有分配的资源（TFLite 解释器、音频缓冲区、I2S 驱动），且可以重新初始化。

**验证：需求 7.3, 7.4**

### 属性 9：配置验证

**对于任意**无效配置（无效引脚号、无效阈值范围、同时指定 model_data 和 model_path 为空），初始化应当返回 ESP_ERR_INVALID_ARG。

**验证：需求 9.6**

### 属性 10：错误恢复

**对于任意**错误状态，通过 `voice_wake_deinit()` 后重新 `voice_wake_init()` 应当能够恢复到正常工作状态。

**验证：需求 8.5**

## 错误处理

### 错误码定义

| 错误码 | 说明 |
|--------|------|
| ESP_OK | 操作成功 |
| ESP_ERR_INVALID_ARG | 无效参数 |
| ESP_ERR_INVALID_STATE | 无效状态（如未初始化就调用 start） |
| ESP_ERR_NO_MEM | 内存不足（PSRAM 不足） |
| ESP_ERR_NOT_FOUND | 资源未找到（如 I2S 初始化失败） |
| ESP_FAIL | 通用错误 |

### 错误处理策略

1. **音频缓冲区溢出**：记录警告日志，丢弃溢出数据，继续运行
2. **TFLite 推理失败**：转换到 ERROR 状态，调用状态回调
3. **模型加载失败**：返回错误码，不改变当前状态
4. **I2S 错误**：尝试重新初始化，失败则转换到 ERROR 状态

## 测试策略

### 单元测试

1. **配置验证测试**：测试各种有效和无效配置的处理
2. **模型管理测试**：测试模型加载、卸载、信息查询
3. **滑动窗口测试**：测试滑动窗口计算逻辑
4. **状态机测试**：测试状态转换逻辑

### 属性测试

使用属性测试框架验证上述正确性属性：

1. **初始化幂等性测试**：生成随机有效配置，多次初始化验证结果一致
2. **模型往返测试**：加载-卸载-重新加载，验证内存正确释放
3. **无效输入测试**：生成各种无效数据，验证正确拒绝
4. **滑动窗口测试**：生成随机概率序列，验证检测逻辑正确

### 集成测试

1. **端到端测试**：使用预录音频文件测试完整流程
2. **多模型测试**：测试同时加载多个模型的场景
3. **长时间运行测试**：验证内存泄漏和稳定性
