import os
from pathlib import Path
import tempfile
import torch
import torch.nn as nn
from silero.utils import Decoder

from aip_trainer import app_logger


def silero_tts(language='en',
               speaker='kseniya_16khz',
               **kwargs):
    """ Silero Text-To-Speech Models
    language (str): language of the model, now available are ['ru', 'en', 'de', 'es', 'fr']
    Returns a model and a set of utils
    Please see https://github.com/snakers4/silero-models for usage examples
    """
    from omegaconf import OmegaConf
    from silero.tts_utils import apply_tts
    from silero.tts_utils import init_jit_model as init_jit_model_tts

    models_list_file = os.path.join(os.path.dirname(__file__), "..", "..", "models.yml")
    if not os.path.exists(models_list_file):
        models_list_file = 'latest_silero_models.yml'
    if not os.path.exists(models_list_file):
        torch.hub.download_url_to_file('https://raw.githubusercontent.com/snakers4/silero-models/master/models.yml',
                                    'latest_silero_models.yml',
                                    progress=False)
    assert os.path.exists(models_list_file)
    models = OmegaConf.load(models_list_file)
    available_languages = list(models.tts_models.keys())
    assert language in available_languages, f'Language not in the supported list {available_languages}'
    available_speakers = []
    speaker_language = {}
    for lang in available_languages:
        speakers = list(models.tts_models.get(lang).keys())
        available_speakers.extend(speakers)
        for _ in speakers:
            speaker_language[_] = lang
    assert speaker in available_speakers, f'Speaker not in the supported list {available_speakers}'
    assert language == speaker_language[speaker], f"Incorrect language '{language}' for this speaker, please specify '{speaker_language[speaker]}'"

    model_conf = models.tts_models[language][speaker].latest
    if '_v2' in speaker or '_v3' in speaker or 'v3_' in speaker or 'v4_' in speaker:
        from torch import package
        model_url = model_conf.package
        model_dir = os.path.join(os.path.dirname(__file__), "model")
        os.makedirs(model_dir, exist_ok=True)
        model_path = os.path.join(model_dir, os.path.basename(model_url))
        if not os.path.isfile(model_path):
            torch.hub.download_url_to_file(model_url,
                                           model_path,
                                           progress=True)
        imp = package.PackageImporter(model_path)
        model = imp.load_pickle("tts_models", "model")
        if speaker == 'multi_v2':
            avail_speakers = model_conf.speakers
            return model, avail_speakers
        else:
            example_text = model_conf.example
            return model, example_text
    else:
        model = init_jit_model_tts(model_conf.jit)
        symbols = model_conf.tokenset
        example_text = model_conf.example
        sample_rate = model_conf.sample_rate
        return model, symbols, sample_rate, example_text, apply_tts


def silero_stt(
    language="en",
    version="latest",
    jit_model="jit",
    output_folder: Path | str = None,
    **kwargs,
    ):
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
    from silero.utils import (
        read_audio,
        read_batch,
        split_into_batches,
        prepare_model_input,
    )

    output_folder = (
        Path(output_folder)
        if output_folder is not None
        else Path(os.path.dirname(__file__)) / ".." / ".."
    )
    models_list_file = output_folder / f"latest_silero_model_{language}.yml"
    if not os.path.exists(models_list_file):
        app_logger.info(
            f"model yml for '{language}' language, '{version}' version not found, download it in folder {output_folder}..."
        )
        torch.hub.download_url_to_file(
            "https://raw.githubusercontent.com/snakers4/silero-models/master/models.yml",
            models_list_file,
            progress=True,
        )
    app_logger.info(
        f"model yml for '{language}' language, '{version}' version in folder {output_folder}: OK!"
    )
    assert os.path.exists(models_list_file)
    models = OmegaConf.load(models_list_file)
    available_languages = list(models.stt_models.keys())
    assert language in available_languages

    model, decoder = init_jit_model(
        model_url=models.stt_models.get(language).get(version).get(jit_model), output_folder=output_folder, **kwargs
    )
    utils = (read_batch, split_into_batches, read_audio, prepare_model_input)

    return model, decoder, utils


def init_jit_model(
    model_url: str,
    device: torch.device = torch.device("cpu"),
    output_folder: Path | str = None,
    ):
    torch.set_grad_enabled(False)

    app_logger.info(
        f"model output_folder exists? '{output_folder is None}' => '{output_folder}' ..."
    )
    model_dir = (
        Path(output_folder)
        if output_folder is not None
        else Path(os.path.dirname(__file__)) / "model"
    )
    os.makedirs(model_dir, exist_ok=True)
    model_path = model_dir / os.path.basename(model_url)
    app_logger.info(
        f"model_path exists? '{os.path.isfile(model_path)}' => '{model_path}' ..."
    )

    if not os.path.isfile(model_path):
        app_logger.info(
            f"downloading model_path: '{model_path}' ..."
        )
        torch.hub.download_url_to_file(model_url, model_path, progress=True)
    app_logger.info(
        f"model_path {model_path} downloaded!"
    )
    model = torch.jit.load(model_path, map_location=device)
    model.eval()
    return model, Decoder(model.labels)


# second returned type here is the custom class src.silero.utils.Decoder from snakers4/silero-models
def getASRModel(language: str) -> tuple[nn.Module, Decoder]:
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

    return model, decoder
