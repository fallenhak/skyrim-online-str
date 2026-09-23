import { createTest, expect } from '@ngx-playwright/test';
import { ApplicationScreen } from '../screens/main-screen.js';

const test = createTest(ApplicationScreen);

test.describe('Character Select', () => {
  test.beforeEach(async ({ page }) => {
    await page.waitForSelector('.app-root-controls', { state: 'attached' });
    await page.press('body', 'F2');
    await page.waitForSelector('.app-root-controls', { state: 'visible' });
    await page.click('//app-window[contains(@class, "app-root-menu")]/button[1]');
    await page.waitForSelector('app-connect');
  });

  test('shows loading, server list, empty, and error states', async ({ page }) => {
    await page.evaluate(() => {
      (window as any).skyrimtogether.requestCharacterList = () => {};
    });

    await page.locator('app-connect input').nth(0).fill('character-server');
    await page.locator('app-connect app-action-buttons button').nth(0).click();
    await expect(page.locator('[data-character-select-state="loading"]'))
      .toBeVisible();

    await page.evaluate(() => {
      (window as any).skyrimtogether.emit('characterList', [
        ['18446744073709551615', 'Server Character', '4294967295', '0', 1, 17],
      ]);
    });
    await expect(page.locator('[data-character-select-state="list"]'))
      .toBeVisible();
    await expect(page.locator('.character-list')).toContainText('Server Character');
    await expect(page.locator('.character-list')).toContainText('Level 17');

    await page.evaluate(() => {
      (window as any).skyrimtogether.emit('characterList', []);
    });
    await expect(page.locator('[data-character-select-state="empty"]'))
      .toContainText('No characters are available on this server');
    await expect(
      page.locator('app-character-select').getByRole('button', { name: /create/i }),
    ).toHaveCount(0);

    await page.evaluate(() => {
      (window as any).skyrimtogether.emit('characterSelectionResult', 2);
    });
    await expect(page.locator('[data-character-select-state="error"]'))
      .toContainText('This character is unavailable for this account');
  });
});
