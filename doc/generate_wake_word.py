import edge_tts
import asyncio
import os
import subprocess

# ========== 配置区域 ==========
WAKE_WORD = "你好星年"  # 修改为你的唤醒词
OUTPUT_DIR = "wake_word_audio"
# ==============================

# 中文声音列表（稳定的声音）
VOICES = [
    "zh-CN-XiaoxiaoNeural",    # 女声-晓晓
    "zh-CN-YunxiNeural",       # 男声-云希
    "zh-CN-YunyangNeural",     # 男声-云扬
    "zh-CN-XiaoyiNeural",      # 女声-晓伊
    "zh-CN-YunjianNeural",     # 男声-云健
    "zh-CN-YunxiaNeural",      # 男声-云夏
    "zh-CN-liaoning-XiaobeiNeural",  # 东北女声
    "zh-TW-HsiaoChenNeural",   # 台湾女声
    "zh-TW-YunJheNeural",      # 台湾男声
]

# 语速变化
RATES = ["-30%", "-20%", "-10%", "+0%", "+10%", "+20%", "+30%"]

# 音调变化
PITCHES = ["-10Hz", "-5Hz", "+0Hz", "+5Hz", "+10Hz"]

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
                return False
    return False

def convert_mp3_to_wav(mp3_path, wav_path):
    """使用 ffmpeg 转换 MP3 到 WAV (16kHz, 16bit, mono)"""
    try:
        subprocess.run([
            'ffmpeg', '-y', '-i', mp3_path,
            '-ar', '16000',  # 采样率 16kHz
            '-ac', '1',      # 单声道
            '-sample_fmt', 's16',  # 16bit
            wav_path
        ], capture_output=True, check=True)
        os.remove(mp3_path)  # 删除 MP3
        return True
    except Exception as e:
        print(f"  转换失败: {e}")
        return False

async def generate_audio():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    count = 0
    failed = 0
    total = len(VOICES) * len(RATES) * len(PITCHES)
    
    print(f"预计生成 {total} 条音频...\n")
    print("注意：需要安装 ffmpeg 来转换格式")
    print("下载地址：https://ffmpeg.org/download.html\n")
    
    for voice in VOICES:
        voice_name = voice.split('-')[-1]
        for rate in RATES:
            for pitch in PITCHES:
                mp3_file = f"{OUTPUT_DIR}/temp_{count:04d}.mp3"
                wav_file = f"{OUTPUT_DIR}/wake_{count:04d}.wav"
                
                # 生成 MP3
                success = await generate_single(WAKE_WORD, voice, rate, pitch, mp3_file)
                
                if success:
                    # 转换为 WAV
                    if convert_mp3_to_wav(mp3_file, wav_file):
                        count += 1
                        print(f"[{count}] {voice_name} {rate} {pitch}")
                    else:
                        failed += 1
                else:
                    failed += 1
                
                await asyncio.sleep(0.05)
    
    print(f"\n完成！共生成 {count} 条 WAV 音频")
    if failed > 0:
        print(f"跳过 {failed} 条失败的音频")
    print(f"保存在 {OUTPUT_DIR}/ 目录")

if __name__ == "__main__":
    asyncio.run(generate_audio())
