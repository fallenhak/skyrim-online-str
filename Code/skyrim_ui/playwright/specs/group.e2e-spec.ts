import { createTest, expect } from '@ngx-playwright/test';
import { ApplicationScreen } from '../screens/main-screen.js';

const test = createTest(ApplicationScreen);

test.describe('Legacy co-op entry points', () => {
  test.beforeEach(async ({ page }) => {
    await page.waitForSelector('.app-root-controls', { state: 'attached' });
    await page.press('body', 'F2');
    await page.waitForSelector('.app-root-controls', { state: 'visible' });

    await page.click('.app-root-menu button');
    await page.waitForSelector('//app-connect');
    await page.locator('//app-connect/div[1]/input[1]').fill('test');
    await page.locator('//app-connect/div[1]/input[2]').fill('test');
    await page.click('//app-connect/div[1]/app-action-buttons[1]/button[1]');
    await expect(page.locator('.app-root-menu button').first()).toHaveText(
      /Disconnect/,
    );
    await page.locator('app-character-select app-action-buttons button').click();
  });

  test('keeps character selection available and hides the old player manager', async ({
    page,
  }) => {
    await expect(
      page.getByRole('button', { name: 'Character Select' }),
    ).toBeVisible();
    await expect(
      page.getByRole('button', { name: 'Player Manager' }),
    ).toHaveCount(0);
    await expect(page.locator('app-player-manager')).toHaveCount(0);
    await expect(page.locator('app-party-menu')).toHaveCount(0);
  });
});
