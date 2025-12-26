import edge_tts
import asyncio
import os
import random
import subprocess

# ========== 配置区域 ==========
OUTPUT_DIR = "negative_audio"
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

ALL_NEGATIVE_WORDS = DAILY_WORDS + SIMILAR_WORDS + NUMBERS_AND_PHRASES

VOICES = [
    "zh-CN-XiaoxiaoNeural",
    "zh-CN-YunxiNeural",
    "zh-CN-YunyangNeural",
    "zh-CN-XiaoyiNeural",
    "zh-CN-YunjianNeural",
]

RATES = ["-10%", "+0%", "+10%"]

def convert_mp3_to_wav(mp3_path, wav_path):
    """使用 ffmpeg 转换 MP3 到 WAV (16kHz, 16bit, mono)"""
    try:
        subprocess.run([
            'ffmpeg', '-y', '-i', mp3_path,
            '-ar', '16000',
            '-ac', '1',
            '-sample_fmt', 's16',
            wav_path
        ], capture_output=True, check=True)
        os.remove(mp3_path)
        return True
    except:
        return False

async def generate_single(word, voice, rate, filename, retries=3):
    """生成单个音频，带重试机制"""
    for attempt in range(retries):
        try:
            tts = edge_tts.Communicate(text=word, voice=voice, rate=rate)
            await tts.save(filename)
            return True
        except:
            if attempt < retries - 1:
                voice = random.choice(VOICES)
                await asyncio.sleep(0.5)
            else:
                return False
    return False

async def generate_negative():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    failed = 0
    
    print("生成负样本音频（WAV 格式）...\n")
    
    for word in ALL_NEGATIVE_WORDS:
        for i in range(2):
            voice = random.choice(VOICES)
            rate = random.choice(RATES)
            
            mp3_file = f"{OUTPUT_DIR}/temp_{count:04d}.mp3"
            wav_file = f"{OUTPUT_DIR}/neg_{count:04d}.wav"
            
            success = await generate_single(word, voice, rate, mp3_file)
            
            if success and convert_mp3_to_wav(mp3_file, wav_file):
                count += 1
                print(f"[{count}] {word}")
            else:
                failed += 1
            
            await asyncio.sleep(0.1)
    
    print(f"\n完成！共生成 {count} 条负样本音频")
    if failed > 0:
        print(f"跳过 {failed} 条")
    print(f"保存在 {OUTPUT_DIR}/ 目录")

if __name__ == "__main__":
    asyncio.run(generate_negative())
