# Picovoice Porcupine 唤醒词方案

本文档介绍如何使用 Picovoice Porcupine 平台训练和部署自定义唤醒词。

## 方案概述

| 特性 | 说明 |
|------|------|
| 平台 | Picovoice Console |
| 支持芯片 | ESP32、ESP32-S3、树莓派等 |
| 自定义唤醒词 | ✅ 支持，在线训练 |
| 中文支持 | ⚠️ 有限支持（英文效果更好） |
| 授权 | 个人/非商业免费，商用收费 |
| 准确率 | 高（专业方案） |

---

## 步骤 1：注册账号

1. 打开 https://console.picovoice.ai/
2. 点击 "Sign up" 注册（可用 GitHub/Google 登录）
3. 注册后会获得一个 **Access Key**（很重要，后面要用）

---

## 步骤 2：创建自定义唤醒词

### 方法一：使用 Picovoice Console（推荐）

1. 登录 https://console.picovoice.ai/
2. 点击左侧菜单 "Porcupine"
3. 点击 "Train Wake Word"
4. 输入唤醒词：
   - **Wake Word**: 输入你的唤醒词，如 `ni hao xing nian`（拼音）
   - **Language**: 选择 `Chinese (Mandarin)`
5. 点击 "Train" 开始训练
6. 训练完成后下载模型文件（`.ppn` 格式）

### 方法二：使用预置唤醒词

Porcupine 提供一些预置唤醒词（免费）：
- "Porcupine"
- "Bumblebee"
- "Alexa"
- "Hey Google"
- "Ok Google"
- "Hey Siri"

---

## 步骤 3：获取 ESP32 SDK

### 下载 SDK

```bash
git clone https://github.com/Picovoice/porcupine.git
cd porcupine/demo/mcu/esp32
```

### SDK 目录结构

```
porcupine/
├── include/
│   └── pv_porcupine.h          # API 头文件
├── lib/
│   └── esp32/
│       └── libpv_porcupine.a   # ESP32 静态库
├── resources/
│   └── keyword_files/
│       └── esp32/              # 预置唤醒词模型
└── demo/
    └── mcu/
        └── esp32/              # ESP32 示例项目
```

---

## 步骤 4：集成到项目

### 4.1 复制必要文件

```bash
# 在你的项目中创建组件目录
mkdir -p components/xn_porcupine_wake

# 复制头文件
cp porcupine/include/pv_porcupine.h components/xn_porcupine_wake/include/

# 复制库文件（选择对应芯片）
cp porcupine/lib/esp32-s3/libpv_porcupine.a components/xn_porcupine_wake/lib/

# 复制模型文件
cp porcupine/lib/common/porcupine_params.pv components/xn_porcupine_wake/model/
cp your_custom_keyword.ppn components/xn_porcupine_wake/model/
```

### 4.2 创建 CMakeLists.txt

```cmake
idf_component_register(
    SRCS "src/porcupine_wake_module.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)

# 链接 Porcupine 静态库
target_link_libraries(${COMPONENT_LIB} INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/libpv_porcupine.a"
)

# 嵌入模型文件
target_add_binary_data(${COMPONENT_LIB} "model/porcupine_params.pv" BINARY)
target_add_binary_data(${COMPONENT_LIB} "model/keyword.ppn" BINARY)
```

### 4.3 实现代码

```c
#include "pv_porcupine.h"
#include "esp_log.h"

static const char *TAG = "porcupine";

// 外部嵌入的模型数据
extern const uint8_t porcupine_params_start[] asm("_binary_porcupine_params_pv_start");
extern const uint8_t porcupine_params_end[] asm("_binary_porcupine_params_pv_end");
extern const uint8_t keyword_start[] asm("_binary_keyword_ppn_start");
extern const uint8_t keyword_end[] asm("_binary_keyword_ppn_end");

static pv_porcupine_t *porcupine = NULL;

esp_err_t porcupine_init(const char *access_key)
{
    // 模型大小
    size_t model_size = porcupine_params_end - porcupine_params_start;
    size_t keyword_size = keyword_end - keyword_start;
    
    // 灵敏度 (0.0 - 1.0)
    float sensitivity = 0.5f;
    
    // 初始化 Porcupine
    pv_status_t status = pv_porcupine_init(
        access_key,
        model_size,
        porcupine_params_start,
        1,                          // 唤醒词数量
        &keyword_size,
        &keyword_start,
        &sensitivity,
        &porcupine
    );
    
    if (status != PV_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Porcupine init failed: %d", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Porcupine initialized, frame_length: %d", 
             pv_porcupine_frame_length());
    
    return ESP_OK;
}

int porcupine_process(const int16_t *audio_frame)
{
    int32_t keyword_index = -1;
    
    pv_status_t status = pv_porcupine_process(porcupine, audio_frame, &keyword_index);
    
    if (status != PV_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Process error: %d", status);
        return -1;
    }
    
    if (keyword_index >= 0) {
        ESP_LOGI(TAG, "Wake word detected! index: %d", keyword_index);
    }
    
    return keyword_index;
}

void porcupine_deinit(void)
{
    if (porcupine) {
        pv_porcupine_delete(porcupine);
        porcupine = NULL;
    }
}
```

---

## 步骤 5：音频采集

Porcupine 要求：
- 采样率：16kHz
- 位深度：16-bit
- 声道：单声道
- 帧大小：512 samples（通过 `pv_porcupine_frame_length()` 获取）

```c
// 每次处理 512 个采样点
#define FRAME_LENGTH 512

int16_t audio_frame[FRAME_LENGTH];

// 从 I2S 读取音频
i2s_read(I2S_NUM_0, audio_frame, sizeof(audio_frame), &bytes_read, portMAX_DELAY);

// 处理音频帧
int result = porcupine_process(audio_frame);
if (result >= 0) {
    // 检测到唤醒词
    on_wake_word_detected();
}
```

---

## 注意事项

### Access Key

- 每个账号有一个 Access Key
- 个人免费版有使用限制（每月激活次数）
- Access Key 需要在代码中配置

### 中文唤醒词

- Porcupine 对中文支持有限
- 建议使用拼音输入唤醒词
- 中文效果不如英文，可能需要多次尝试

### 内存占用

- Porcupine 需要约 20-30KB RAM
- 模型文件约 1-2MB（存放在 Flash）

### 商用授权

- 个人/非商业项目：免费
- 商业项目：需要购买授权
- 详情：https://picovoice.ai/pricing/

---

## 与 Edge Impulse 对比

| 特性 | Picovoice Porcupine | Edge Impulse |
|------|---------------------|--------------|
| 训练难度 | 简单（输入文字即可） | 需要准备音频数据 |
| 准确率 | 高 | 取决于训练数据 |
| 中文支持 | 有限 | 取决于训练数据 |
| 免费额度 | 有限制 | 20分钟音频 |
| 自定义程度 | 低（只能调灵敏度） | 高（可调整模型） |
| 离线运行 | ✅ | ✅ |

---

## 参考资源

- Picovoice 官网：https://picovoice.ai/
- Porcupine GitHub：https://github.com/Picovoice/porcupine
- ESP32 示例：https://github.com/Picovoice/porcupine/tree/master/demo/mcu/esp32
- API 文档：https://picovoice.ai/docs/porcupine/
