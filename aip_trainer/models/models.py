from typing import Any

import torch
import torch.nn as nn


# second returned type here is the custom class src.silero.utils.Decoder from snakers4/silero-models
def getASRModel(language: str) -> tuple[nn.Module, Any]:


    if language == 'de':

        model, decoder, utils = torch.hub.load(repo_or_dir='snakers4/silero-models',
                                               model='silero_stt',
                                               language='de',
                                               device=torch.device('cpu'))

    elif language == 'en':
        model, decoder, utils = torch.hub.load(repo_or_dir='snakers4/silero-models',
                                               model='silero_stt',
                                               language='en',
                                               device=torch.device('cpu'))
    else:
        raise NotImplementedError("currenty works only for 'de' and 'en' languages, not for '{}'.".format(language))

    return model, decoder
