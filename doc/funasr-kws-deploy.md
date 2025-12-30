# FunASR 语音唤醒服务部署文档

## 一、服务器要求

- Ubuntu 20.04+
- Python 3.8+
- CUDA 11.x（可选，GPU 加速）
- 内存 4GB+

## 二、安装依赖

```bash
# 创建虚拟环境
python3 -m venv venv
source venv/bin/activate

# 安装依赖
pip install funasr torch torchaudio fastapi uvicorn websockets

# GPU 版本（可选）
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu118
```

## 三、上传服务端代码

将 `funasr_kws_server.py` 上传到服务器：

```bash
scp doc/funasr_kws_server.py root@117.50.176.26:/root/voice-wake-server/
```

## 四、启动服务

```bash
cd /root/voice-wake-server
source venv/bin/activate
python funasr_kws_server.py
```

## 五、配置开机自启动

```bash
sudo tee /etc/systemd/system/voice-wake.service << 'EOF'
[Unit]
Description=FunASR Voice Wake Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/root/voice-wake-server
ExecStart=/root/voice-wake-server/venv/bin/python funasr_kws_server.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable voice-wake
sudo systemctl start voice-wake
```

## 六、API 接口

### 1. 健康检查

```bash
curl http://117.50.176.26:8000/
```

### 2. 设置唤醒词

```bash
curl -X POST http://117.50.176.26:8000/set_keywords \
  -H "Content-Type: application/json" \
  -d '{"user_id": "esp32_device", "keywords": "你好星年,小星星"}'
```

### 3. WebSocket 流式检测

连接地址：`ws://117.50.176.26:8000/ws/{user_id}`

发送：PCM 音频数据（16bit, 16kHz, 单声道）

接收：JSON 格式
```json
{
  "detected": true,
  "keyword": "你好星年",
  "text": "你好星年"
}
```

## 七、ESP32 端使用

ESP32 通过 WebSocket 发送 VAD 检测到的音频片段，服务器返回是否检测到唤醒词。
