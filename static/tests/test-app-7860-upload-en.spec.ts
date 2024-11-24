import { test, expect, chromium, Locator } from "@playwright/test";

const basedirAudioFiles = `${__dirname}/../../tests/events`

test("test: get a phonetic accuracy evaluation from an uploaded audio file.", async () => {
  const testAudioEnvPath = `${basedirAudioFiles}/test_en.wav`
  console.log("start");
  console.log("testAudioEnvPath", testAudioEnvPath, "#");
  
  const browser = await chromium.launch({
    args: [
        "--use-fake-device-for-media-stream",
      ],
      ignoreDefaultArgs: ['--mute-audio']
    })

  const context = await browser.newContext();
  context.grantPermissions(["microphone"]);
  const page = await browser.newPage({});

  await page.goto('http://localhost:7860/');
  await page.getByRole('button', { name: 'Run TTS' }).click();
  const buttonPlay = page.getByLabel('Play', { exact: true })
  await buttonPlay.click();
  const waveFormTTS = page.locator('.scroll > .wrapper').first();
  await waveFormTTS.waitFor({ state: 'attached' });
  await waveFormTTS.waitFor({ state: 'visible' });
  await expect(waveFormTTS).toBeVisible();
  console.log("waveFormTTS??");

  const fileChooserPromise = page.waitForEvent('filechooser');
  await page.getByLabel('Upload file').click();
  await page.getByRole('button', { name: 'Drop Audio Here - or - Click' }).click();
  const fileChooser = await fileChooserPromise;
  await fileChooser.setFiles(testAudioEnvPath);
  
  await page.getByRole('button', { name: 'Recognize speech accuracy' }).click();

  await page.waitForTimeout(300);
  const errorsElements = page.getByText(/Error/);
  const ErrorText = errorsElements.all()
  console.log("ErrorText:", (await ErrorText).length, "#");
  await expect(errorsElements).toHaveCount(0);
  console.log("end");
  await page.close();
});
