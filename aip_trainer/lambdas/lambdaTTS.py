import random
import tempfile
from pathlib import Path

from aip_trainer import app_logger


def get_tts(text: str, language: str):
    from aip_trainer.models import models

    tmp_dir = Path(tempfile.gettempdir())
    if language == "en" or language == "de":
        try:
            model, _, speaker, sample_rate = models.silero_tts(
                language, output_folder=tmp_dir
            )
        except ValueError:
            model, _, sample_rate, _, _, speaker = models.silero_tts(
                language, output_folder=tmp_dir
            )
    else:
        raise NotImplementedError(f"Not yet tested with {language} error...")
    app_logger.info(f"model speaker #0: {speaker} ...")

    with tempfile.NamedTemporaryFile(prefix="audio_", suffix=".wav", delete=False) as tmp_audio_file:
        app_logger.info(f"tmp_audio_file output: {tmp_audio_file.name} ...")
        audio_paths = model.save_wav(text=text, speaker=speaker, sample_rate=sample_rate, audio_path=str(tmp_audio_file.name))
        app_logger.info(f"audio_paths output: {audio_paths} ...")
        return audio_paths


"""
Help on method save_wav:

save_wav(text=None, ssml_text=None, speaker: str = 'xenia', audio_path: str = '', sample_rate: int = 48000, put_accent=True, put_yo=True) method of <torch_package_0>.multi_acc_v3_package.TTSModelMultiAcc_v3 instance


    tmp_dir = tempfile.gettempdir()
    if language == "de":
        model, decoder, _ = silero_stt(
            language="de", version="v4", jit_model="jit_large", output_folder=tmp_dir
        )
    elif language == "en":
        model, decoder, _ = silero_stt(language="en", output_folder=tmp_dir)
    else:
        raise NotImplementedError(
            "currenty works only for 'de' and 'en' languages, not for '{}'.".format(
                language
            )
        )

"""
