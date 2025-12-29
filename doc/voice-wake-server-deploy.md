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
pip install openai-whisper faster-whisper speechbrain langid && \
pip install fastapi uvicorn websockets python-multipart numpy scipy librosa soundfile
```

---

## 第五步：创建服务端代码

```bash
cat > /root/voice-wake-server/server.py << 'EOF'
"""
语音唤醒服务器
功能：Whisper 语音识别 + SpeechBrain 声纹识别 + 唤醒词检测
"""

import torch
import numpy as np
import logging
from typing import Optional, Dict, Any
from fastapi import FastAPI, WebSocket, UploadFile, File, Form, HTTPException
from fastapi.middleware.cors import CORSMiddleware

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

app = FastAPI(title="语音唤醒服务器")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

models = {"whisper": None, "speaker": None}
user_configs: Dict[str, Dict[str, Any]] = {}

def get_user_config(user_id: str) -> Dict[str, Any]:
    if user_id not in user_configs:
        user_configs[user_id] = {"wake_word": "你好星年", "voice_embedding": None, "voice_enabled": False}
    return user_configs[user_id]

@app.on_event("startup")
async def load_models():
    global models
    device = "cuda" if torch.cuda.is_available() else "cpu"
    logger.info(f"使用设备: {device}")
    
    # Faster-Whisper
    logger.info("加载 Faster-Whisper...")
    try:
        from faster_whisper import WhisperModel
        models["whisper"] = WhisperModel("base", device=device, compute_type="float16")
        logger.info("✅ Faster-Whisper 加载完成")
    except Exception as e:
        logger.warning(f"Faster-Whisper 失败，使用原版: {e}")
        import whisper
        models["whisper"] = whisper.load_model("base", device=device)
    
    # 声纹识别
    logger.info("加载声纹识别模型...")
    try:
        from speechbrain.inference.speaker import SpeakerRecognition
        models["speaker"] = SpeakerRecognition.from_hparams(
            source="speechbrain/spkrec-ecapa-voxceleb",
            savedir="/root/voice-wake-server/models/speaker",
            run_opts={"device": device}
        )
        logger.info("✅ 声纹识别加载完成")
    except Exception as e:
        logger.error(f"声纹识别加载失败: {e}")
    
    logger.info("🎉 服务就绪！")

def audio_bytes_to_numpy(audio_bytes: bytes) -> np.ndarray:
    return np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0

def transcribe_audio(audio_np: np.ndarray) -> dict:
    if models["whisper"] is None:
        return {"text": "", "error": "模型未加载"}
    try:
        if hasattr(models["whisper"], 'transcribe'):
            segments, info = models["whisper"].transcribe(audio_np, language="zh", beam_size=5)
            text = "".join([seg.text for seg in segments])
            return {"text": text.strip()}
        else:
            result = models["whisper"].transcribe(audio_np, language="zh", fp16=True)
            return {"text": result["text"].strip()}
    except Exception as e:
        logger.error(f"语音识别错误: {e}")
        return {"text": "", "error": str(e)}

def get_speaker_embedding(audio_np: np.ndarray) -> Optional[torch.Tensor]:
    if models["speaker"] is None:
        return None
    try:
        audio_tensor = torch.from_numpy(audio_np).unsqueeze(0)
        return models["speaker"].encode_batch(audio_tensor)
    except Exception as e:
        logger.error(f"声纹提取错误: {e}")
        return None

def verify_speaker(emb1, emb2, threshold=0.5) -> dict:
    score = torch.nn.functional.cosine_similarity(emb1, emb2).item()
    return {"is_same": score > threshold, "score": score}

@app.get("/")
async def root():
    return {"status": "ok", "loaded_models": [k for k, v in models.items() if v]}

@app.post("/set_wake_word")
async def set_wake_word(user_id: str = Form(...), wake_word: str = Form(...)):
    config = get_user_config(user_id)
    config["wake_word"] = wake_word
    logger.info(f"用户 {user_id} 设置唤醒词: {wake_word}")
    return {"status": "ok", "wake_word": wake_word}

@app.post("/register_voice")
async def register_voice(user_id: str = Form(...), audio: UploadFile = File(...)):
    config = get_user_config(user_id)
    audio_np = audio_bytes_to_numpy(await audio.read())
    embedding = get_speaker_embedding(audio_np)
    if embedding is not None:
        config["voice_embedding"] = embedding
        config["voice_enabled"] = True
        logger.info(f"用户 {user_id} 注册声纹成功")
        return {"status": "ok", "message": "声纹注册成功"}
    raise HTTPException(status_code=500, detail="声纹提取失败")

@app.post("/recognize")
async def recognize(user_id: str = Form(...), audio: UploadFile = File(...)):
    config = get_user_config(user_id)
    audio_np = audio_bytes_to_numpy(await audio.read())
    
    asr_result = transcribe_audio(audio_np)
    text = asr_result["text"]
    wake_detected = config["wake_word"] in text
    
    result = {"text": text, "wake_detected": wake_detected, "wake_word": config["wake_word"],
              "speaker_verified": False, "speaker_score": 0.0}
    
    if wake_detected and config["voice_enabled"] and config["voice_embedding"] is not None:
        emb = get_speaker_embedding(audio_np)
        if emb is not None:
            verify = verify_speaker(config["voice_embedding"], emb)
            result["speaker_verified"] = verify["is_same"]
            result["speaker_score"] = verify["score"]
    return result

@app.websocket("/ws/{user_id}")
async def websocket_endpoint(websocket: WebSocket, user_id: str):
    await websocket.accept()
    config = get_user_config(user_id)
    logger.info(f"用户 {user_id} 连接")
    try:
        while True:
            audio_np = audio_bytes_to_numpy(await websocket.receive_bytes())
            text = transcribe_audio(audio_np)["text"]
            wake_detected = config["wake_word"] in text
            
            result = {"text": text, "wake_detected": wake_detected, "speaker_verified": False, "speaker_score": 0.0}
            if wake_detected and config["voice_enabled"] and config["voice_embedding"] is not None:
                emb = get_speaker_embedding(audio_np)
                if emb:
                    verify = verify_speaker(config["voice_embedding"], emb)
                    result["speaker_verified"] = verify["is_same"]
                    result["speaker_score"] = verify["score"]
            
            await websocket.send_json(result)
            if wake_detected:
                logger.info(f"🎤 用户 {user_id} 唤醒: {text}")
    except Exception as e:
        logger.error(f"WebSocket 错误: {e}")
    finally:
        logger.info(f"用户 {user_id} 断开")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
EOF
```

---

## 第六步：启动服务

### 方式一：前台运行（测试用）

```bash
cd /root/voice-wake-server
python server.py
```

### 方式二：后台运行

```bash
cd /root/voice-wake-server
nohup python server.py > server.log 2>&1 &

# 查看日志
tail -f server.log

# 查看进程
ps aux | grep server.py

# 停止服务
pkill -f "python server.py"
```

### 方式三：systemd 开机自启动（推荐）

```bash
# 创建 systemd 服务文件
cat > /etc/systemd/system/voice-wake.service << 'EOF'
[Unit]
Description=Voice Wake Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/root/voice-wake-server
ExecStart=/usr/bin/python /root/voice-wake-server/server.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# 重新加载 systemd
systemctl daemon-reload

# 启动服务
systemctl start voice-wake

# 设置开机自启动
systemctl enable voice-wake

# 查看服务状态
systemctl status voice-wake

# 查看日志
journalctl -u voice-wake -f

# 停止服务
systemctl stop voice-wake

# 重启服务
systemctl restart voice-wake
```

---

## 第七步：测试 API

### 1. 健康检查

```
GET /
```

请求：
```bash
curl http://localhost:8000/
```

响应：
```json
{
    "status": "ok",
    "loaded_models": ["whisper", "speaker"]
}
```

---

### 2. 设置唤醒词

```
POST /set_wake_word
Content-Type: multipart/form-data
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| user_id | string | 是 | 用户ID |
| wake_word | string | 是 | 唤醒词 |

请求：
```bash
curl -X POST http://localhost:8000/set_wake_word \
  -F "user_id=user001" \
  -F "wake_word=你好星年"
```

响应：
```json
{
    "status": "ok",
    "wake_word": "你好星年"
}
```

---

### 3. 注册声纹

```
POST /register_voice
Content-Type: multipart/form-data
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| user_id | string | 是 | 用户ID |
| audio | file | 是 | 音频文件（16kHz, 16bit, mono, PCM/WAV） |

请求：
```bash
curl -X POST http://localhost:8000/register_voice \
  -F "user_id=user001" \
  -F "audio=@my_voice.wav"
```

响应：
```json
{
    "status": "ok",
    "message": "声纹注册成功"
}
```

---

### 4. 语音识别 + 唤醒词检测

```
POST /recognize
Content-Type: multipart/form-data
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| user_id | string | 是 | 用户ID |
| audio | file | 是 | 音频文件（16kHz, 16bit, mono, PCM/WAV） |

请求：
```bash
curl -X POST http://localhost:8000/recognize \
  -F "user_id=user001" \
  -F "audio=@test.wav"
```

响应：
```json
{
    "text": "你好星年打开灯",
    "wake_detected": true,
    "wake_word": "你好星年",
    "speaker_verified": true,
    "speaker_score": 0.85
}
```

| 响应字段 | 类型 | 说明 |
|------|------|------|
| text | string | 识别出的文字 |
| wake_detected | bool | 是否检测到唤醒词 |
| wake_word | string | 当前设置的唤醒词 |
| speaker_verified | bool | 声纹是否匹配（需先注册声纹） |
| speaker_score | float | 声纹相似度（0-1，越高越相似） |

---

### 5. WebSocket 实时语音识别

```
WebSocket /ws/{user_id}
```

Python 示例：
```python
import asyncio
import websockets

async def test_ws():
    uri = "ws://localhost:8000/ws/user001"
    async with websockets.connect(uri) as ws:
        # 发送音频数据（16kHz, 16bit, mono, PCM）
        with open("test.raw", "rb") as f:
            audio_data = f.read()
        await ws.send(audio_data)
        
        # 接收结果
        result = await ws.recv()
        print(result)

asyncio.run(test_ws())
```

响应：
```json
{
    "text": "你好星年",
    "wake_detected": true,
    "speaker_verified": false,
    "speaker_score": 0.0
}
```

---

## 音频格式要求

| 参数 | 值 |
|------|------|
| 采样率 | 16000 Hz |
| 位深度 | 16 bit |
| 声道 | 单声道 (mono) |
| 格式 | PCM 或 WAV |

---

## 公网访问

确保防火墙开放 8000 端口：

```bash
ufw allow 8000
```

访问地址：`http://117.50.176.26:8000/`
