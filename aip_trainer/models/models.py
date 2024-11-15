import os
from pathlib import Path
import tempfile
import torch.nn as nn
from silero.utils import Decoder

from aip_trainer import app_logger


def silero_stt(language='en',
               version='latest',
               jit_model='jit',
               output_folder: Path | str = None,
               **kwargs):
    """Modified Silero Speech-To-Text Model(s) function
    language (str): language of the model, now available are ['en', 'de', 'es']
    version:
    jit_model:
    output_folder: needed in case of docker build
    Returns a model, decoder object and a set of utils
    Please see https://github.com/snakers4/silero-models for usage examples
    """
    import torch
    from omegaconf import OmegaConf    
    from silero.utils import (init_jit_model,
                        read_audio,
                        read_batch,
                        split_into_batches,
                        prepare_model_input)

    output_folder = Path(output_folder) if output_folder is not None else Path(os.path.dirname(__file__)) / ".." / ".."
    models_list_file = output_folder / f'latest_silero_model_{language}.yml'
    if not os.path.exists(models_list_file):
        app_logger.info(f"model yml for '{language}' language, '{version}' version not found, download it in folder {output_folder}...")
        torch.hub.download_url_to_file(
            'https://raw.githubusercontent.com/snakers4/silero-models/master/models.yml',
            models_list_file,
            progress=True
        )
    app_logger.info(f"model yml for '{language}' language, '{version}' version in folder {output_folder}: OK!")
    assert os.path.exists(models_list_file)
    models = OmegaConf.load(models_list_file)
    available_languages = list(models.stt_models.keys())
    assert language in available_languages

    model, decoder = init_jit_model(model_url=models.stt_models.get(language).get(version).get(jit_model),
                                    **kwargs)
    utils = (read_batch,
             split_into_batches,
             read_audio,
             prepare_model_input)

    return model, decoder, utils


# second returned type here is the custom class src.silero.utils.Decoder from snakers4/silero-models
def getASRModel(language: str) -> tuple[nn.Module, Decoder]:
    tmp_dir = tempfile.gettempdir()
    if language == 'de':
        model, decoder, _ = silero_stt(language='de', version="v4", jit_model="jit_large", output_folder=tmp_dir)
    elif language == 'en':
        model, decoder, _ = silero_stt(language='en', output_folder=tmp_dir)
    else:
        raise NotImplementedError("currenty works only for 'de' and 'en' languages, not for '{}'.".format(language))

    return model, decoder
