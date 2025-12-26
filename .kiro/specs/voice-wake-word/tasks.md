# 实现计划：语音唤醒组件 (xn_voice_wake)

## 概述

基于 Edge Impulse 导出的 SDK 实现语音唤醒组件，集成到现有 ESP32-S3 项目中。

## 任务列表

- [x] 1. 创建组件基础结构
  - 创建 `components/xn_voice_wake/` 目录结构
  - 创建 CMakeLists.txt
  - 集成 Edge Impulse SDK
  - _需求: 1.1, 1.2_

- [x] 2. 实现主模块接口
  - [x] 2.1 创建 voice_wake_module.h 头文件
    - 定义配置结构体和默认配置宏
    - 定义状态枚举和回调类型
    - 声明公共 API
    - _需求: 1.1, 6.1, 9.1-9.6_

  - [x] 2.2 实现 voice_wake_module.c
    - 实现初始化/反初始化
    - 实现开始/停止监听
    - 实现状态管理
    - _需求: 1.2-1.6, 6.4-6.7, 7.3-7.6_

- [x] 3. 实现音频处理模块
  - [x] 3.1 创建 audio_processor.h 头文件
    - 定义音频配置结构体
    - 声明音频采集 API
    - _需求: 3.1-3.4_

  - [x] 3.2 实现 audio_processor.c
    - 实现 I2S 初始化和配置
    - 实现 DMA 音频采集
    - 实现暂停/恢复功能
    - _需求: 3.1-3.7_

- [x] 4. 实现检测引擎
  - [x] 4.1 创建 detection_engine.h 头文件
    - 定义检测配置结构体
    - 声明检测 API
    - _需求: 5.1-5.7_

  - [x] 4.2 实现 detection_engine.c
    - 集成 Edge Impulse 推理
    - 实现滑动窗口逻辑
    - 实现冷却时间控制
    - _需求: 5.1-5.7_

- [x] 5. 集成测试
  - [x] 5.1 创建示例应用
    - 在 main.c 中添加唤醒词检测示例
    - 测试完整流程
    - _需求: 1.1, 6.3_

- [ ] 6. 检查点 - 确保编译通过并能运行

## 注意事项

- 使用 Edge Impulse SDK 而非从头实现 TFLite
- 遵循现有组件风格（参考 xn_ota_manger）
- 所有内存分配使用 PSRAM
