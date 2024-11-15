import json
import pickle
import random
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
        sample_idx = int(body['idx'])
    except KeyError:
        sample_idx = None

    app_logger.info(f"category={category}, language={language}, sample_idx={sample_idx}.")
    lambda_df_lang = lambda_database[language]
    current_transcript = lambda_df_lang[sample_idx] if sample_idx is not None else lambda_df_lang.get_random_sample_from_df(language, category)
    # sentence_category = getSentenceCategory(current_transcript[0])
    current_ipa = lambda_ipa_converter[language].convertToPhonem(current_transcript[0])

    app_logger.info(f"real_transcript={current_transcript}, ipa_transcript={current_ipa}.")
    result = {
        'real_transcript': current_transcript,
        'ipa_transcript': current_ipa,
        'transcript_translation': ""
    }

    return json.dumps(result)


def getSentenceCategory(sentence) -> int:
    number_of_words = len(sentence.split())
    categories_word_limits = [0, 8, 20, 100000]
    for category in range(len(categories_word_limits) - 1):
        if categories_word_limits[category] < number_of_words <= categories_word_limits[category + 1]:
            return category + 1


if __name__ == "__main__":
    import pandas as pd
    with open(sample_folder / 'data_de_en_2.pickle', 'rb') as handle:
        df = pickle.load(handle)
        pass
        df["de_category"] = df["de_sentence"].apply(getSentenceCategory)
        print("de_category added")
        df["en_category"] = df["en_sentence"].apply(getSentenceCategory)
        print("en_category added")
    df_json = df.to_json()
    with open(sample_folder / 'data_de_en_with_categories.json', 'w') as dst:
        dst.write(df_json)
        print("data_de_en_with_categories.json written")
    with open(sample_folder / 'data_de_en_with_categories.json', 'r') as src:
        jj = json.load(src)
        print("jj:", jj)
        df2 = pd.read_json(json.dumps(jj))
        print(df2)
