import { test, expect, chromium } from "@playwright/test";

test("test: get a custom sample writing within the input field.", async () => {
  const browser = await chromium.launch({
    args: [
      "--use-fake-device-for-media-stream",
      "--use-fake-ui-for-media-stream",
    ],
  });
  const context = await browser.newContext();
  context.grantPermissions(["microphone"]);
  const page = await browser.newPage({});
  await page.goto("http://localhost:3000");

  const inputField = page.getByPlaceholder(
    "Write and press enter to filter"
  );
  await inputField.fill("Hi Tom, how are you?");
  await inputField.press("Enter");
  await expect(page.getByText("Hi Tom, how are you?")).toBeVisible();
  await expect(
    page.getByText("/ hiː toːm, hoː aːrɛː yːuː? /")
  ).toBeVisible();
  console.log("end");
  await page.close();
});
