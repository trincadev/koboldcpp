import json
import os
import unittest

import epitran
import structlog

from aip_trainer.models import RuleBasedModels
from aip_trainer import pronunciationTrainer, LOG_JSON_FORMAT
from aip_trainer.lambdas import lambdaGetSample
from aip_trainer.utils import session_logger


log_level = os.getenv("LOG_LEVEL", "INFO")
session_logger.setup_logging(json_logs=LOG_JSON_FORMAT, log_level=log_level)
test_logger = structlog.stdlib.get_logger(__name__)


def test_category(category: int, threshold_min: int, threshold_max: int, n: int = 1000):
    for _ in range(n):
        event = {'body': json.dumps({'category': category, 'language': 'de'})}
        response = lambdaGetSample.lambda_handler(event, [])
        response_dict = json.loads(response)
        number_of_words = len(response_dict['real_transcript'][0].split())
        try:
            assert threshold_min < number_of_words <= threshold_max
        except AssertionError:
            test_logger.error(
                f"Category: {category} had a sentence with length {number_of_words}.")
            raise AssertionError


class TestDataset(unittest.TestCase):
    def test_random_sentences(self):
        test_category(0, 0, 40)

    def test_easy_sentences(self):
        test_category(1, 0, 8)

    def test_normal_sentences(self):
        test_category(2, 8, 20)

    def test_hard_sentences(self):
        test_category(3, 20, 10000)


class TestPhonemConverter(unittest.TestCase):

    def test_english(self):
        phonem_converter = RuleBasedModels.EngPhonemConverter()
        output = phonem_converter.convertToPhonem('Hello, this is a test')
        self.assertEqual(output, 'hɛˈloʊ, ðɪs ɪz ə tɛst')

    def test_german_ok(self):
        deu_latn = epitran.Epitran('deu-Latn')
        phonem_converter = RuleBasedModels.EpitranPhonemConverter(deu_latn)
        output = phonem_converter.convertToPhonem('Hallo, das ist ein Test')
        self.assertEqual(output, 'haloː, dɑːs ɪst ain tɛst')


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
