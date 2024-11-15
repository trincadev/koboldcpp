import torch.nn as nn
from silero import silero_stt
from silero.utils import Decoder


# second returned type here is the custom class src.silero.utils.Decoder from snakers4/silero-models
def getASRModel(language: str) -> tuple[nn.Module, Decoder]:
    if language == 'de':
        model, decoder, _ = silero_stt(language='de', version="v4", jit_model="jit_large")
    elif language == 'en':
        model, decoder, _ = silero_stt(language='en')
    else:
        raise NotImplementedError("currenty works only for 'de' and 'en' languages, not for '{}'.".format(language))

    return model, decoder
