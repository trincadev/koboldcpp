import gradio as gr

from aip_trainer import app_logger
from aip_trainer.lambdas import lambdaGetSample, lambdaSpeechToScore, lambdaTTS


js = """
function updateCssText(text, letters) {
    let wordsArr = text.split(" ")
    let lettersWordsArr = letters.split(" ")
    let speechOutputContainer = document.querySelector('#speech-output');
    speechOutputContainer.textContent = ""

    for (let idx in wordsArr) {
        let word = wordsArr[idx]
        let letterIsCorrect = lettersWordsArr[idx]
        for (let idx1 in word) {
        let letterCorrect = letterIsCorrect[idx1] == "1"
        let containerLetter = document.createElement("span")
        containerLetter.style.color = letterCorrect ? 'green' : "red"
        containerLetter.innerText = word[idx1];
        speechOutputContainer.appendChild(containerLetter)
        }
        let containerSpace = document.createElement("span")
        containerSpace.textContent = " "
        speechOutputContainer.appendChild(containerSpace)
    }
}
"""


def change_interactivity_components(components, is_active: bool):
    for component in components:
        component.interactive = is_active


with gr.Blocks() as gradio_app:
    app_logger.info("start gradio app building...")

    gr.Markdown(
        """
        # AI Pronunciation Trainer

        See [my fork](https://github.com/trincadev/ai-pronunciation-trainer) of [AI Pronunciation Trainer](https://github.com/Thiagohgl/ai-pronunciation-trainer) repositroy
        for more details.
        """
    )
    with gr.Row():
        with gr.Column(scale=4, min_width=300):
            with gr.Row():
                with gr.Column(scale=2, min_width=80):
                    language = gr.Radio(["de", "en"], label="Language", value="en")
                with gr.Column(scale=5, min_width=160):
                    difficulty = gr.Radio(
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
                    learner_transcription = gr.Textbox(
                        lines=3,
                        label="Learner Transcription",
                        value="Hi there, how are you?",
                    )
            with gr.Row():
                with gr.Column(scale=7, min_width=240):
                    tts = gr.Audio(label="Audio TTS")
                with gr.Column(scale=1, min_width=50):
                    btn_tts = gr.Button(value="Run TTS")
            with gr.Row():
                learner_recording = gr.Audio(
                    label="Learner Recording",
                    sources=["microphone", "upload"],
                    type="filepath",
                )
        with gr.Column(scale=4, min_width=320):
            examples_text = gr.Examples(
                examples=[
                    ["Hi there, how are you?", "en", 1],
                    ["Hallo, wie geht es dir?", "de", 1],
                    ["Die König-Ludwig-Eiche ist ein Naturdenkmal im Staatsbad Brückenau.", "de", 2,],
                    ["Rome is home to some of the most beautiful monuments in the world.", "en", 2],
                    ["Some machine learning models are designed to understand and generate human-like text based on the input they receive.", "en", 3],
                    ["Die König-Ludwig-Eiche ist ein Naturdenkmal im Staatsbad Brückenau, einem Ortsteil des drei Kilometer nordöstlich gelegenen Bad Brückenau im Landkreis Bad Kissingen in Bayern.", "de", 3],
                ],
                inputs=[learner_transcription, language, difficulty],
            )

            transcripted_text = gr.Textbox(
                lines=2, placeholder=None, label="Transcripted text", visible=False
            )
            letter_correctness = gr.Textbox(
                lines=1,
                placeholder=None,
                label="Letters correctness",
                visible=False,
            )
            pronunciation_accuracy = gr.Textbox(
                lines=1, placeholder=None, label="Pronunciation accuracy %"
            )
            recording_ipa = gr.Textbox(
                lines=1, placeholder=None, label="Learner phonetic transcription"
            )
            ideal_ipa = gr.Textbox(
                lines=1, placeholder=None, label="Ideal phonetic transcription"
            )
            res = gr.Textbox(lines=1, placeholder=None, label="RES", visible=False)
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
            btn_random_phrase.click(
                lambdaGetSample.get_random_selection,
                inputs=[language, difficulty],
                outputs=[learner_transcription],
            )
    btn.click(
        lambdaSpeechToScore.get_speech_to_score_tuple,
        inputs=[learner_transcription, learner_recording, language],
        outputs=[
            transcripted_text,
            letter_correctness,
            pronunciation_accuracy,
            recording_ipa,
            ideal_ipa,
            res,
        ],
    )
    btn_tts.click(
        fn=lambdaTTS.get_tts,
        inputs=[learner_transcription, language],
        outputs=tts,
    )
    html_output.change(
        None,
        inputs=[transcripted_text, letter_correctness],
        outputs=[html_output],
        js=js,
    )


if __name__ == "__main__":
    gradio_app.launch()
