import logging
import os
import time

import gradio as gr
import structlog
import uvicorn
from aip_trainer.lambdas import lambdaSpeechToScore
from asgi_correlation_id import CorrelationIdMiddleware
from asgi_correlation_id.context import correlation_id
from dotenv import load_dotenv
from fastapi import FastAPI, Request, Response
from uvicorn.protocols.utils import get_path_with_query_string

from aip_trainer.utils.session_logger import setup_logging
from aip_trainer.lambdas.routes import router


load_dotenv()

LOG_JSON_FORMAT = bool(os.getenv("LOG_JSON_FORMAT", False))
LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO")
setup_logging(json_logs=LOG_JSON_FORMAT, log_level=LOG_LEVEL)
logger = structlog.stdlib.get_logger(__name__)
app = FastAPI(title="Example API", version="1.0.0")


@app.middleware("http")
async def logging_middleware(request: Request, call_next) -> Response:
    structlog.contextvars.clear_contextvars()
    # These context vars will be added to all log entries emitted during the request
    request_id = correlation_id.get()
    # print(f"request_id:{request_id}.")
    structlog.contextvars.bind_contextvars(request_id=request_id)

    start_time = time.perf_counter_ns()
    # If the call_next raises an error, we still want to return our own 500 response,
    # so we can add headers to it (process time, request ID...)
    response = Response(status_code=500)
    try:
        response = await call_next(request)
    except Exception:
        # TODO: Validate that we don't swallow exceptions (unit test?)
        structlog.stdlib.get_logger("api.error").exception("Uncaught exception")
        raise
    finally:
        process_time = time.perf_counter_ns() - start_time
        status_code = response.status_code
        url = get_path_with_query_string(request.scope)
        client_host = request.client.host
        client_port = request.client.port
        http_method = request.method
        http_version = request.scope["http_version"]
        # Recreate the Uvicorn access log format, but add all parameters as structured information
        logger.info(
            f"""{client_host}:{client_port} - "{http_method} {url} HTTP/{http_version}" {status_code}""",
            http={
                "url": str(request.url),
                "status_code": status_code,
                "method": http_method,
                "request_id": request_id,
                "version": http_version,
            },
            network={"client": {"ip": client_host, "port": client_port}},
            duration=process_time,
        )
        response.headers["X-Process-Time"] = str(process_time / 10 ** 9)
        return response


app.include_router(router)
logger.info("routes included, creating gradio app")
CUSTOM_GRADIO_PATH = "/"


def get_gradio_app():
    with gr.Blocks() as gradio_app:
        logger.info("start gradio app building...")
        gr.Markdown(
            """
            # Hello World!

            Start typing below to _see_ the *output*.

            Here a [link](https://huggingface.co/spaces/aletrn/gradio_with_fastapi).
            """
        )
        learner_transcription = gr.Textbox(
            label="Learner Transcription",
            placeholder="It is nice to wreck a nice beach",
        )
        language = gr.Textbox(
            label="language",
            placeholder="en",
        )
        learner_recording = gr.Audio(
            label="Learner Recording",
            sources=["microphone", "upload"],
            type="filepath"
        )
        text_output = gr.Textbox(lines=1, placeholder=None, label="Text Output")
        btn = gr.Button(value="get speech score")
        """
        event = {'body': json.dumps(request.get_json(force=True))}
        lambda_correct_output = lambdaSpeechToScore.lambda_handler(event, [])
        """
        btn.click(
            lambdaSpeechToScore.get_speech_to_score,
            inputs=[learner_transcription, learner_recording, language],
            outputs=[text_output]
        )
    return gradio_app


logger.info("mounting gradio app within FastAPI...")
gradio_app_md = get_gradio_app()
app.add_middleware(CorrelationIdMiddleware)
app = gr.mount_gradio_app(app, gradio_app_md, path=CUSTOM_GRADIO_PATH)
logger.info("gradio app mounted")


if __name__ == "__main__":
    try:
        uvicorn.run("app:app", host="127.0.0.1", port=7860, log_config=None, reload=True)
    except Exception as ex:
        logging.error(f"ex:{ex}.")
        raise ex
