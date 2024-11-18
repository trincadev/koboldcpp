import json
import os
import platform
import unittest

from aip_trainer import app_logger
from aip_trainer.lambdas import lambdaSpeechToScore
from tests import EVENTS_FOLDER


text_dict = {
    "de": "Ich bin Alex, wer bist du?",
    "en": "Hi there, how are you?"
}


def check_output_by_field(output, key, match, expected_output):
    import re

    assert len(output[key].strip()) > 0
    for word in output[key].lstrip().rstrip().split(" "):
        word_check = re.findall(match, word.strip())
        assert len(word_check) == 1
        assert word_check[0] == word.strip()
    output[key] = expected_output[key]
    return output


def check_output(self, output, expected_output):
    self.maxDiff = None
    try:
        assert len(output["matched_transcripts"].strip()) > 0
        assert len(output["matched_transcripts_ipa"].strip()) > 0
        assert len(output["ipa_transcript"].strip()) > 0
        assert len(output["real_transcripts_ipa"].strip()) > 0
        output = check_output_by_field(
            output, "is_letter_correct_all_words", "[01]+", expected_output
        )
        output = check_output_by_field(output, "end_time", "\d+\.\d+", expected_output)
        output = check_output_by_field(
            output, "start_time", "\d+\.\d+", expected_output
        )
        output = check_output_by_field(
            output, "pronunciation_accuracy", "\d+", expected_output
        )
        output["matched_transcripts"] = expected_output["matched_transcripts"]
        output["matched_transcripts_ipa"] = expected_output["matched_transcripts_ipa"]
        output["pronunciation_accuracy"] = expected_output["pronunciation_accuracy"]
        output["pair_accuracy_category"] = expected_output["pair_accuracy_category"]
        output["ipa_transcript"] = expected_output["ipa_transcript"]
        output["real_transcript"] = expected_output["real_transcript"]
        output["real_transcripts_ipa"] = expected_output["real_transcripts_ipa"]
        self.assertDictEqual(expected_output, output)
    except Exception as e:
        app_logger.error(f"e:{e}.")
        raise e


class TestGetAccuracyFromRecordedAudio(unittest.TestCase):
    def setUp(self):
        if platform.system() == "Windows" or platform.system() == "Win32":
            os.environ["PYTHONUTF8"] = "1"

    def tearDown(self):
        if (
            platform.system() == "Windows" or platform.system() == "Win32"
        ) and "PYTHONUTF8" in os.environ:
            del os.environ["PYTHONUTF8"]

    def test_GetAccuracyFromRecordedAudio(self):
        with open(EVENTS_FOLDER / "GetAccuracyFromRecordedAudio.json", "r") as src:
            inputs_outputs = json.load(src)
        inputs = inputs_outputs["inputs"]
        outputs = inputs_outputs["outputs"]
        for event_name, event_content in inputs.items():
            expected_output = outputs[event_name]
            output = lambdaSpeechToScore.lambda_handler(event_content, [])
            output = json.loads(output)
            app_logger.info(
                f"output type:{type(output)}, expected_output type:{type(expected_output)}."
            )
            check_output(self, output, expected_output)

    def test_get_speech_to_score_en_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "en"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        output = lambdaSpeechToScore.get_speech_to_score(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=path,
            language=language,
            remove_random_file=False,
        )
        expected_output = {
            "real_transcript": text_dict[language],
            "ipa_transcript": "ha\u026a ha\u028a \u0259r ju",
            "pronunciation_accuracy": "69",
            "real_transcripts": text_dict[language],
            "matched_transcripts": "hi - how are you",
            "real_transcripts_ipa": "ha\u026a \u00f0\u025br, ha\u028a \u0259r ju?",
            "matched_transcripts_ipa": "ha\u026a  ha\u028a \u0259r ju",
            "pair_accuracy_category": "0 2 0 0 0",
            "start_time": "0.2245625 1.3228125 0.852125 1.04825 1.3228125",
            "end_time": "0.559875 1.658125 1.14825 1.344375 1.658125",
            "is_letter_correct_all_words": "11 000001 111 111 1111 ",
        }
        check_output(self, json.loads(output), expected_output)

    def test_get_speech_to_score_de_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "de"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        output = lambdaSpeechToScore.get_speech_to_score(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=path,
            language=language,
            remove_random_file=False,
        )
        expected_output = {
            "real_transcript": text_dict[language],
            "ipa_transcript": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bksv\u025b\u02d0 b\u025bst\u025b\u02d0 du\u02d0",
            "pronunciation_accuracy": "63",
            "real_transcripts": text_dict[language],
            "matched_transcripts": "ich bin alexwe - beste du",
            "real_transcripts_ipa": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bks, v\u0250 b\u026ast du\u02d0?",
            "matched_transcripts_ipa": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bksv\u0259 - b\u0259st\u0259 du\u02d0",
            "pair_accuracy_category": "0 0 2 2 2 0",
            "start_time": "0.0 0.3075 0.62525 2.1346875 1.5785625 2.1346875",
            "end_time": "0.328 0.6458125 1.44025 2.4730625 2.15525 2.4730625",
            "is_letter_correct_all_words": "111 111 11111 000 1011 111 ",
        }
        check_output(self, json.loads(output), expected_output)


if __name__ == "__main__":
    unittest.main()
