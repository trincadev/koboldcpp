
import base64
import json
import os
from pathlib import Path
import tempfile
import time

import audioread
import numpy as np
import torch
from torchaudio.transforms import Resample

from aip_trainer import WordMatching as wm, app_logger
from aip_trainer import pronunciationTrainer, sample_rate_start


trainer_SST_lambda = {
    'de': pronunciationTrainer.getTrainer("de"),
    'en': pronunciationTrainer.getTrainer("en")
}
transform = Resample(orig_freq=sample_rate_start, new_freq=16000)


def lambda_handler(event, context):
    data = json.loads(event['body'])

    real_text = data['title']
    base64Audio = data["base64Audio"]
    app_logger.debug(f"base64Audio:{base64Audio} ...")
    file_bytes_or_audiotmpfile = base64.b64decode(base64Audio[22:].encode('utf-8'))
    language = data['language']

    if len(real_text) == 0:
        return {
            'statusCode': 200,
            'headers': {
                'Access-Control-Allow-Headers': '*',
                'Access-Control-Allow-Credentials': "true",
                'Access-Control-Allow-Origin': 'http://127.0.0.1:3000/',
                'Access-Control-Allow-Methods': 'OPTIONS,POST,GET'
            },
            'body': ''
        }
    output = get_speech_to_score_dict(real_text=real_text, file_bytes_or_audiotmpfile=file_bytes_or_audiotmpfile, language=language, remove_random_file=False)
    output = json.dumps(output)
    app_logger.debug(f"output: {output} ...")
    return output


def get_speech_to_score_dict(real_text: str, file_bytes_or_audiotmpfile: str | dict, language: str = "en", remove_random_file: bool = True):
    app_logger.info(f"real_text:{real_text} ...")
    app_logger.debug(f"file_bytes:{file_bytes_or_audiotmpfile} ...")
    app_logger.info(f"language:{language} ...")

    if real_text is None or len(real_text) == 0:
        raise ValueError(f"cannot read an empty/None text: '{real_text}'...")
    if language is None or len(language) == 0:
        raise NotImplementedError(f"Not tested/supported with '{language}' language...")
    if not isinstance(file_bytes_or_audiotmpfile, (bytes, bytearray)) and (file_bytes_or_audiotmpfile is None or len(file_bytes_or_audiotmpfile) == 0 or os.path.getsize(file_bytes_or_audiotmpfile) == 0):
        raise ValueError(f"cannot read an empty/None file: '{file_bytes_or_audiotmpfile}'...")

    start0 = time.time()

    random_file_name = file_bytes_or_audiotmpfile
    app_logger.debug(f"random_file_name:{random_file_name} ...")
    if isinstance(file_bytes_or_audiotmpfile, (bytes, bytearray)):
        app_logger.debug("writing streaming data to file on disk...")
        with tempfile.NamedTemporaryFile(prefix="temp_sound_speech_score_", suffix=".ogg", delete=False) as f1:
            f1.write(file_bytes_or_audiotmpfile)
            duration = time.time() - start0
            app_logger.info(f'Saved binary data in file in {duration}s.')
            random_file_name = f1.name

    start = time.time()
    app_logger.info(f'Loading .ogg file file {random_file_name} ...')
    signal, _ = audioread_load(random_file_name)

    duration = time.time() - start
    app_logger.info(f'Read .ogg file {random_file_name} in {duration}s.')

    signal = transform(torch.Tensor(signal)).unsqueeze(0)

    duration = time.time() - start
    app_logger.info(f'Loaded .ogg file {random_file_name} in {duration}s.')

    language_trainer_sst_lambda = trainer_SST_lambda[language]
    app_logger.info('language_trainer_sst_lambda: preparing...')
    result = language_trainer_sst_lambda.processAudioForGivenText(signal, real_text)
    app_logger.info(f'language_trainer_sst_lambda: result: {result}...')

    start = time.time()
    if remove_random_file:
        os.remove(random_file_name)
    duration = time.time() - start
    app_logger.info(f'Deleted file {random_file_name} in {duration}s.')

    start = time.time()
    real_transcripts_ipa = ' '.join(
        [word[0] for word in result['real_and_transcribed_words_ipa']])
    matched_transcripts_ipa = ' '.join(
        [word[1] for word in result['real_and_transcribed_words_ipa']])

    real_transcripts = ' '.join(
        [word[0] for word in result['real_and_transcribed_words']])
    matched_transcripts = ' '.join(
        [word[1] for word in result['real_and_transcribed_words']])

    words_real = real_transcripts.lower().split()
    mapped_words = matched_transcripts.split()

    is_letter_correct_all_words = ''
    for idx, word_real in enumerate(words_real):
        mapped_letters, _ = wm.get_best_mapped_words(
            mapped_words[idx], word_real
        )

        is_letter_correct = wm.getWhichLettersWereTranscribedCorrectly(
            word_real, mapped_letters)  # , mapped_letters_indices)

        is_letter_correct_all_words += ''.join([str(is_correct)
                                                for is_correct in is_letter_correct]) + ' '

    pair_accuracy_category = ' '.join(
        [str(category) for category in result['pronunciation_categories']])
    duration = time.time() - start
    duration_tot = time.time() - start0
    app_logger.info(f'Time to post-process results: {duration}, tot_duration:{duration_tot}.')
    pronunciation_accuracy = str(int(result['pronunciation_accuracy']))
    ipa_transcript = result['recording_ipa']

    return {'real_transcript': result['recording_transcript'],
           'ipa_transcript': ipa_transcript,
           'pronunciation_accuracy': pronunciation_accuracy,
           'real_transcripts': real_transcripts, 'matched_transcripts': matched_transcripts,
           'real_transcripts_ipa': real_transcripts_ipa, 'matched_transcripts_ipa': matched_transcripts_ipa,
           'pair_accuracy_category': pair_accuracy_category,
           'start_time': result['start_time'],
           'end_time': result['end_time'],
           'is_letter_correct_all_words': is_letter_correct_all_words}


def get_speech_to_score_tuple(real_text: str, file_bytes_or_audiotmpfile: str | dict, language: str = "en", remove_random_file: bool = True):
    output = get_speech_to_score_dict(real_text=real_text, file_bytes_or_audiotmpfile=file_bytes_or_audiotmpfile, language=language, remove_random_file=remove_random_file)
    real_transcripts = output['real_transcripts']
    is_letter_correct_all_words = output['is_letter_correct_all_words']
    pronunciation_accuracy = output['pronunciation_accuracy']
    ipa_transcript = output['ipa_transcript']
    real_transcripts_ipa = output['real_transcripts_ipa']
    return real_transcripts, is_letter_correct_all_words, pronunciation_accuracy, ipa_transcript, real_transcripts_ipa, json.dumps(output)


# From Librosa

def calc_start_end(sr_native, time_position, n_channels):
    return int(np.round(sr_native * time_position)) * n_channels


def audioread_load(path, offset=0.0, duration=None, dtype=np.float32):
    """Load an audio buffer using audioread.

    This loads one block at a time, and then concatenates the results.
    """

    import shutil
    shutil.copyfile(path, Path("/tmp") / f"test_en_{Path(path).name}")
    y = []
    app_logger.debug(f"reading audio file at path:{path} ...")
    with audioread.audio_open(path) as input_file:
        sr_native = input_file.samplerate
        n_channels = input_file.channels

        s_start = calc_start_end(sr_native, offset, n_channels)

        if duration is None:
            s_end = np.inf
        else:
            duration = calc_start_end(sr_native, duration, n_channels)
            s_end = duration + s_start

        n = 0

        for frame in input_file:
            frame = buf_to_float(frame, dtype=dtype)
            n_prev = n
            n = n + len(frame)

            if n < s_start:
                # offset is after the current frame
                # keep reading
                continue

            if s_end < n_prev:
                # we're off the end.  stop reading
                break

            if s_end < n:
                # the end is in this frame.  crop.
                frame = frame[: s_end - n_prev]

            if n_prev <= s_start <= n:
                # beginning is in this frame
                frame = frame[(s_start - n_prev):]

            # tack on the current frame
            y.append(frame)

    if y:
        y = np.concatenate(y)
        if n_channels > 1:
            y = y.reshape((-1, n_channels)).T
    else:
        y = np.empty(0, dtype=dtype)

    return y, sr_native


# From Librosa


def buf_to_float(x, n_bytes=2, dtype=np.float32):
    """Convert an integer buffer to floating point values.
    This is primarily useful when loading integer-valued wav data
    into numpy arrays.

    Parameters
    ----------
    x : np.ndarray [dtype=int]
        The integer-valued data buffer

    n_bytes : int [1, 2, 4]
        The number of bytes per sample in ``x``

    dtype : numeric type
        The target output type (default: 32-bit float)

    Returns
    -------
    x_float : np.ndarray [dtype=float]
        The input data buffer cast to floating point
    """

    # Invert the scale of the data
    scale = 1.0 / float(1 << ((8 * n_bytes) - 1))

    # Construct the format string
    fmt = "<i{:d}".format(n_bytes)

    # Rescale and format the data buffer
    return scale * np.frombuffer(x, fmt).astype(dtype)
