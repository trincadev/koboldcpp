import random

import structlog
from fastapi import APIRouter


custom_structlog_logger = structlog.stdlib.get_logger(__name__)
router = APIRouter()


@router.get("/health")
def health():
    import torch
    import torchaudio
    custom_structlog_logger.info(f"Still alive, torch version:{torch.__version__}, torchaudio:{torchaudio.__version__} ...")
    return "Still alive!"
