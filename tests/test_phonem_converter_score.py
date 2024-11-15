import unittest

import epitran

from aip_trainer import pronunciationTrainer
from aip_trainer.models import RuleBasedModels


class TestPhonemConverter(unittest.TestCase):

    def test_english_ok(self):
        phonem_converter = RuleBasedModels.EngPhonemConverter()
        output = phonem_converter.convertToPhonem('Hello, this is a test')
        self.assertEqual(output, 'hɛˈloʊ, ðɪs ɪz ə tɛst')

    def test_german_ok(self):
        deu_latn = epitran.Epitran('deu-Latn')
        phonem_converter = RuleBasedModels.EpitranPhonemConverter(deu_latn)
        output = phonem_converter.convertToPhonem('Hallo, das ist ein Test')
        self.assertEqual(output, 'haloː, daːs ɪst aɪ̯n tɛst')


trainer_SST_lambda = {'de': pronunciationTrainer.getTrainer("de")}


class TestScore(unittest.TestCase):

    def test_exact_transcription(self):
        words_real = 'Ich habe sehr viel glück, am leben und gesund zu sein'

        real_and_transcribed_words, _, _ = trainer_SST_lambda['de'].matchSampleAndRecordedWords(
            words_real, words_real)

        pronunciation_accuracy, _ = trainer_SST_lambda['de'].getPronunciationAccuracy(
            real_and_transcribed_words)

        self.assertEqual(int(pronunciation_accuracy), 100)

    def test_incorrect_transcription(self):
        words_real = 'Ich habe sehr viel glück, am leben und gesund zu sein'
        words_transcribed = 'Ic hab zeh viel guck am und gesund tu sein'

        real_and_transcribed_words, _, _ = trainer_SST_lambda['de'].matchSampleAndRecordedWords(
            words_real, words_transcribed)

        pronunciation_accuracy, _ = trainer_SST_lambda['de'].getPronunciationAccuracy(
            real_and_transcribed_words)

        self.assertEqual(int(pronunciation_accuracy), 71)


if __name__ == '__main__':
    unittest.main()
