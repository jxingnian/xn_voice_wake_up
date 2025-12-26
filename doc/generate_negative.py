import edge_tts
import asyncio
import os
import random

# ========== 配置区域 ==========
OUTPUT_DIR = "negative_audio"
# ==============================

# 随机词语（用于生成"非唤醒词"样本）
RANDOM_WORDS = [
    "今天天气不错",
    "打开灯",
    "关闭空调",
    "播放音乐",
    "现在几点了",
    "明天有雨吗",
    "帮我设个闹钟",
    "声音大一点",
    "声音小一点",
    "下一首歌",
    "暂停播放",
    "继续播放",
    "我要睡觉了",
    "早上好",
    "晚安",
    "谢谢你",
    "没问题",
    "好的",
    "不要",
    "等一下",
]

VOICES = [
    "zh-CN-XiaoxiaoNeural",
    "zh-CN-YunxiNeural",
    "zh-CN-YunyangNeural",
    "zh-CN-XiaoyiNeural",
]

RATES = ["-20%", "+0%", "+20%"]

async def generate_negative():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    
    for word in RANDOM_WORDS:
        for voice in VOICES:
            rate = random.choice(RATES)
            filename = f"{OUTPUT_DIR}/neg_{count:04d}.mp3"
            
            tts = edge_tts.Communicate(
                text=word,
                voice=voice,
                rate=rate
            )
            
            await tts.save(filename)
            count += 1
            print(f"[{count}] 生成: {filename} - {word}")
    
    print(f"\n完成！共生成 {count} 条负样本音频")

if __name__ == "__main__":
    asyncio.run(generate_negative())