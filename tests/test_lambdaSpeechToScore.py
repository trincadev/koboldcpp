import json
import os
import platform
import unittest

from aip_trainer import app_logger
from aip_trainer.lambdas import lambdaSpeechToScore
from tests import EVENTS_FOLDER


text_dict = {"de": "Ich bin Alex, wer bist du?", "en": "Hi there, how are you?"}
expected_output = {
    "de": {
        "real_transcript": text_dict["de"],
        "ipa_transcript": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bksv\u025b\u02d0 b\u025bst\u025b\u02d0 du\u02d0",
        "pronunciation_accuracy": 63.0,
        "real_transcripts": text_dict["de"],
        "matched_transcripts": "ich bin alexwe - beste du",
        "real_transcripts_ipa": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bks, v\u0250 b\u026ast du\u02d0?",
        "matched_transcripts_ipa": "\u026a\u00e7 bi\u02d0n a\u02d0l\u025bksv\u0259 - b\u0259st\u0259 du\u02d0",
        "pair_accuracy_category": "0 0 2 2 2 0",
        "start_time": "0.0 0.3075 0.62525 2.1346875 1.5785625 2.1346875",
        "end_time": "0.328 0.6458125 1.44025 2.4730625 2.15525 2.4730625",
        "is_letter_correct_all_words": "111 111 11111 000 1011 111 ",
    },
    "en": {
        "real_transcript": text_dict["en"],
        "ipa_transcript": "ha\u026a ha\u028a \u0259r ju",
        "pronunciation_accuracy": 69.0,
        "real_transcripts": text_dict["en"],
        "matched_transcripts": "hi - how are you",
        "real_transcripts_ipa": "ha\u026a \u00f0\u025br, ha\u028a \u0259r ju?",
        "matched_transcripts_ipa": "ha\u026a  ha\u028a \u0259r ju",
        "pair_accuracy_category": "0 2 0 0 0",
        "start_time": "0.2245625 1.3228125 0.852125 1.04825 1.3228125",
        "end_time": "0.559875 1.658125 1.14825 1.344375 1.658125",
        "is_letter_correct_all_words": "11 000001 111 111 1111 ",
    },
}


def assert_raises_get_speech_to_score_dict(self, real_text, file_bytes_or_audiotmpfile, language, exc, error_message):
    from aip_trainer.lambdas import lambdaSpeechToScore

    with self.assertRaises(exc):
        try:
            lambdaSpeechToScore.get_speech_to_score_dict(
                real_text, file_bytes_or_audiotmpfile, language, remove_random_file=False
            )
        except exc as e:
            self.assertEqual(str(e), error_message)
            raise e


def check_value_by_field(value, match):
    import re

    assert len(value.strip()) > 0
    for word in value.lstrip().rstrip().split(" "):
        word_check = re.findall(match, word.strip())
        assert len(word_check) == 1
        assert word_check[0] == word.strip()


def check_output_by_field(output, key, match, expected_output):
    check_value_by_field(output[key], match)
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
        pronunciation_accuracy = output["pronunciation_accuracy"]
        assert isinstance(pronunciation_accuracy, float)
        assert pronunciation_accuracy <= 100
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
            current_expected_output = outputs[event_name]
            output = lambdaSpeechToScore.lambda_handler(event_content, [])
            output = json.loads(output)
            app_logger.info(
                f"output type:{type(output)}, expected_output type:{type(current_expected_output)}."
            )
            check_output(self, output, current_expected_output)

    def test_get_speech_to_score_en_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "en"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        output = lambdaSpeechToScore.get_speech_to_score_dict(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path),
            language=language,
            remove_random_file=False,
        )
        check_output(self, output, expected_output[language])

    def test_get_speech_to_score_en_ok_remove_input_file(self):
        import shutil
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "en"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        path2 = EVENTS_FOLDER / f"test2_{language}.wav"
        shutil.copy(path, path2)
        assert path2.exists() and path2.is_file()
        output = lambdaSpeechToScore.get_speech_to_score_dict(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path2),
            language=language,
            remove_random_file=True,
        )
        assert not path2.exists()
        check_output(self, output, expected_output[language])

    def test_get_speech_to_score_de_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "de"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        output = lambdaSpeechToScore.get_speech_to_score_dict(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path),
            language=language,
            remove_random_file=False,
        )
        check_output(self, output, expected_output[language])

    def test_get_speech_to_score_de_ok_remove_input_file(self):
        import shutil
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "de"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        path2 = EVENTS_FOLDER / f"test2_{language}.wav"
        shutil.copy(path, path2)
        assert path2.exists() and path2.is_file()
        output = lambdaSpeechToScore.get_speech_to_score_dict(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path2),
            language=language,
            remove_random_file=True,
        )
        assert not path2.exists()
        check_output(self, output, expected_output[language])

    def test_get_speech_to_score_tuple_de_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "de"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        (
            real_transcripts,
            is_letter_correct_all_words,
            pronunciation_accuracy,
            ipa_transcript,
            real_transcripts_ipa,
            dumped,
        ) = lambdaSpeechToScore.get_speech_to_score_tuple(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path),
            language=language,
            remove_random_file=False,
        )
        assert real_transcripts == text_dict[language]
        check_value_by_field(is_letter_correct_all_words, "[01]+")
        assert isinstance(pronunciation_accuracy, float)
        assert pronunciation_accuracy <= 100
        assert len(ipa_transcript.strip()) > 0
        assert len(real_transcripts_ipa.strip()) > 0
        check_output(self, json.loads(dumped), expected_output[language])

    def test_get_speech_to_score_tuple_en_ok(self):
        from aip_trainer.lambdas import lambdaSpeechToScore

        language = "en"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        (
            real_transcripts,
            is_letter_correct_all_words,
            pronunciation_accuracy,
            ipa_transcript,
            real_transcripts_ipa,
            dumped,
        ) = lambdaSpeechToScore.get_speech_to_score_tuple(
            real_text=text_dict[language],
            file_bytes_or_audiotmpfile=str(path),
            language=language,
            remove_random_file=False,
        )
        assert real_transcripts == text_dict[language]
        check_value_by_field(is_letter_correct_all_words, "[01]+")
        assert isinstance(pronunciation_accuracy, float)
        assert pronunciation_accuracy <= 100
        assert len(ipa_transcript.strip()) > 0
        assert len(real_transcripts_ipa.strip()) > 0
        check_output(self, json.loads(dumped), expected_output[language])

    def test_get_speech_to_score_dict__de_empty_input_text(self):
        language = "de"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        assert_raises_get_speech_to_score_dict(self, "", str(path), language, ValueError, "cannot read an empty/None text: ''...")

    def test_get_speech_to_score_dict__en_empty_input_text(self):
        language = "en"
        path = EVENTS_FOLDER / f"test_{language}.wav"
        assert_raises_get_speech_to_score_dict(self, "", str(path), language, ValueError, "cannot read an empty/None text: ''...")

    def test_get_speech_to_score_dict__de_empty_input_file(self):
        language = "de"
        assert_raises_get_speech_to_score_dict(self, "text fake", "", language, ValueError, "cannot read an empty/None file: ''...")

    def test_get_speech_to_score_dict__en_empty_input_file(self):
        language = "en"
        assert_raises_get_speech_to_score_dict(self, "text fake", "", language, ValueError, "cannot read an empty/None file: ''...")
    
    def test_get_speech_to_score_dict__empty_language(self):
        assert_raises_get_speech_to_score_dict(self, "text fake", "fake_file", "", NotImplementedError, "Not tested/supported with '' language...")


if __name__ == "__main__":
    unittest.main()
