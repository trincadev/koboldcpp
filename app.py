from pathlib import Path
import gradio as gr

from aip_trainer import PROJECT_ROOT_FOLDER, app_logger, sample_rate_start
from aip_trainer.lambdas import js, lambdaGetSample, lambdaSpeechToScore, lambdaTTS


def clear():
    return None


def clear2():
    return None, None


with gr.Blocks() as gradio_app:
    local_storage = gr.BrowserState([0.0, 0.0])
    app_logger.info("start gradio app building...")

    project_root_folder = Path(PROJECT_ROOT_FOLDER)
    with open(project_root_folder / "aip_trainer" / "lambdas" / "app_description.md", "r", encoding="utf-8") as app_description_src:
        md_app_description = app_description_src.read()
        gr.Markdown(md_app_description.format(sample_rate_start=sample_rate_start))
    with gr.Row():
        with gr.Column(scale=4, min_width=300):
            with gr.Row():
                with gr.Column(scale=2, min_width=80):
                    radio_language = gr.Radio(["de", "en"], label="Language", value="en")
                with gr.Column(scale=5, min_width=160):
                    radio_difficulty = gr.Radio(
                        label="Difficulty",
                        value=0,
                        choices=[
                            ("random", 0),
                            ("easy", 1),
                            ("medium", 2),
                            ("hard", 3),
                        ],
                    )
                with gr.Column(scale=1, min_width=100):
                    btn_random_phrase = gr.Button(value="Choose a random phrase")
            with gr.Row():
                with gr.Column(scale=7, min_width=300):
                    text_learner_transcription = gr.Textbox(
                        lines=3,
                        label="Learner Transcription",
                        value="Hi there, how are you?",
                    )
            with gr.Row():
                with gr.Column(scale=7, min_width=240):
                    audio_tts = gr.Audio(label="Audio TTS")
                with gr.Column(scale=1, min_width=50):
                    btn_run_tts = gr.Button(value="Run TTS")
                    btn_clear_tts = gr.Button(value="Clear TTS")
                    btn_clear_tts.click(clear, inputs=[], outputs=[audio_tts])
            with gr.Row():
                audio_learner_recording_stt = gr.Audio(
                    label="Learner Recording",
                    sources=["microphone", "upload"],
                    type="filepath",
                    show_download_button=True,
                )
        with gr.Column(scale=4, min_width=320):
            text_transcribed_hidden = gr.Textbox(
                lines=2, placeholder=None, label="Transcribed text", visible=False
            )
            text_letter_correctness = gr.Textbox(
                lines=1,
                placeholder=None,
                label="Letters correctness",
                visible=False,
            )
            with gr.Row():
                gr.Markdown("Speech accuracy score (%)")
            with gr.Row():
                    with gr.Column(min_width=100):
                        number_pronunciation_accuracy = gr.Number(label="Current score")
                    with gr.Column(min_width=100):
                        number_score_de = gr.Number(label="Global score DE", value=0, interactive=False)
                    with gr.Column(min_width=100):
                        number_score_en = gr.Number(label="Global score EN", value=0, interactive=False)
            text_recording_ipa = gr.Textbox(
                lines=1, placeholder=None, label="Learner phonetic transcription"
            )
            text_ideal_ipa = gr.Textbox(
                lines=1, placeholder=None, label="Ideal phonetic transcription"
            )
            text_raw_json_output_hidden = gr.Textbox(lines=1, placeholder=None, label="text_raw_json_output_hidden", visible=False)
            html_output = gr.HTML(
                label="Speech accuracy output",
                elem_id="speech-output",
                show_label=True,
                visible=True,
                render=True,
                value=" - ",
                elem_classes="speech-output",
            )
            with gr.Row():
                btn = gr.Button(value="Recognize speech accuracy")
            with gr.Accordion("Click here to expand the table examples", open=False):
                examples_text = gr.Examples(
                    examples=[
                        ["Hallo, wie geht es dir?", "de", 1],
                        ["Hi there, how are you?", "en", 1],
                        ["Die König-Ludwig-Eiche ist ein Naturdenkmal im Staatsbad Brückenau.", "de", 2,],
                        ["Rome is home to some of the most beautiful monuments in the world.", "en", 2],
                        ["Die König-Ludwig-Eiche ist ein Naturdenkmal im Staatsbad Brückenau, einem Ortsteil des drei Kilometer nordöstlich gelegenen Bad Brückenau im Landkreis Bad Kissingen in Bayern.", "de", 3],
                        ["Some machine learning models are designed to understand and generate human-like text based on the input they receive.", "en", 3],
                    ],
                    inputs=[text_learner_transcription, radio_language, radio_difficulty],
                )

    def get_updated_score_by_language(text: str, audio_rec: str | Path, lang: str, score_de: float, score_en: float):
        _transcribed_text, _letter_correctness, _pronunciation_accuracy, _recording_ipa, _ideal_ipa, _res = lambdaSpeechToScore.get_speech_to_score_tuple(text, audio_rec, lang)
        output = {
            text_transcribed_hidden: _transcribed_text,
            text_letter_correctness: _letter_correctness,
            number_pronunciation_accuracy: _pronunciation_accuracy,
            text_recording_ipa: _recording_ipa,
            text_ideal_ipa: _ideal_ipa,
            text_raw_json_output_hidden: _res,
        }
        match lang:
            case "de":
                return {
                    number_score_de: float(score_de) + float(_pronunciation_accuracy),
                    number_score_en: float(score_en),
                    **output
                }
            case "en":
                return {
                    number_score_en: float(score_en) + float(_pronunciation_accuracy),
                    number_score_de: float(score_de),
                    **output
                }
            case _:
                raise NotImplementedError(f"Language {lang} not supported")

    btn.click(
        get_updated_score_by_language,
        inputs=[text_learner_transcription, audio_learner_recording_stt, radio_language, number_score_de, number_score_en],
        outputs=[
            text_transcribed_hidden,
            text_letter_correctness,
            number_pronunciation_accuracy,
            text_recording_ipa,
            text_ideal_ipa,
            text_raw_json_output_hidden,
            number_score_de, number_score_en
        ],
    )
    btn_run_tts.click(
        fn=lambdaTTS.get_tts,
        inputs=[text_learner_transcription, radio_language],
        outputs=audio_tts,
    )
    btn_random_phrase.click(
        lambdaGetSample.get_random_selection,
        inputs=[radio_language, radio_difficulty],
        outputs=[text_learner_transcription],
    )
    btn_random_phrase.click(
        clear2,
        inputs=[],
        outputs=[audio_learner_recording_stt, audio_tts]
    )
    html_output.change(
        None,
        inputs=[text_transcribed_hidden, text_letter_correctness],
        outputs=[html_output],
        js=js.js_update_ipa_output,
    )
    
    @gradio_app.load(inputs=[local_storage], outputs=[number_score_de, number_score_en])
    def load_from_local_storage(saved_values):
        print("loading from local storage", saved_values)
        return saved_values[0], saved_values[1]

    @gr.on([number_score_de.change, number_score_en.change], inputs=[number_score_de, number_score_en], outputs=[local_storage])
    def save_to_local_storage(score_de, score_en):
        return [score_de, score_en]


if __name__ == "__main__":
    gradio_app.launch()
