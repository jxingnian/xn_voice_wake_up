"""
生成静音和噪声负样本
用于唤醒词模型训练
"""

import numpy as np
from scipy.io import wavfile
import os

# 输出目录
OUTPUT_DIR = "noise_audio"

# 音频参数
SAMPLE_RATE = 16000
DURATION = 1.0  # 1秒

def generate_silence(filename, duration=DURATION):
    """生成静音"""
    samples = int(SAMPLE_RATE * duration)
    # 添加极小的噪声，避免完全静音
    audio = np.random.normal(0, 50, samples).astype(np.int16)
    wavfile.write(filename, SAMPLE_RATE, audio)

def generate_white_noise(filename, amplitude=1000, duration=DURATION):
    """生成白噪声"""
    samples = int(SAMPLE_RATE * duration)
    audio = np.random.normal(0, amplitude, samples).astype(np.int16)
    wavfile.write(filename, SAMPLE_RATE, audio)

def generate_pink_noise(filename, amplitude=1000, duration=DURATION):
    """生成粉红噪声（更接近环境噪声）"""
    samples = int(SAMPLE_RATE * duration)
    # 简单的粉红噪声生成
    white = np.random.randn(samples)
    # 低通滤波近似粉红噪声
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    a = [1, -2.494956002, 2.017265875, -0.522189400]
    from scipy.signal import lfilter
    pink = lfilter(b, a, white)
    pink = (pink / np.max(np.abs(pink)) * amplitude).astype(np.int16)
    wavfile.write(filename, SAMPLE_RATE, pink)

def generate_hum_noise(filename, freq=50, amplitude=500, duration=DURATION):
    """生成电源哼声（50Hz/60Hz）"""
    samples = int(SAMPLE_RATE * duration)
    t = np.linspace(0, duration, samples)
    # 基频 + 谐波
    audio = amplitude * np.sin(2 * np.pi * freq * t)
    audio += amplitude * 0.3 * np.sin(2 * np.pi * freq * 2 * t)
    audio += amplitude * 0.1 * np.sin(2 * np.pi * freq * 3 * t)
    # 添加一些随机噪声
    audio += np.random.normal(0, amplitude * 0.1, samples)
    wavfile.write(filename, SAMPLE_RATE, audio.astype(np.int16))

def generate_fan_noise(filename, amplitude=800, duration=DURATION):
    """生成风扇噪声（低频为主）"""
    samples = int(SAMPLE_RATE * duration)
    # 低频噪声
    white = np.random.randn(samples)
    # 简单低通滤波
    from scipy.signal import butter, filtfilt
    b, a = butter(4, 500 / (SAMPLE_RATE / 2), btype='low')
    filtered = filtfilt(b, a, white)
    audio = (filtered / np.max(np.abs(filtered)) * amplitude).astype(np.int16)
    wavfile.write(filename, SAMPLE_RATE, audio)

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    
    # 生成静音样本 (30个)
    print("生成静音样本...")
    for i in range(30):
        filename = f"{OUTPUT_DIR}/noise_{count:04d}.wav"
        generate_silence(filename)
        print(f"[{count+1}] 静音 -> {filename}")
        count += 1
    
    # 生成白噪声样本 (不同强度, 20个)
    print("\n生成白噪声样本...")
    for amp in [300, 500, 800, 1000, 1500]:
        for i in range(4):
            filename = f"{OUTPUT_DIR}/noise_{count:04d}.wav"
            generate_white_noise(filename, amplitude=amp)
            print(f"[{count+1}] 白噪声(amp={amp}) -> {filename}")
            count += 1
    
    # 生成粉红噪声样本 (20个)
    print("\n生成粉红噪声样本...")
    for amp in [300, 500, 800, 1000, 1500]:
        for i in range(4):
            filename = f"{OUTPUT_DIR}/noise_{count:04d}.wav"
            generate_pink_noise(filename, amplitude=amp)
            print(f"[{count+1}] 粉红噪声(amp={amp}) -> {filename}")
            count += 1
    
    # 生成电源哼声样本 (10个)
    print("\n生成电源哼声样本...")
    for freq in [50, 60]:  # 50Hz (中国) 和 60Hz (美国)
        for amp in [300, 500, 800, 1000, 1500]:
            filename = f"{OUTPUT_DIR}/noise_{count:04d}.wav"
            generate_hum_noise(filename, freq=freq, amplitude=amp)
            print(f"[{count+1}] 电源哼声({freq}Hz, amp={amp}) -> {filename}")
            count += 1
    
    # 生成风扇噪声样本 (20个)
    print("\n生成风扇噪声样本...")
    for amp in [300, 500, 800, 1000, 1500]:
        for i in range(4):
            filename = f"{OUTPUT_DIR}/noise_{count:04d}.wav"
            generate_fan_noise(filename, amplitude=amp)
            print(f"[{count+1}] 风扇噪声(amp={amp}) -> {filename}")
            count += 1
    
    print(f"\n完成！共生成 {count} 条噪声样本")
    print(f"保存在 {OUTPUT_DIR}/ 目录")
    print("\n请将这些文件上传到 Edge Impulse 作为 'noise' 或 'unknown' 标签的负样本")

if __name__ == "__main__":
    main()
