import os
from pathlib import Path
import tempfile
import torch
import torch.nn as nn
from silero.utils import Decoder

from aip_trainer import app_logger


default_speaker_dict = {
    "de": {"speaker": "karlsson", "model_id": "v3_de", "sample_rate": 24000},
    "en": {"speaker": "en_0", "model_id": "v3_en", "sample_rate": 24000},
}


def silero_tts(
    language="en", version="latest", output_folder: Path | str = None, **kwargs
):
    """Silero Text-To-Speech Models
    language (str): language of the model, now available are ['ru', 'en', 'de', 'es', 'fr']
    Returns a model and a set of utils
    Please see https://github.com/snakers4/silero-models for usage examples
    """
    output_folder = Path(output_folder)
    current_model_lang = default_speaker_dict[language]
    app_logger.info(f"model speaker current_model_lang: {current_model_lang} ...")
    if language in default_speaker_dict:
        model_id = current_model_lang["model_id"]

    models = get_models(language, output_folder, version, model_type="tts_models")
    available_languages = list(models.tts_models.keys())
    assert (
        language in available_languages
    ), f"Language not in the supported list {available_languages}"

    tts_models_lang = models.tts_models[language]
    model_conf = tts_models_lang[model_id]
    model_conf_latest = model_conf[version]
    app_logger.info(f"model_conf: {model_conf_latest} ...")
    if "_v2" in model_id or "_v3" in model_id or "v3_" in model_id or "v4_" in model_id:
        from torch import package

        model_url = model_conf_latest.package
        model_dir = output_folder / "model"
        os.makedirs(model_dir, exist_ok=True)
        model_path = output_folder / os.path.basename(model_url)
        if not os.path.isfile(model_path):
            torch.hub.download_url_to_file(model_url, model_path, progress=True)
        imp = package.PackageImporter(model_path)
        model = imp.load_pickle("tts_models", "model")
        app_logger.info(
            f"current model_conf_latest.sample_rate:{model_conf_latest.sample_rate} ..."
        )
        sample_rate = current_model_lang["sample_rate"]
        return (
            model,
            model_conf_latest.example,
            current_model_lang["speaker"],
            sample_rate,
        )
    else:
        from silero.tts_utils import apply_tts, init_jit_model as init_jit_model_tts

        model = init_jit_model_tts(model_conf_latest.jit)
        symbols = model_conf_latest.tokenset
        example_text = model_conf_latest.example
        sample_rate = model_conf_latest.sample_rate
        return model, symbols, sample_rate, example_text, apply_tts, model_id


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
    from silero.utils import (
        read_audio,
        read_batch,
        split_into_batches,
        prepare_model_input,
    )

    model, decoder = get_latest_model(
        language,
        output_folder,
        version,
        model_type="stt_models",
        jit_model=jit_model,
        **kwargs,
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
        app_logger.info(f"downloading model_path: '{model_path}' ...")
        torch.hub.download_url_to_file(model_url, model_path, progress=True)
    app_logger.info(f"model_path {model_path} downloaded!")
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


def get_models(language, output_folder, version, model_type):
    from omegaconf import OmegaConf

    output_folder = (
        Path(output_folder)
        if output_folder is not None
        else Path(os.path.dirname(__file__)) / ".." / ".."
    )
    models_list_file = output_folder / f"latest_silero_model_{language}.yml"
    if not os.path.exists(models_list_file):
        app_logger.info(
            f"model {model_type} yml for '{language}' language, '{version}' version not found, download it in folder {output_folder}..."
        )
        torch.hub.download_url_to_file(
            "https://raw.githubusercontent.com/snakers4/silero-models/master/models.yml",
            models_list_file,
            progress=False,
        )
    assert os.path.exists(models_list_file)
    return OmegaConf.load(models_list_file)


def get_latest_model(language, output_folder, version, model_type, jit_model, **kwargs):
    models = get_models(language, output_folder, version, model_type)
    available_languages = list(models[model_type].keys())
    assert language in available_languages

    model, decoder = init_jit_model(
        model_url=models[model_type].get(language).get(version).get(jit_model),
        output_folder=output_folder,
        **kwargs,
    )
    return model, decoder
