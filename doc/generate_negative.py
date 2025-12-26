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
# 根据你的唤醒词修改这些词
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