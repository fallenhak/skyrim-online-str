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
    const character = page.locator('.character-list li');
    await expect(character).toHaveAttribute(
      'data-character-id',
      '18446744073709551615',
    );
    await expect(character).toContainText('Server Character');
    await expect(character).toContainText('Level 17');
    await expect(character).toContainText(
      'Race form (base 4294967295, mod 0)',
    );
    await expect(character).toContainText('Sex 1');

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
