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