import json
import webbrowser

from aip_trainer import PROJECT_ROOT_FOLDER, app_logger
from flask import Flask, render_template, request, Response
from flask_cors import CORS

from aip_trainer.lambdas import lambdaGetSample
from aip_trainer.lambdas import lambdaSpeechToScore


app = Flask(__name__, template_folder="static")
cors = CORS(app)
app.config['CORS_HEADERS'] = '*'

rootPath = ''


@app.route(rootPath+'/')
def main():
    return render_template('main.html')


@app.route(rootPath+'/getSample', methods=['POST'])
def getNext():
    event = {'body':  json.dumps(request.get_json(force=True))}
    return lambdaGetSample.lambda_handler(event, [])


@app.route(rootPath+'/GetAccuracyFromRecordedAudio', methods=['POST'])
def GetAccuracyFromRecordedAudio():
    try:
        event = {'body': json.dumps(request.get_json(force=True))}
        lambda_correct_output = lambdaSpeechToScore.lambda_handler(event, [])
        return lambda_correct_output
    except Exception as e:
        import traceback
        app_logger.error(e)
        app_logger.error(traceback.format_exc())
        raise e


if __name__ == "__main__":
    language = 'de'
    webbrowser.open_new('http://127.0.0.1:3000/')
    app.run(host="0.0.0.0", port=3000)  # , debug=True)
