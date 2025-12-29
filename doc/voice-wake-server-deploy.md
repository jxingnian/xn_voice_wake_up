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

## 第四步：安装依赖

```bash
pip install --upgrade pip && \
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu121 && \
pip install openai-whisper faster-whisper speechbrain && \
pip install huggingface_hub==0.23.0 && \
pip install fastapi uvicorn websockets python-multipart numpy soundfile
```

注意：必须安装 `huggingface_hub==0.23.0`，新版本与 speechbrain 不兼容。

---

## 第五步：上传服务端代码

服务端代码文件：`doc/server.py`

上传到服务器：
```bash
scp -P 23 doc/server.py root@117.50.176.26:/root/voice-wake-server/
```

或者在服务器上直接下载/复制 `server.py` 文件内容到 `/root/voice-wake-server/server.py`

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
