import json
import pickle
from pathlib import Path

import epitran
import pandas as pd

from aip_trainer import PROJECT_ROOT_FOLDER, app_logger
from aip_trainer.models import RuleBasedModels


class TextDataset:
    def __init__(self, table, language='-'):
        self.table_dataframe = table
        self.number_of_samples = len(table)
        self.language = language

    def __getitem__(self, idx):
        language_sentence = f"{self.language}_sentence" if self.language != '-' else 'sentence'
        language_series = self.table_dataframe[language_sentence]
        return [language_series.iloc[idx]]

    def __len__(self):
        return self.number_of_samples

    def get_category_from_df_by_language(self, language: str, category_value:int):
        selector = self.table_dataframe[f"{language}_category"] == category_value
        df_by_category = self.table_dataframe[selector]
        return df_by_category

    def get_random_sample_from_df(self, language: str, category_value:int):
        app_logger.info(f"language={language}, category_value={category_value}.")
        choice = self.table_dataframe.sample(n=1)
        if category_value !=0:
            df_language_filtered_by_category_and_language = self.get_category_from_df_by_language(language, category_value)
            choice = df_language_filtered_by_category_and_language.sample(n=1)
        return [choice[f"{language}_sentence"].iloc[0]]


sample_folder = Path(PROJECT_ROOT_FOLDER / "aip_trainer" / "lambdas")
lambda_database = {}
lambda_ipa_converter = {}

with open(sample_folder / 'data_de_en_with_categories.json', 'r') as src:
    df = pd.read_json(src)

lambda_database['de'] = TextDataset(df, 'de')
lambda_database['en'] = TextDataset(df, 'en')
lambda_translate_new_sample = False
lambda_ipa_converter['de'] = RuleBasedModels.EpitranPhonemConverter(
    epitran.Epitran('deu-Latn'))
lambda_ipa_converter['en'] = RuleBasedModels.EngPhonemConverter()


def lambda_handler(event, context):
    body = json.loads(event['body'])

    try:
        category = int(body['category'])
    except KeyError:
        category = 0
    language = body['language']
    try:
        current_transcript = str(body["transcript"])
    except KeyError:
        current_transcript = get_random_selection(language, category, is_gradio_output=False)
    current_transcript = current_transcript if isinstance(current_transcript, str) else current_transcript[0]
    current_ipa = lambda_ipa_converter[language].convertToPhonem(current_transcript)

    app_logger.info(f"real_transcript='{current_transcript}', ipa_transcript='{current_ipa}'.")
    result = {
        'real_transcript': current_transcript,
        'ipa_transcript': current_ipa,
        'transcript_translation': ""
    }

    return json.dumps(result)


def get_random_selection(language: str, category: int, is_gradio_output=True):
    lambda_df_lang = lambda_database[language]
    current_transcript = lambda_df_lang.get_random_sample_from_df(language, category)
    app_logger.info(f"category={category}, language={language}, current_transcript={current_transcript}.")
    return current_transcript[0] if is_gradio_output else current_transcript


def getSentenceCategory(sentence) -> int:
    number_of_words = len(sentence.split())
    categories_word_limits = [0, 8, 20, 100000]
    for category in range(len(categories_word_limits) - 1):
        if categories_word_limits[category] < number_of_words <= categories_word_limits[category + 1]:
            return category + 1


def get_pickle2json_dataframe(
        custom_pickle_filename_no_ext: Path | str = 'data_de_en_2',
        custom_folder: Path = sample_folder
    ):
    custom_folder = Path(custom_folder)
    with open(custom_folder / f'{custom_pickle_filename_no_ext}.pickle', 'rb') as handle:
        df2 = pickle.load(handle)
        pass
        df2["de_category"] = df2["de_sentence"].apply(getSentenceCategory)
        print("de_category added")
        df2["en_category"] = df2["en_sentence"].apply(getSentenceCategory)
        print("en_category added")
    df_json = df2.to_json()
    with open(custom_folder / f'{custom_pickle_filename_no_ext}.json', 'w') as dst:
        dst.write(df_json)
        print("data_de_en_with_categories.json written")

