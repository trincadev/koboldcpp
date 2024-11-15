
# AI Pronunciation Trainer

This tool uses AI to evaluate your pronunciation so you can improve it and be understood more clearly. You can go straight test the tool at <https://aipronunciationtr.com> (please use the chrome browser for desktop and have some patience for it to "warm-up" :) ).

![](images/MainScreen.jpg)

## Installation

To run the program locally, you need to install the requirements and run the main python file:

```bash
pip install -r requirements.txt
python webApp.py
```

You'll also need ffmpeg, which you can download from here <https://ffmpeg.org/download.html>. On Windows, it may be needed to add the ffmpeg "bin" folder to your PATH environment variable. On Mac, you can also just run "brew install ffmpeg".

You should be able to run it locally without any major issues as long as you’re using a recent python 3.X version.  

## Changes on [trincadev's](https://github.com/trincadev/) [repository](https://github.com/trincadev/ai-pronunciation-trainer)

I upgraded the frontend (jquery@3.7.1, bootstrap@5.3.3) and backend (pytorch==1.13.1, numpy<2.0.0) libraries where possible: for example right now this project doesn't work with pytorch > 2.0.0, then we are locked with pytorch == 1.13.1.

### E2E tests with playwright

Normally I use Visual Studio Code to write and execute my playwright tests, however it's always possible to run them from cli (from the `static` folder, using a node package manager like `npm` or `pnpm`):

```bash
pnpm install
pnpm playwright test
```

### Unused classes and functions (now removed)

- `aip_trainer.lambdas.lambdaTTS.*`
- `aip_trainer.models.models.getTTSModel()`
- `aip_trainer.models.models.getTranslationModel()`
- `aip_trainer.models.AllModels.NeuralTTS`
- `aip_trainer.models.AllModels.NeuralTranslator`

### Security implication of using pytorch v1.13.1

Pytorch versions prior to 2.2.0 are known to have security vulnerabilities:

- https://security.snyk.io/vuln/SNYK-PYTHON-TORCH-6619806
- https://security.snyk.io/vuln/SNYK-PYTHON-TORCH-6649934

For this reason, it's best to use this project only in a local environment.

### TODO

- add more e2e tests with playwright
- move from pytorch to onnxruntime
- refactor frontend with something more modern (e.g. vuejs)
- refactor css style with tailwindcss
- add an updated online version on Cloudflare or AWS

## Online version

For the people who don’t feel comfortable running code or just want to have a quick way to use the tool, I hosted an online version of it at <https://aipronunciationtr.com>. It should work well in desktop-chrome, any other browser is not officially supported, although most of the functionality should work fine.

Please be aware that the usage is limited by day (I’m still not rich ;)). If, for some reason, you would like to avoid the daily usage limit, just enter in contact and we see what we can do.

## Motivation

Often, when we want to improve our pronunciation, it is very difficult to self-assess how good we’re speaking. Asking a native, or language instructor, to constantly correct us is either impractical, due to monetary constrains, or annoying due to simply being too boring for this other person. Additionally, they may often say “it sounds good” after your 10th try to not discourage you, even though you may still have some mistakes in your pronunciation.

The AI pronunciation trainer is a way to provide objective feedback on how well your pronunciation is in an automatic and scalable fashion, so the only limit to your improvement is your own dedication.

This project originated from a small program that I did to improve my own pronunciation.  When I finished it, I believed it could be a useful tool also for other people trying to be better understood, so I decided to make a simple, more user-friendly version of it.

## Disclaimer

This is a simple project that I made in my free time with the goal to be useful to some people. It is not perfect, thus be aware that some small bugs may be present. In case you find something is not working, all feedback is welcome, and issues may be addressed depending on their severity.
