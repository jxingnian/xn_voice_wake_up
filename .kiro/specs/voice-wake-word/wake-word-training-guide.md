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
pip install edge-tts
```

### 安装 ffmpeg（必须）

Edge Impulse 只支持 WAV 格式，需要 ffmpeg 转换音频格式。

**Windows 安装方法：**

```bash
# 方法一：使用 winget（推荐）
winget install ffmpeg

# 方法二：使用 scoop
scoop install ffmpeg

# 方法三：手动下载
# 1. 访问 https://ffmpeg.org/download.html
# 2. 下载 Windows 版本
# 3. 解压后把 bin 目录添加到系统 PATH
```

验证安装：
```bash
ffmpeg -version
```

### 注册 Edge Impulse 账号

1. 打开 https://edgeimpulse.com
2. 点击 "Sign up" 注册（可用 GitHub 账号登录）
3. 免费版支持 20 分钟音频，足够个人项目使用

---

## 音频生成

### 生成唤醒词音频（正样本）

脚本位置：`doc/generate_wake_word.py`

```python
# 修改唤醒词
WAKE_WORD = "你好星年"  # 改成你的唤醒词
```

运行：
```bash
python doc/generate_wake_word.py
```

**生成结果：**
- 约 200-300 条 WAV 音频
- 包含 9 种不同声音（男声、女声、方言）
- 7 种语速变化（-30% ~ +30%）
- 5 种音调变化（-10Hz ~ +10Hz）
- 格式：16kHz, 16bit, 单声道
- 保存在 `wake_word_audio/` 目录

### 生成负样本音频

脚本位置：`doc/generate_negative.py`

运行：
```bash
python doc/generate_negative.py
```

**生成结果：**
- 约 100-150 条 WAV 音频
- 包含日常对话词语
- 包含相似发音词语（减少误唤醒）
- 包含数字和常用短语
- 保存在 `negative_audio/` 目录

---

## Edge Impulse 使用

### 步骤 1：创建项目

1. 登录 https://studio.edgeimpulse.com
2. 点击 "Create new project"
3. 项目名称：`wake-word-detection`
4. 选择 "Audio" 类型

### 步骤 2：上传数据

1. 点击左侧菜单 "Data acquisition"
2. 点击 "Upload data"
3. 上传设置：
   - 上传模式：选择一个文件夹
   - 上传至类别：训练
   - 标签：输入标签
4. 分两次上传：
   - **唤醒词音频**：选择 `wake_word_audio` 文件夹，标签填 `wake_word`
   - **负样本音频**：选择 `negative_audio` 文件夹，标签填 `noise`

### 步骤 3：创建 Impulse

1. 点击左侧菜单 "Create impulse"
2. 配置如下：

```
Time series data:
- Window size: 1000 ms
- Window increase: 500 ms
- Frequency: 16000 Hz

Processing block:
- 选择 "Audio (MFCC)"

Learning block:
- 选择 "Classification"
```

3. 点击 "Save Impulse"

### 步骤 4：生成特征

1. 点击左侧菜单 "MFCC"
2. 使用默认参数，点击 "Save parameters"
3. 点击 "Generate features"
4. 等待处理完成
5. 查看特征可视化，确保 wake_word 和 noise 能够区分

### 步骤 5：训练模型

1. 点击左侧菜单 "Classifier"
2. 在 "Neural Network settings" 区域配置：

```
Training settings:
- Number of training cycles: 100
- Learning rate: 0.005
- Training processor: CPU（免费版只能用 CPU）

Audio training options:
- Data augmentation: 可选勾选（增加数据多样性）

Neural network architecture:
- 使用默认配置即可，或点击 "Load preset" 选择预设
```

3. 点击 "Save & train" 按钮
4. 等待训练完成（几分钟）
5. 查看 "Training output" 区域的准确率

### 步骤 6：测试模型

1. 点击左侧菜单 "Model testing"
2. 点击 "Classify all"
3. 查看准确率

**目标准确率**：
- 90%+ 为良好
- 95%+ 为优秀

---

## 模型导出与部署

### 导出 TensorFlow Lite 模型

1. 点击左侧菜单 "Deployment"
2. 选择 "TensorFlow Lite (int8 quantized)"（推荐，模型更小）
3. 点击 "Build"
4. 下载生成的 ZIP 文件

### 导出为 C++ 源码（推荐用于 ESP32）

1. 选择 "C++ library"
2. 点击 "Build"
3. 下载 ZIP 文件

ZIP 文件包含：
- `model.h` - 模型数据
- `model_metadata.h` - 模型元数据
- 推理代码示例

---

## 在 ESP32 上使用模型

### 使用 Edge Impulse SDK

```cpp
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

void classify_audio() {
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &get_audio_data;
    
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    
    if (res == EI_IMPULSE_OK) {
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

---

## 常见问题

### Q: 上传失败，提示 Invalid mimetype？

Edge Impulse 只支持 WAV 格式。确保：
1. 已安装 ffmpeg
2. 使用最新版本的生成脚本（会自动转换为 WAV）

### Q: 准确率太低怎么办？

1. 增加训练数据
2. 确保唤醒词和负样本数量平衡
3. 增加训练轮数到 200
4. 尝试不同的处理块（MFCC vs MFE）

### Q: 误唤醒太多怎么办？

1. 在 `generate_negative.py` 中添加更多相似发音词
2. 提高置信度阈值（0.8 → 0.9）
3. 添加滑动窗口平均

### Q: 漏唤醒太多怎么办？

1. 增加更多唤醒词样本
2. 降低置信度阈值
3. 确保训练数据覆盖实际使用场景

### Q: 模型太大怎么办？

1. 使用 int8 量化（推荐）
2. 减少神经网络层数
3. 使用更小的窗口大小

---

## 参考资源

- Edge Impulse 官方文档：https://docs.edgeimpulse.com
- Edge TTS 项目：https://github.com/rany2/edge-tts
- ffmpeg 下载：https://ffmpeg.org/download.html
