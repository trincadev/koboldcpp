import json
import unittest

from aip_trainer.lambdas import lambdaGetSample
from tests import test_logger, TEST_ROOT_FOLDER


def helper_category(category: int, threshold_min: int, threshold_max: int, n: int = 1000):
    for _ in range(n):
        event = {'body': json.dumps({'category': category, 'language': 'de'})}
        response = lambdaGetSample.lambda_handler(event, [])
        response_dict = json.loads(response)
        number_of_words = len(response_dict['real_transcript'].split())
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

    def test_get_pickle2json_dataframe(self):
        import os

        custom_filename = 'test_data_de_en_2'
        lambdaGetSample.get_pickle2json_dataframe(custom_filename, TEST_ROOT_FOLDER)
        with open(TEST_ROOT_FOLDER / f'{custom_filename}.json', 'r') as src1:
            with open(TEST_ROOT_FOLDER / f'{custom_filename}_expected.json', 'r') as src2:
                json1 = json.load(src1)
                json2 = json.load(src2)
                assert json1 == json2
        os.remove(TEST_ROOT_FOLDER / f'{custom_filename}.json')


if __name__ == '__main__':
    unittest.main()
