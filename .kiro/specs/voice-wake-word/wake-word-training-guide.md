# 唤醒词训练指南

本文档介绍如何使用 Edge Impulse 平台训练自定义唤醒词模型，以及如何使用 TTS 自动生成训练音频。

## 目录

1. [环境准备](#环境准备)
2. [音频生成](#音频生成)
3. [Edge Impulse 使用](#edge-impulse-使用)
4. [模型导出与部署](#模型导出与部署)

---

## 环境准备

### 安装 Python 依赖

```bash
# 安装 edge-tts（用于生成唤醒词音频）
pip install edge-tts

# 安装 pydub（用于音频处理，可选）
pip install pydub
```

### 注册 Edge Impulse 账号

1. 打开 https://edgeimpulse.com
2. 点击 "Sign up" 注册（可用 GitHub 账号登录）
3. 免费版足够个人项目使用

---

## 音频生成

### 方案一：使用 Edge TTS 批量生成（推荐）

创建文件 `generate_wake_word.py`：

```python
import edge_tts
import asyncio
import os

# ========== 配置区域 ==========
WAKE_WORD = "你好星年"  # 修改为你的唤醒词
OUTPUT_DIR = "wake_word_audio"
# ==============================

# 中文声音列表
VOICES = [
    "zh-CN-XiaoxiaoNeural",    # 女声-晓晓
    "zh-CN-YunxiNeural",       # 男声-云希
    "zh-CN-YunyangNeural",     # 男声-云扬
    "zh-CN-XiaoyiNeural",      # 女声-晓伊
    "zh-CN-XiaochenNeural",    # 女声-晓辰
    "zh-CN-XiaohanNeural",     # 女声-晓涵
    "zh-CN-XiaomengNeural",    # 女声-晓梦
    "zh-CN-XiaomoNeural",      # 女声-晓墨
    "zh-CN-XiaoqiuNeural",     # 女声-晓秋
    "zh-CN-XiaoruiNeural",     # 女声-晓睿
]

# 语速变化
RATES = ["-30%", "-20%", "-10%", "+0%", "+10%", "+20%", "+30%"]

# 音调变化
PITCHES = ["-10Hz", "+0Hz", "+10Hz"]

async def generate_audio():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    total = len(VOICES) * len(RATES) * len(PITCHES)
    
    for voice in VOICES:
        for rate in RATES:
            for pitch in PITCHES:
                filename = f"{OUTPUT_DIR}/wake_{count:04d}.mp3"
                
                tts = edge_tts.Communicate(
                    text=WAKE_WORD,
                    voice=voice,
                    rate=rate,
                    pitch=pitch
                )
                
                await tts.save(filename)
                count += 1
                print(f"[{count}/{total}] 生成: {filename} (声音:{voice}, 语速:{rate}, 音调:{pitch})")
    
    print(f"\n完成！共生成 {count} 条音频，保存在 {OUTPUT_DIR}/ 目录")

if __name__ == "__main__":
    asyncio.run(generate_audio())
```

运行脚本：

```bash
python generate_wake_word.py
```

这将自动生成约 210 条不同声音、语速、音调的唤醒词音频。


### 方案二：生成负样本音频

创建文件 `generate_negative.py`：

```python
import edge_tts
import asyncio
import os
import random

# ========== 配置区域 ==========
OUTPUT_DIR = "negative_audio"
WAKE_WORD = "你好星年"  # 你的唤醒词，用于生成相似但不同的词
# ==============================

# 日常对话词语
DAILY_WORDS = [
    "今天天气不错", "明天有雨吗", "现在几点了",
    "打开灯", "关闭灯", "开灯", "关灯",
    "打开空调", "关闭空调", "温度调高", "温度调低",
    "播放音乐", "暂停播放", "继续播放", "下一首", "上一首",
    "声音大一点", "声音小一点", "静音",
    "帮我设个闹钟", "取消闹钟",
    "我要睡觉了", "早上好", "晚安", "谢谢", "好的", "不要",
]

# 相似发音词语（重要！减少误唤醒）
SIMILAR_WORDS = [
    "你好", "星年", "你好啊", "星年好",
    "你好星", "好星年", "你星年", "你好年",
    "李星年", "王星年", "星年你好",
    "你好星星", "你好新年", "你好心愿",
]

# 数字和常用短语
NUMBERS_AND_PHRASES = [
    "一二三四五", "六七八九十",
    "星期一", "星期二", "星期三",
    "是的", "不是", "可以", "不行",
    "等一下", "马上", "稍等",
]

# 所有负样本词语
ALL_NEGATIVE_WORDS = DAILY_WORDS + SIMILAR_WORDS + NUMBERS_AND_PHRASES

VOICES = [
    "zh-CN-XiaoxiaoNeural",    # 女声-晓晓
    "zh-CN-YunxiNeural",       # 男声-云希
    "zh-CN-YunyangNeural",     # 男声-云扬
    "zh-CN-XiaoyiNeural",      # 女声-晓伊
    "zh-CN-XiaochenNeural",    # 女声-晓辰
]

RATES = ["-20%", "-10%", "+0%", "+10%", "+20%"]

async def generate_negative():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    
    for word in ALL_NEGATIVE_WORDS:
        # 每个词用 2 种不同声音和语速
        for _ in range(2):
            voice = random.choice(VOICES)
            rate = random.choice(RATES)
            
            filename = f"{OUTPUT_DIR}/neg_{count:04d}.mp3"
            
            tts = edge_tts.Communicate(
                text=word,
                voice=voice,
                rate=rate
            )
            
            await tts.save(filename)
            count += 1
            print(f"[{count}] {word} -> {filename}")
    
    print(f"\n完成！共生成 {count} 条负样本音频")
    print(f"保存在 {OUTPUT_DIR}/ 目录")

if __name__ == "__main__":
    asyncio.run(generate_negative())
```

**这个脚本会生成约 100-150 条负样本**，包括：
- 日常对话词语
- **相似发音词语**（重要！减少误唤醒）
- 数字和常用短语

---

## Edge Impulse 使用

### 步骤 1：创建项目

1. 登录 https://studio.edgeimpulse.com
2. 点击 "Create new project"
3. 项目名称：`wake-word-detection`（或你喜欢的名字）
4. 选择 "Audio" 类型

### 步骤 2：上传数据

1. 点击左侧菜单 "Data acquisition"
2. 点击 "Upload data"
3. 选择上传方式：
   - **唤醒词音频**：Label 填 `wake_word`
   - **负样本音频**：Label 填 `noise` 或 `unknown`
4. 上传之前生成的音频文件

**数据分配建议**：
- Training: 80%
- Testing: 20%
（上传时可以选择自动分配）

### 步骤 3：创建 Impulse

1. 点击左侧菜单 "Create impulse"
2. 配置如下：

```
Time series data:
- Window size: 1000 ms
- Window increase: 500 ms
- Frequency: 16000 Hz

Processing block:
- 选择 "Audio (MFCC)" 或 "Audio (MFE)"

Learning block:
- 选择 "Classification"
```

3. 点击 "Save Impulse"

### 步骤 4：生成特征

1. 点击左侧菜单 "MFCC"（或你选择的处理块）
2. 点击 "Generate features"
3. 等待处理完成
4. 查看特征可视化，确保唤醒词和噪声能够区分

### 步骤 5：训练模型

1. 点击左侧菜单 "Classifier"
2. 配置训练参数：

```
Number of training cycles: 100
Learning rate: 0.005
Minimum confidence rating: 0.6
```

3. 点击 "Start training"
4. 等待训练完成（几分钟）

### 步骤 6：测试模型

1. 点击左侧菜单 "Model testing"
2. 点击 "Classify all"
3. 查看准确率

**目标准确率**：
- 90%+ 为良好
- 95%+ 为优秀

如果准确率不够，可以：
- 增加训练数据
- 调整训练参数
- 增加训练轮数

---

## 模型导出与部署

### 导出 TensorFlow Lite 模型

1. 点击左侧菜单 "Deployment"
2. 选择 "TensorFlow Lite (float32)" 或 "TensorFlow Lite (int8 quantized)"
3. 点击 "Build"
4. 下载生成的 ZIP 文件

**推荐选择 int8 quantized**：模型更小，适合 ESP32

### 导出为 Arduino 库（可选）

1. 选择 "Arduino library"
2. 点击 "Build"
3. 下载 ZIP 文件
4. 在 Arduino IDE 中导入库

### 导出为 C++ 源码

1. 选择 "C++ library"
2. 点击 "Build"
3. 下载 ZIP 文件

ZIP 文件包含：
- `model.h` - 模型数据
- `model_metadata.h` - 模型元数据
- 推理代码示例

---

## 在 ESP32 上使用模型

### 方法一：使用 Edge Impulse SDK

```cpp
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// 音频缓冲区
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

void classify_audio() {
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &get_audio_data;
    
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    
    if (res == EI_IMPULSE_OK) {
        // 检查是否检测到唤醒词
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            if (strcmp(result.classification[i].label, "wake_word") == 0) {
                if (result.classification[i].value > 0.8) {
                    printf("检测到唤醒词！置信度: %.2f\n", result.classification[i].value);
                }
            }
        }
    }
}
```

### 方法二：使用 TFLite Micro

将导出的 `.tflite` 模型转换为 C 数组，然后使用 TensorFlow Lite Micro 加载运行。

---

## 常见问题

### Q: 准确率太低怎么办？

1. 增加训练数据（更多声音、更多变化）
2. 确保唤醒词和负样本数量平衡
3. 增加训练轮数
4. 尝试不同的处理块（MFCC vs MFE）

### Q: 误唤醒太多怎么办？

1. 增加更多负样本（特别是相似发音的词）
2. 提高置信度阈值
3. 添加滑动窗口平均

### Q: 漏唤醒太多怎么办？

1. 增加更多唤醒词样本（不同声音、语速）
2. 降低置信度阈值
3. 确保训练数据覆盖实际使用场景

### Q: 模型太大怎么办？

1. 使用 int8 量化
2. 减少神经网络层数
3. 使用更小的窗口大小

---

## 参考资源

- Edge Impulse 官方文档：https://docs.edgeimpulse.com
- Edge TTS 项目：https://github.com/rany2/edge-tts
- TensorFlow Lite Micro：https://www.tensorflow.org/lite/microcontrollers
