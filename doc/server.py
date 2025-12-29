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
