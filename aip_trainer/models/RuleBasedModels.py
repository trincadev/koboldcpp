import eng_to_ipa

from aip_trainer.models import ModelInterfaces
from aip_trainer import app_logger


class EpitranPhonemConverter(ModelInterfaces.ITextToPhonemModel):
    word_locations_in_samples = None
    audio_transcript = None

    def __init__(self, epitran_model) -> None:
        super().__init__()
        self.epitran_model = epitran_model

    def convertToPhonem(self, sentence: str) -> str:
        app_logger.debug(f'starting EpitranPhonemConverter.convertToPhonem for sentence/token "{sentence}"...')
        phonem_representation = self.epitran_model.transliterate(sentence)
        app_logger.debug(f'EpitranPhonemConverter: got phonem_representation for sentence/token "{sentence}"!')
        return phonem_representation


class EngPhonemConverter(ModelInterfaces.ITextToPhonemModel):

    def __init__(self,) -> None:
        super().__init__()

    def convertToPhonem(self, sentence: str) -> str:
        app_logger.debug(f'starting EngPhonemConverter.convertToPhonem for sentence/token "{sentence}"...')
        phonem_representation = eng_to_ipa.convert(sentence)
        phonem_representation = phonem_representation.replace('*','')
        app_logger.debug(f'EngPhonemConverter: got phonem_representation for sentence/token "{sentence}"!')
        return phonem_representation
