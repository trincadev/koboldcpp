import random
import tempfile
from pathlib import Path

from aip_trainer import app_logger


def get_tts(text: str, language: str):
    from aip_trainer.models import models

    if text is None or len(text) == 0:
        raise ValueError(f"cannot read an empty/None text: '{text}'...")
    if language is None or len(language) == 0:
        raise NotImplementedError(f"Not tested/supported with '{language}' language...")

    tmp_dir = Path(tempfile.gettempdir())
    try:
        model, _, speaker, sample_rate = models.silero_tts(
            language, output_folder=tmp_dir
        )
    except ValueError:
        model, _, sample_rate, _, _, speaker = models.silero_tts(
            language, output_folder=tmp_dir
        )
    app_logger.info(f"model speaker #0: {speaker} ...")

    with tempfile.NamedTemporaryFile(prefix="audio_", suffix=".wav", delete=False) as tmp_audio_file:
        app_logger.info(f"tmp_audio_file output: {tmp_audio_file.name} ...")
        audio_paths = model.save_wav(text=text, speaker=speaker, sample_rate=sample_rate, audio_path=str(tmp_audio_file.name))
        app_logger.info(f"audio_paths output: {audio_paths} ...")
        return audio_paths
