import json
import unittest

from aip_trainer.lambdas import lambdaSpeechToScore
from tests import EVENTS_FOLDER


def check_output_by_field(output, key, match, expected_output):
    import re

    assert len(output[key].strip()) > 0
    for word in output[key].lstrip().rstrip().split(" "):
        word_check = re.findall(match, word.strip())
        assert len(word_check) == 1
        assert word_check[0] == word.strip()
    output[key] = expected_output[key]
    return output


class TestGetAccuracyFromRecordedAudio(unittest.TestCase):
    def test_GetAccuracyFromRecordedAudio(self):
        self.maxDiff = None

        with open(EVENTS_FOLDER / "GetAccuracyFromRecordedAudio.json", "r") as src:
            inputs_outputs = json.load(src)
        inputs = inputs_outputs["inputs"]
        outputs = inputs_outputs["outputs"]
        for event_name, event_content in inputs.items():
            expected_output = outputs[event_name]
            output = lambdaSpeechToScore.lambda_handler(event_content, [])
            output = json.loads(output)
            assert len(output["matched_transcripts"].strip()) > 0
            assert len(output["matched_transcripts_ipa"].strip()) > 0
            output = check_output_by_field(output, "is_letter_correct_all_words", '[01]+', expected_output)
            output = check_output_by_field(output, "end_time", '\d+\.\d+', expected_output)
            output = check_output_by_field(output, "start_time", '\d+\.\d+', expected_output)
            output = check_output_by_field(output, "pronunciation_accuracy", '\d+', expected_output)
            output["matched_transcripts"] = expected_output["matched_transcripts"]
            output["matched_transcripts_ipa"] = expected_output["matched_transcripts_ipa"]
            output["pronunciation_accuracy"] = expected_output["pronunciation_accuracy"]
            self.assertEqual(expected_output, output)


if __name__ == '__main__':
    unittest.main()
