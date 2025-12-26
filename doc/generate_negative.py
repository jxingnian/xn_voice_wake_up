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
    "请打开灯", "请关闭灯", "把灯打开", "把灯关掉",
    "打开空调", "关闭空调", "温度调高一点", "温度调低一点",
    "播放音乐", "暂停播放", "继续播放", "下一首歌", "上一首歌",
    "声音大一点", "声音小一点", "静音模式",
    "帮我设个闹钟", "取消闹钟",
    "我要睡觉了", "早上好啊", "晚安啦", "谢谢你", "好的呢", "不要啦",
]

# 相似发音词语（重要！减少误唤醒）
SIMILAR_WORDS = [
    "你好呀", "星年好", "你好啊", "星年呢",
    "你好星", "好星年", "你星年", "你好年",
    "李星年", "王星年", "星年你好",
    "你好星星", "你好新年", "你好心愿",
]

# 数字和常用短语
NUMBERS_AND_PHRASES = [
    "一二三四五", "六七八九十",
    "星期一了", "星期二了", "星期三了",
    "是的呢", "不是的", "可以的", "不行啊",
    "等一下啊", "马上来", "稍等一下",
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

# 使用较温和的语速变化，避免极端值导致错误
RATES = ["-10%", "+0%", "+10%"]

async def generate_single(word, voice, rate, filename, retries=3):
    """生成单个音频，带重试机制"""
    for attempt in range(retries):
        try:
            tts = edge_tts.Communicate(
                text=word,
                voice=voice,
                rate=rate
            )
            await tts.save(filename)
            return True
        except Exception as e:
            if attempt < retries - 1:
                # 重试时换一个声音
                voice = random.choice(VOICES)
                await asyncio.sleep(0.5)
            else:
                print(f"  [跳过] {word} 生成失败: {e}")
                return False
    return False

async def generate_negative():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    failed = 0
    
    for word in ALL_NEGATIVE_WORDS:
        # 每个词用 2 种不同声音和语速
        for i in range(2):
            voice = random.choice(VOICES)
            rate = random.choice(RATES)
            
            filename = f"{OUTPUT_DIR}/neg_{count:04d}.mp3"
            
            success = await generate_single(word, voice, rate, filename)
            
            if success:
                count += 1
                print(f"[{count}] {word} -> {filename}")
            else:
                failed += 1
            
            # 添加小延迟，避免请求过快
            await asyncio.sleep(0.1)
    
    print(f"\n完成！共生成 {count} 条负样本音频")
    if failed > 0:
        print(f"跳过 {failed} 条失败的音频")
    print(f"保存在 {OUTPUT_DIR}/ 目录")

if __name__ == "__main__":
    asyncio.run(generate_negative())
