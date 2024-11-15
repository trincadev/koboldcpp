import numpy as np
import torch

from aip_trainer.models import ModelInterfaces


class NeuralASR(ModelInterfaces.IASRModel):
    word_locations_in_samples = None
    audio_transcript = None

    def __init__(self, model: torch.nn.Module, decoder) -> None:
        super().__init__()
        self.model = model
        self.decoder = decoder  # Decoder from CTC-outputs to transcripts

    def getTranscript(self) -> str:
        """Get the transcripts of the process audio"""
        assert(self.audio_transcript is not None,
               'Can get audio transcripts without having processed the audio')
        return self.audio_transcript

    def getWordLocations(self) -> list:
        """Get the pair of words location from audio"""
        assert(self.word_locations_in_samples is not None,
               'Can get word locations without having processed the audio')

        return self.word_locations_in_samples

    def processAudio(self, audio: torch.Tensor):
        """Process the audio"""
        audio_length_in_samples = audio.shape[1]
        with torch.inference_mode():
            nn_output = self.model(audio)

            self.audio_transcript, self.word_locations_in_samples = self.decoder(
                nn_output[0, :, :].detach(), audio_length_in_samples, word_align=True)
