# 语音唤醒服务器部署指南

## 概述

本文档介绍如何在 GPU 云服务器上部署全功能语音处理服务，包含：
- 语音识别（ASR）- 语音转文字
- 声纹识别 - 识别/验证说话人
- 语音增强 - 降噪、去混响
- 情感识别 - 识别语音情绪
- 语言识别 - 识别说的什么语言
- VAD - 检测是否有人说话
- TTS - 文字转语音

---

## 服务器信息

```
IP: 117.50.176.26
端口: 23
用户: root
密码: Wsq7Se4vLmi96HT

连接命令: ssh -p 23 root@117.50.176.26
```

---

## 第一步：连接服务器

### 方式一：命令行
```bash
ssh -p 23 root@117.50.176.26
# 输入密码: Wsq7Se4vLmi96HT
```

### 方式二：VS Code Remote-SSH
1. 安装 Remote-SSH 插件
2. 配置 `~/.ssh/config`:
```
Host gpu-server
    HostName 117.50.176.26
    User root
    Port 23
```
3. 连接 gpu-server

---

## 第二步：检查环境

```bash
# 检查 GPU
nvidia-smi

# 检查 Python
python --version

# 检查 CUDA
nvcc --version
```

---

## 第三步：创建项目目录

```bash
mkdir -p /root/voice-wake-server
cd /root/voice-wake-server
```

---

## 第四步：安装所有依赖

```bash
# 升级 pip
pip install --upgrade pip

# 安装 PyTorch（CUDA 12.x 版本）
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu121

# ============ 核心模块 ============

# Whisper - 语音识别（中文效果最好）
pip install openai-whisper

# Faster-Whisper - 加速版语音识别（推荐，速度快 2-4 倍）
pip install faster-whisper

# SpeechBrain - 声纹识别、语音增强、情感识别等
pip install speechbrain

# ============ 辅助模块 ============

# Silero VAD - 语音活动检测
pip install silero-vad

# TTS - 文字转语音
pip install TTS

# 语言识别
pip install langid

# ============ Web 服务 ============

# FastAPI Web 框架
pip install fastapi uvicorn websockets python-multipart

# 其他工具
pip install numpy scipy librosa soundfile
```

### 一键安装命令

```bash
pip install --upgrade pip && \
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu121 && \
pip install openai-whisper faster-whisper speechbrain silero-vad TTS langid && \
pip install fastapi uvicorn websockets python-multipart numpy scipy librosa soundfile
```

---

## 第五步：创建服务端代码

```bash
cat > /root/voice-wake-server/server.py << 'ENDOFFILE'
"""
全功能语音处理服务器
功能：
1. 语音识别（Whisper/Faster-Whisper）
2. 声纹识别（SpeechBrain）
3. 语音增强/降噪（SpeechBrain）
4. 情感识别（SpeechBrain）
5. 语言识别（langid）
6. VAD 语音活动检测（Silero）
7. TTS 文字转语音
"""

import torch
import numpy as np
import io
import json
import asyncio
import logging
import tempfile
import soundfile as sf
from pathlib import Path
from typing import Optional, Dict, Any
from fastapi import FastAPI, WebSocket, UploadFile, File, Form, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, StreamingResponse

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

app = FastAPI(title="全功能语音处理服务器", version="1.0.0")

# 跨域配置
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ============ 全局模型 ============
models = {
    "whisper": None,           # 语音识别
    "speaker": None,           # 声纹识别
    "enhancer": None,          # 语音增强
    "emotion": None,           # 情感识别
    "vad": None,               # VAD
    "tts": None,               # TTS
}

# 用户配置存储
user_configs: Dict[str, Dict[str, Any]] = {}

def get_user_config(user_id: str) -> Dict[str, Any]:
    if user_id not in user_configs:
        user_configs[user_id] = {
            "wake_word": "你好星年",
            "voice_embedding": None,
            "voice_enabled": False,
        }
    return user_configs[user_id]

# ============ 模型加载 ============

@app.on_event("startup")
async def load_models():
    """启动时加载所有模型"""
    global models
    device = "cuda" if torch.cuda.is_available() else "cpu"
    logger.info(f"使用设备: {device}")
    
    # 1. 加载 Faster-Whisper（语音识别）
    logger.info("正在加载 Faster-Whisper 模型...")
    try:
        from faster_whisper import WhisperModel
        models["whisper"] = WhisperModel("base", device=device, compute_type="float16")
        logger.info("✅ Faster-Whisper 加载完成")
    except Exception as e:
        logger.warning(f"Faster-Whisper 加载失败，使用原版 Whisper: {e}")
        import whisper
        models["whisper"] = whisper.load_model("base", device=device)
        logger.info("✅ Whisper 加载完成")
    
    # 2. 加载声纹识别模型
    logger.info("正在加载声纹识别模型...")
    try:
        from speechbrain.inference.speaker import SpeakerRecognition
        models["speaker"] = SpeakerRecognition.from_hparams(
            source="speechbrain/spkrec-ecapa-voxceleb",
            savedir="/root/voice-wake-server/models/speaker",
            run_opts={"device": device}
        )
        logger.info("✅ 声纹识别模型加载完成")
    except Exception as e:
        logger.error(f"声纹识别模型加载失败: {e}")
    
    # 3. 加载语音增强模型
    logger.info("正在加载语音增强模型...")
    try:
        from speechbrain.inference.enhancement import SpectralMaskEnhancement
        models["enhancer"] = SpectralMaskEnhancement.from_hparams(
            source="speechbrain/metricgan-plus-voicebank",
            savedir="/root/voice-wake-server/models/enhancer",
            run_opts={"device": device}
        )
        logger.info("✅ 语音增强模型加载完成")
    except Exception as e:
        logger.warning(f"语音增强模型加载失败: {e}")
    
    # 4. 加载情感识别模型
    logger.info("正在加载情感识别模型...")
    try:
        from speechbrain.inference.classifiers import EncoderClassifier
        models["emotion"] = EncoderClassifier.from_hparams(
            source="speechbrain/emotion-recognition-wav2vec2-IEMOCAP",
            savedir="/root/voice-wake-server/models/emotion",
            run_opts={"device": device}
        )
        logger.info("✅ 情感识别模型加载完成")
    except Exception as e:
        logger.warning(f"情感识别模型加载失败: {e}")
    
    # 5. 加载 VAD 模型
    logger.info("正在加载 VAD 模型...")
    try:
        vad_model, utils = torch.hub.load(
            repo_or_dir='snakers4/silero-vad',
            model='silero_vad',
            force_reload=False
        )
        models["vad"] = {"model": vad_model, "utils": utils}
        logger.info("✅ VAD 模型加载完成")
    except Exception as e:
        logger.warning(f"VAD 模型加载失败: {e}")
    
    # 6. 加载 TTS 模型
    logger.info("正在加载 TTS 模型...")
    try:
        from TTS.api import TTS
        models["tts"] = TTS(model_name="tts_models/zh-CN/baker/tacotron2-DDC-GST", progress_bar=False)
        if device == "cuda":
            models["tts"].to(device)
        logger.info("✅ TTS 模型加载完成")
    except Exception as e:
        logger.warning(f"TTS 模型加载失败: {e}")
    
    logger.info("🎉 所有模型加载完成，服务就绪！")

# ============ 工具函数 ============

def audio_bytes_to_numpy(audio_bytes: bytes, sample_rate: int = 16000) -> np.ndarray:
    """将音频字节转换为 numpy 数组"""
    audio_np = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0
    return audio_np

def save_temp_audio(audio_np: np.ndarray, sample_rate: int = 16000) -> str:
    """保存临时音频文件"""
    temp_file = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    sf.write(temp_file.name, audio_np, sample_rate)
    return temp_file.name

# ============ 核心功能 ============

def transcribe_audio(audio_np: np.ndarray) -> dict:
    """语音识别"""
    if models["whisper"] is None:
        return {"text": "", "error": "模型未加载"}
    
    try:
        # 检查是否是 faster-whisper
        if hasattr(models["whisper"], 'transcribe'):
            # faster-whisper
            segments, info = models["whisper"].transcribe(
                audio_np,
                language="zh",
                beam_size=5
            )
            text = "".join([seg.text for seg in segments])
            return {"text": text.strip(), "language": info.language}
        else:
            # 原版 whisper
            result = models["whisper"].transcribe(audio_np, language="zh", fp16=True)
            return {"text": result["text"].strip(), "language": "zh"}
    except Exception as e:
        logger.error(f"语音识别错误: {e}")
        return {"text": "", "error": str(e)}

def get_speaker_embedding(audio_np: np.ndarray) -> Optional[torch.Tensor]:
    """获取声纹特征"""
    if models["speaker"] is None:
        return None
    try:
        audio_tensor = torch.from_numpy(audio_np).unsqueeze(0)
        embedding = models["speaker"].encode_batch(audio_tensor)
        return embedding
    except Exception as e:
        logger.error(f"声纹提取错误: {e}")
        return None

def verify_speaker(emb1: torch.Tensor, emb2: torch.Tensor, threshold: float = 0.5) -> dict:
    """声纹验证"""
    score = torch.nn.functional.cosine_similarity(emb1, emb2).item()
    return {"is_same": score > threshold, "score": score}

def enhance_audio(audio_np: np.ndarray) -> np.ndarray:
    """语音增强/降噪"""
    if models["enhancer"] is None:
        return audio_np
    try:
        audio_tensor = torch.from_numpy(audio_np).unsqueeze(0)
        enhanced = models["enhancer"].enhance_batch(audio_tensor)
        return enhanced.squeeze().numpy()
    except Exception as e:
        logger.error(f"语音增强错误: {e}")
        return audio_np

def recognize_emotion(audio_np: np.ndarray) -> dict:
    """情感识别"""
    if models["emotion"] is None:
        return {"emotion": "unknown", "score": 0}
    try:
        temp_file = save_temp_audio(audio_np)
        out_prob, score, index, label = models["emotion"].classify_file(temp_file)
        Path(temp_file).unlink()  # 删除临时文件
        return {"emotion": label[0], "score": score.item()}
    except Exception as e:
        logger.error(f"情感识别错误: {e}")
        return {"emotion": "unknown", "score": 0}

def detect_language(text: str) -> dict:
    """语言识别"""
    try:
        import langid
        lang, confidence = langid.classify(text)
        return {"language": lang, "confidence": confidence}
    except Exception as e:
        return {"language": "unknown", "confidence": 0}

def detect_vad(audio_np: np.ndarray, sample_rate: int = 16000) -> dict:
    """VAD 检测"""
    if models["vad"] is None:
        return {"has_speech": True, "segments": []}
    try:
        vad_model = models["vad"]["model"]
        get_speech_timestamps = models["vad"]["utils"][0]
        
        audio_tensor = torch.from_numpy(audio_np)
        speech_timestamps = get_speech_timestamps(audio_tensor, vad_model, sampling_rate=sample_rate)
        
        has_speech = len(speech_timestamps) > 0
        segments = [{"start": ts["start"] / sample_rate, "end": ts["end"] / sample_rate} for ts in speech_timestamps]
        
        return {"has_speech": has_speech, "segments": segments}
    except Exception as e:
        logger.error(f"VAD 检测错误: {e}")
        return {"has_speech": True, "segments": []}

def text_to_speech(text: str, output_path: str = None) -> Optional[str]:
    """文字转语音"""
    if models["tts"] is None:
        return None
    try:
        if output_path is None:
            output_path = tempfile.NamedTemporaryFile(suffix=".wav", delete=False).name
        models["tts"].tts_to_file(text=text, file_path=output_path)
        return output_path
    except Exception as e:
        logger.error(f"TTS 错误: {e}")
        return None

# ============ HTTP API ============

@app.get("/")
async def root():
    """健康检查"""
    loaded_models = [k for k, v in models.items() if v is not None]
    return {
        "status": "ok",
        "message": "语音处理服务器运行中",
        "loaded_models": loaded_models
    }

@app.post("/set_wake_word")
async def set_wake_word(user_id: str = Form(...), wake_word: str = Form(...)):
    """设置唤醒词"""
    config = get_user_config(user_id)
    config["wake_word"] = wake_word
    logger.info(f"用户 {user_id} 设置唤醒词: {wake_word}")
    return {"status": "ok", "wake_word": wake_word}

@app.post("/register_voice")
async def register_voice(user_id: str = Form(...), audio: UploadFile = File(...)):
    """注册声纹"""
    config = get_user_config(user_id)
    audio_bytes = await audio.read()
    audio_np = audio_bytes_to_numpy(audio_bytes)
    
    embedding = get_speaker_embedding(audio_np)
    if embedding is not None:
        config["voice_embedding"] = embedding
        config["voice_enabled"] = True
        logger.info(f"用户 {user_id} 注册声纹成功")
        return {"status": "ok", "message": "声纹注册成功"}
    else:
        raise HTTPException(status_code=500, detail="声纹提取失败")

@app.post("/recognize")
async def recognize(
    user_id: str = Form(...),
    audio: UploadFile = File(...),
    enhance: bool = Form(False),
    check_emotion: bool = Form(False)
):
    """完整语音识别（HTTP 方式）"""
    config = get_user_config(user_id)
    audio_bytes = await audio.read()
    audio_np = audio_bytes_to_numpy(audio_bytes)
    
    result = {}
    
    # VAD 检测
    vad_result = detect_vad(audio_np)
    result["vad"] = vad_result
    
    if not vad_result["has_speech"]:
        result["text"] = ""
        result["wake_detected"] = False
        return result
    
    # 语音增强（可选）
    if enhance:
        audio_np = enhance_audio(audio_np)
        result["enhanced"] = True
    
    # 语音识别
    asr_result = transcribe_audio(audio_np)
    result["text"] = asr_result["text"]
    result["asr_language"] = asr_result.get("language", "zh")
    
    # 检查唤醒词
    result["wake_detected"] = config["wake_word"] in result["text"]
    result["wake_word"] = config["wake_word"]
    
    # 声纹验证
    result["speaker_verified"] = False
    result["speaker_score"] = 0.0
    if result["wake_detected"] and config["voice_enabled"] and config["voice_embedding"] is not None:
        current_embedding = get_speaker_embedding(audio_np)
        if current_embedding is not None:
            verify_result = verify_speaker(config["voice_embedding"], current_embedding)
            result["speaker_verified"] = verify_result["is_same"]
            result["speaker_score"] = verify_result["score"]
    
    # 情感识别（可选）
    if check_emotion:
        emotion_result = recognize_emotion(audio_np)
        result["emotion"] = emotion_result
    
    # 语言识别
    if result["text"]:
        lang_result = detect_language(result["text"])
        result["text_language"] = lang_result
    
    return result

@app.post("/enhance")
async def enhance(audio: UploadFile = File(...)):
    """语音增强/降噪"""
    audio_bytes = await audio.read()
    audio_np = audio_bytes_to_numpy(audio_bytes)
    
    enhanced_np = enhance_audio(audio_np)
    
    # 返回增强后的音频
    output_path = save_temp_audio(enhanced_np)
    return FileResponse(output_path, media_type="audio/wav", filename="enhanced.wav")

@app.post("/tts")
async def tts(text: str = Form(...)):
    """文字转语音"""
    output_path = text_to_speech(text)
    if output_path:
        return FileResponse(output_path, media_type="audio/wav", filename="tts_output.wav")
    else:
        raise HTTPException(status_code=500, detail="TTS 生成失败")

@app.post("/emotion")
async def emotion(audio: UploadFile = File(...)):
    """情感识别"""
    audio_bytes = await audio.read()
    audio_np = audio_bytes_to_numpy(audio_bytes)
    result = recognize_emotion(audio_np)
    return result

@app.post("/vad")
async def vad(audio: UploadFile = File(...)):
    """VAD 检测"""
    audio_bytes = await audio.read()
    audio_np = audio_bytes_to_numpy(audio_bytes)
    result = detect_vad(audio_np)
    return result

# ============ WebSocket API ============

@app.websocket("/ws/{user_id}")
async def websocket_endpoint(websocket: WebSocket, user_id: str):
    """WebSocket 实时语音识别"""
    await websocket.accept()
    config = get_user_config(user_id)
    logger.info(f"用户 {user_id} WebSocket 连接")
    
    try:
        while True:
            audio_bytes = await websocket.receive_bytes()
            audio_np = audio_bytes_to_numpy(audio_bytes)
            
            # VAD 检测
            vad_result = detect_vad(audio_np)
            if not vad_result["has_speech"]:
                await websocket.send_json({"wake_detected": False, "has_speech": False})
                continue
            
            # 语音识别
            asr_result = transcribe_audio(audio_np)
            text = asr_result["text"]
            
            # 检查唤醒词
            wake_detected = config["wake_word"] in text
            
            # 声纹验证
            speaker_verified = False
            speaker_score = 0.0
            if wake_detected and config["voice_enabled"] and config["voice_embedding"] is not None:
                current_embedding = get_speaker_embedding(audio_np)
                if current_embedding is not None:
                    verify_result = verify_speaker(config["voice_embedding"], current_embedding)
                    speaker_verified = verify_result["is_same"]
                    speaker_score = verify_result["score"]
            
            result = {
                "text": text,
                "wake_detected": wake_detected,
                "speaker_verified": speaker_verified,
                "speaker_score": speaker_score,
                "has_speech": True
            }
            await websocket.send_json(result)
            
            if wake_detected:
                logger.info(f"🎤 用户 {user_id} 唤醒: {text}, 声纹: {speaker_verified}")
                
    except Exception as e:
        logger.error(f"WebSocket 错误: {e}")
    finally:
        logger.info(f"用户 {user_id} WebSocket 断开")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
ENDOFFILE
```

---

## 第六步：启动服务

```bash
cd /root/voice-wake-server

# 前台运行（测试）
python server.py

# 后台运行
nohup python server.py > server.log 2>&1 &

# 查看日志
tail -f server.log
```

---

## 第七步：测试 API

### 健康检查
```bash
curl http://localhost:8000/
```

### 设置唤醒词
```bash
curl -X POST http://localhost:8000/set_wake_word \
  -F "user_id=user001" \
  -F "wake_word=你好星年"
```

### 注册声纹
```bash
curl -X POST http://localhost:8000/register_voice \
  -F "user_id=user001" \
  -F "audio=@my_voice.wav"
```

### 语音识别（完整功能）
```bash
curl -X POST http://localhost:8000/recognize \
  -F "user_id=user001" \
  -F "audio=@test.wav" \
  -F "enhance=true" \
  -F "check_emotion=true"
```

### 语音增强
```bash
curl -X POST http://localhost:8000/enhance \
  -F "audio=@noisy.wav" \
  --output enhanced.wav
```

### 文字转语音
```bash
curl -X POST http://localhost:8000/tts \
  -F "text=你好，我是语音助手" \
  --output output.wav
```

### 情感识别
```bash
curl -X POST http://localhost:8000/emotion \
  -F "audio=@test.wav"
```

### VAD 检测
```bash
curl -X POST http://localhost:8000/vad \
  -F "audio=@test.wav"
```

---

## API 文档

### HTTP 接口

| 接口 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/` | GET | - | 健康检查 |
| `/set_wake_word` | POST | user_id, wake_word | 设置唤醒词 |
| `/register_voice` | POST | user_id, audio | 注册声纹 |
| `/recognize` | POST | user_id, audio, enhance?, check_emotion? | 完整语音识别 |
| `/enhance` | POST | audio | 语音增强/降噪 |
| `/tts` | POST | text | 文字转语音 |
| `/emotion` | POST | audio | 情感识别 |
| `/vad` | POST | audio | VAD 检测 |

### WebSocket 接口

| 接口 | 说明 |
|------|------|
| `/ws/{user_id}` | 实时语音识别 |

---

## 功能说明

| 功能 | 模型 | 说明 |
|------|------|------|
| 语音识别 | Faster-Whisper base | 语音转文字，支持中文 |
| 声纹识别 | ECAPA-TDNN | 说话人识别/验证 |
| 语音增强 | MetricGAN+ | 降噪、去混响 |
| 情感识别 | Wav2Vec2 | 识别喜怒哀乐 |
| VAD | Silero VAD | 检测是否有人说话 |
| TTS | Tacotron2 | 中文文字转语音 |
| 语言识别 | langid | 识别文字语言 |

---

## 常见问题

### Q: CUDA out of memory
A: 减少同时加载的模型，或使用更小的模型

### Q: 模型下载慢
A: 使用国内镜像或提前下载模型文件

### Q: TTS 中文效果不好
A: 可以换用 edge-tts（微软 TTS）

---

## 下一步

1. 配置公网访问（端口映射）
2. 修改 ESP32 代码上传音频
3. 添加用户认证和数据库
