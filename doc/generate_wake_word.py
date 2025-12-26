import edge_tts
import asyncio
import os

# ========== 配置区域 ==========
WAKE_WORD = "你好星年"  # 修改为你的唤醒词
OUTPUT_DIR = "wake_word_audio"
# ==============================

# 中文声音列表（只保留稳定的声音）
VOICES = [
    "zh-CN-XiaoxiaoNeural",    # 女声-晓晓
    "zh-CN-YunxiNeural",       # 男声-云希
    "zh-CN-YunyangNeural",     # 男声-云扬
    "zh-CN-XiaoyiNeural",      # 女声-晓伊
    # 以下声音可能不稳定，已移除
    # "zh-CN-XiaochenNeural",
    # "zh-CN-XiaohanNeural",
    # "zh-CN-XiaomengNeural",
    # "zh-CN-XiaomoNeural",
    # "zh-CN-XiaoqiuNeural",
    # "zh-CN-XiaoruiNeural",
]

# 语速变化（使用温和的范围）
RATES = ["-20%", "-10%", "+0%", "+10%", "+20%"]

# 音调变化（使用温和的范围）
PITCHES = ["-5Hz", "+0Hz", "+5Hz"]

async def generate_single(text, voice, rate, pitch, filename, retries=3):
    """生成单个音频，带重试机制"""
    for attempt in range(retries):
        try:
            tts = edge_tts.Communicate(
                text=text,
                voice=voice,
                rate=rate,
                pitch=pitch
            )
            await tts.save(filename)
            return True
        except Exception as e:
            if attempt < retries - 1:
                await asyncio.sleep(0.5)
            else:
                print(f"  [跳过] 生成失败 ({voice}, {rate}, {pitch}): {e}")
                return False
    return False

async def generate_audio():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    failed = 0
    total = len(VOICES) * len(RATES) * len(PITCHES)
    
    for voice in VOICES:
        for rate in RATES:
            for pitch in PITCHES:
                filename = f"{OUTPUT_DIR}/wake_{count:04d}.mp3"
                
                success = await generate_single(WAKE_WORD, voice, rate, pitch, filename)
                
                if success:
                    count += 1
                    print(f"[{count}/{total}] 生成: {filename} (声音:{voice.split('-')[-1]}, 语速:{rate}, 音调:{pitch})")
                else:
                    failed += 1
                
                # 添加小延迟，避免请求过快
                await asyncio.sleep(0.1)
    
    print(f"\n完成！共生成 {count} 条音频，保存在 {OUTPUT_DIR}/ 目录")
    if failed > 0:
        print(f"跳过 {failed} 条失败的音频")

if __name__ == "__main__":
    asyncio.run(generate_audio())
