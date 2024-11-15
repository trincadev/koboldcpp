import json
import unittest

from aip_trainer.lambdas import lambdaGetSample
from tests import test_logger


def helper_category(category: int, threshold_min: int, threshold_max: int, n: int = 1000):
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
        helper_category(0, 0, 40)

    def test_easy_sentences(self):
        helper_category(1, 0, 8)

    def test_normal_sentences(self):
        helper_category(2, 8, 20)

    def test_hard_sentences(self):
        helper_category(3, 20, 10000)


if __name__ == '__main__':
    unittest.main()
