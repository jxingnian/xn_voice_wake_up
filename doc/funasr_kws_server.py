"""
FunASR 语音唤醒服务器
功能：基于 fsmn-kws 模型的流式关键词检测

部署步骤：
1. pip install funasr torch torchaudio fastapi uvicorn websockets
2. python funasr_kws_server.py

API:
- GET /                     - 健康检查
- POST /set_keywords        - 设置唤醒词
- WebSocket /ws/{user_id}   - 流式音频检测
"""

import asyncio
import numpy as np
import logging
from typing import Dict, Any, List
from fastapi import FastAPI, WebSocket, Form, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

app = FastAPI(title="FunASR 语音唤醒服务器")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# 全局模型
kws_model = None
user_configs: Dict[str, Dict[str, Any]] = {}

def get_user_config(user_id: str) -> Dict[str, Any]:
    if user_id not in user_configs:
        user_configs[user_id] = {
            "keywords": ["你好星年"],
        }
    return user_configs[user_id]

@app.on_event("startup")
async def load_models():
    global kws_model
    logger.info("加载 FunASR KWS 模型...")
    
    try:
        from funasr import AutoModel
        
        # 加载流式关键词检测模型
        # fsmn-kws: 0.7M 参数，支持流式，中文
        kws_model = AutoModel(model="fsmn-kws")
        logger.info("✅ FunASR KWS 模型加载完成")
        
    except Exception as e:
        logger.error(f"模型加载失败: {e}")
        logger.error("请确保已安装: pip install funasr torch torchaudio")
        raise
    
    logger.info("🎉 服务就绪！")

def audio_bytes_to_numpy(audio_bytes: bytes) -> np.ndarray:
    """将 PCM 字节转换为 numpy 数组 (16bit -> float32)"""
    audio_int16 = np.frombuffer(audio_bytes, dtype=np.int16)
    return audio_int16.astype(np.float32) / 32768.0

def detect_keywords(audio_np: np.ndarray, keywords: List[str]) -> dict:
    """检测音频中的关键词"""
    global kws_model
    
    if kws_model is None:
        return {"detected": False, "keyword": None, "text": ""}
    
    try:
        # FunASR KWS 推理
        # hotwords 参数用于指定要检测的关键词
        result = kws_model.generate(
            input=audio_np,
            hotwords=" ".join(keywords),
        )
        
        if result and len(result) > 0:
            text = result[0].get("text", "")
            
            # 检查是否检测到任何关键词
            for kw in keywords:
                if kw in text:
                    return {"detected": True, "keyword": kw, "text": text}
            
            return {"detected": False, "keyword": None, "text": text}
        
        return {"detected": False, "keyword": None, "text": ""}
        
    except Exception as e:
        logger.error(f"关键词检测错误: {e}")
        return {"detected": False, "keyword": None, "text": "", "error": str(e)}

@app.get("/")
async def root():
    return {
        "status": "ok", 
        "model": "fsmn-kws",
        "description": "FunASR 流式关键词检测服务"
    }

@app.post("/set_keywords")
async def set_keywords(user_id: str = Form(None), keywords: str = Form(None), request: Request = None):
    """设置唤醒词列表"""
    if user_id is None or keywords is None:
        try:
            body = await request.json()
            user_id = body.get("user_id")
            keywords = body.get("keywords")
        except:
            raise HTTPException(status_code=400, detail="Missing user_id or keywords")
    
    if not user_id or not keywords:
        raise HTTPException(status_code=400, detail="Missing user_id or keywords")
    
    # 支持逗号分隔的字符串或列表
    if isinstance(keywords, str):
        keyword_list = [k.strip() for k in keywords.split(",") if k.strip()]
    else:
        keyword_list = keywords
    
    config = get_user_config(user_id)
    config["keywords"] = keyword_list
    logger.info(f"用户 {user_id} 设置唤醒词: {keyword_list}")
    return {"status": "ok", "keywords": keyword_list}

@app.get("/get_keywords/{user_id}")
async def get_keywords(user_id: str):
    """获取用户的唤醒词列表"""
    config = get_user_config(user_id)
    return {"status": "ok", "keywords": config["keywords"]}

@app.websocket("/ws/{user_id}")
async def websocket_endpoint(websocket: WebSocket, user_id: str):
    """WebSocket 流式关键词检测"""
    await websocket.accept()
    config = get_user_config(user_id)
    keywords = config["keywords"]
    logger.info(f"用户 {user_id} 连接, 唤醒词: {keywords}")
    
    try:
        while True:
            # 接收音频数据 (PCM 16bit 16kHz)
            audio_bytes = await websocket.receive_bytes()
            audio_np = audio_bytes_to_numpy(audio_bytes)
            
            duration = len(audio_np) / 16000
            logger.debug(f"收到音频: {duration:.2f}s")
            
            # 关键词检测
            result = detect_keywords(audio_np, keywords)
            
            # 发送结果
            await websocket.send_json(result)
            
            if result["detected"]:
                logger.info(f"🎤 用户 {user_id} 唤醒: {result['keyword']}")
                
    except Exception as e:
        if "1000" not in str(e) and "1001" not in str(e):  # 正常关闭
            logger.error(f"WebSocket 错误: {e}")
    finally:
        logger.info(f"用户 {user_id} 断开")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
