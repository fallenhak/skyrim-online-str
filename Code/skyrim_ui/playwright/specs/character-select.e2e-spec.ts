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
      const client = (window as any).skyrimtogether;
      client.emit(
        'characterList',
        [
          [
            '18446744073709551615',
            'Server Character',
            '4294967295',
            '0',
            1,
            17,
          ],
        ],
        client.characterConnectionGeneration,
      );
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
      const client = (window as any).skyrimtogether;
      client.emit('characterList', [], client.characterConnectionGeneration);
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

  test(
    'submits the server character ID once and disables selection while pending',
    async ({ page }) => {
      await page.evaluate(() => {
        const client = (window as any).skyrimtogether;
        client.requestCharacterList = () => {};
        (window as any).selectedCharacterIds = [];
        client.selectCharacter = (characterId: string) => {
          (window as any).selectedCharacterIds.push(characterId);
        };
      });

      await page.locator('app-connect input').nth(0).fill('character-server');
      await page.locator('app-connect app-action-buttons button').nth(0).click();

      const characterSelect = page.locator('app-character-select');
      await expect(
        characterSelect.locator('[data-character-select-state="loading"]'),
      ).toBeVisible();

      const selectedCharacterId = '18446744073709551615';
      await page.evaluate(characterId => {
        const client = (window as any).skyrimtogether;
        client.emit(
          'characterList',
          [
            [characterId, 'First Server Character', '0', '0', 0, 1],
            ['200', 'Second Server Character', '0', '0', 1, 12],
          ],
          client.characterConnectionGeneration,
        );
      }, selectedCharacterId);

      const characterButtons = characterSelect.locator(
        '.character-select-action',
      );
      await expect(characterButtons).toHaveCount(2);
      await characterButtons.nth(0).click();

      await expect(characterButtons.nth(0)).toBeDisabled();
      await expect(characterButtons.nth(1)).toBeDisabled();
      await expect(
        characterSelect.getByRole('button', { name: /selecting/i }),
      ).toBeVisible();

      // A duplicate DOM activation while the request is pending must not submit
      // the same server-owned character ID a second time.
      await characterButtons.nth(0).evaluate((button: HTMLButtonElement) => {
        button.click();
      });
      await expect
        .poll(() => page.evaluate(() => (window as any).selectedCharacterIds))
        .toEqual([selectedCharacterId]);
    },
  );

  test(
    'clears old server data on errors and connection changes',
    async ({ page }) => {
      await page.evaluate(() => {
        (window as any).skyrimtogether.requestCharacterList = () => {};
      });

      await page.locator('app-connect input').nth(0).fill('character-server');
      await page
        .locator('app-connect app-action-buttons button')
        .nth(0)
        .click();

      const characterSelect = page.locator('app-character-select');
      await expect(
        characterSelect.locator('[data-character-select-state="loading"]'),
      ).toBeVisible();

      const oldConnectionGeneration = await page.evaluate(
        () => (window as any).skyrimtogether.characterConnectionGeneration,
      );
      await page.evaluate(() => {
        const client = (window as any).skyrimtogether;
        client.emit(
          'characterList',
          [['100', 'Old Connection Character', '0', '0', 0, 1]],
          client.characterConnectionGeneration,
        );
      });
      await expect(
        characterSelect.locator('[data-character-id="100"]'),
      ).toBeVisible();

      await page.evaluate(() => {
        (window as any).skyrimtogether.emit(
          'triggerError',
          JSON.stringify({ error: 'server_full' }),
        );
      });
      await expect(
        characterSelect.locator('[data-character-select-state="error"]'),
      ).toBeVisible();
      await expect(
        characterSelect.locator('.character-list li'),
      ).toHaveCount(0);

      await page.evaluate(generation => {
        const client = (window as any).skyrimtogether;
        client.emit(
          'characterList',
          [['100', 'Old Connection Character', '0', '0', 0, 1]],
          generation,
        );
      }, oldConnectionGeneration);
      await expect(
        characterSelect.locator('.character-list li'),
      ).toHaveCount(0);

      await page.evaluate(() => {
        const client = (window as any).skyrimtogether;
        client.characterConnectionGeneration += 1;
        client.emit('connect', client.characterConnectionGeneration);
      });

      await expect(
        characterSelect.locator('[data-character-select-state="loading"]'),
      ).toBeVisible();
      await page.evaluate(generation => {
        // A delayed notification from the previous session must not restore it.
        const client = (window as any).skyrimtogether;
        client.emit(
          'characterList',
          [['100', 'Old Connection Character', '0', '0', 0, 1]],
          generation,
        );
      }, oldConnectionGeneration);
      await expect(
        characterSelect.locator('.character-list li'),
      ).toHaveCount(0);
      await page.evaluate(() => {
        const client = (window as any).skyrimtogether;
        client.emit(
          'characterList',
          [['200', 'New Connection Character', '0', '0', 0, 2]],
          client.characterConnectionGeneration,
        );
      });
      await expect(
        characterSelect.locator('[data-character-id="200"]'),
      ).toBeVisible();
      await expect(
        characterSelect.locator('[data-character-id="100"]'),
      ).toHaveCount(0);

      await page.evaluate(() => {
        (window as any).skyrimtogether.disconnect();
      });
      await expect(
        characterSelect.locator('[data-character-select-state="error"]'),
      ).toBeVisible();
      await expect(
        characterSelect.locator('.character-list li'),
      ).toHaveCount(0);
    },
  );

  test(
    'keeps selection open until the server confirms the character is in world',
    async ({ page }) => {
      await page.locator('app-connect input').nth(0).fill('character-server');
      await page.locator('app-connect app-action-buttons button').nth(0).click();

      await page.evaluate(() => {
        (window as any).skyrimtogether.selectCharacter = () => {};
      });

      const characterSelect = page.locator('app-character-select');
      await expect(
        characterSelect.locator('[data-character-select-state="list"]'),
      ).toBeVisible();
      await characterSelect.locator('.character-select-action').click();
      await expect(
        characterSelect.getByRole('button', { name: /selecting/i }),
      ).toBeVisible();
      await expect(
        characterSelect.getByRole('button', { name: /back/i }),
      ).toHaveCount(0);
      await page.keyboard.press('Escape');
      await expect(characterSelect).toBeVisible();

      await page.evaluate(() => {
        (window as any).skyrimtogether.emit('characterSelectionResult', 0);
      });
      await expect(
        characterSelect.locator('[data-character-select-state="loading"]'),
      ).toContainText('The server accepted the selection');
      await expect(
        characterSelect.getByRole('button', { name: /back/i }),
      ).toHaveCount(0);

      for (const state of [
        'characterSelected',
        'applyingCharacter',
        'awaitingClientReady',
        'awaitingPlayerAssignment',
      ]) {
        await page.evaluate(sessionState => {
          (window as any).skyrimtogether.emit(
            'characterSessionState',
            sessionState,
          );
        }, state);
        await expect(characterSelect).toBeVisible();
      }

      await page.evaluate(() => {
        (window as any).skyrimtogether.emit(
          'characterSessionState',
          'awaitingCharacterSelection',
        );
      });
      await expect(
        characterSelect.locator('[data-character-select-state="list"]'),
      ).toBeVisible();
      await expect(
        characterSelect.getByRole('button', { name: /back/i }),
      ).toBeVisible();

      await characterSelect.locator('.character-select-action').click();
      await expect(
        characterSelect.getByRole('button', { name: /selecting/i }),
      ).toBeVisible();
      await page.evaluate(() => {
        (window as any).skyrimtogether.emit('characterSelectionResult', 0);
      });

      await page.evaluate(() => {
        (window as any).skyrimtogether.emit(
          'characterSessionState',
          'inWorld',
        );
      });
      await expect(characterSelect).toHaveCount(0);
    },
  );

  test('supports keyboard and controller focus, confirm, and back actions', async ({
    page,
  }) => {
    await page.locator('app-connect input').nth(0).fill('character-server');
    await page.locator('app-connect app-action-buttons button').nth(0).click();

    const characterSelect = page.locator('app-character-select');
    const characterButtons = characterSelect.locator('.character-select-action');
    await expect(characterSelect.locator('[data-character-select-state="list"]'))
      .toBeVisible();

    await page.evaluate(() => {
      const client = (window as any).skyrimtogether;
      client.emit(
        'characterList',
        [
          ['100', 'First Server Character', '0', '0', 0, 1],
          ['200', 'Second Server Character', '0', '0', 1, 12],
        ],
        client.characterConnectionGeneration,
      );
      client.selectCharacter = (characterId: string) => {
        (window as any).selectedServerCharacterId = characterId;
      };
    });

    await expect(characterButtons).toHaveCount(2);
    await expect(characterButtons.nth(0)).toBeFocused();

    const backButton = characterSelect.getByRole('button', { name: /back/i });
    await page.keyboard.press('Shift+Tab');
    await expect(backButton).toBeFocused();
    await page.keyboard.press('Tab');
    await expect(characterButtons.nth(0)).toBeFocused();
    await page.keyboard.press('ArrowDown');
    await expect(characterButtons.nth(1)).toBeFocused();

    await page.evaluate(() => {
      const gamepad = {
        axes: [0, 0],
        buttons: Array.from({ length: 16 }, () => ({
          pressed: false,
          touched: false,
          value: 0,
        })),
        connected: true,
        id: 'Character Select test pad',
        index: 0,
        mapping: 'standard',
        timestamp: 0,
      };
      Object.defineProperty(navigator, 'getGamepads', {
        configurable: true,
        value: () => [gamepad],
      });
      (window as any).testCharacterSelectGamepad = gamepad;
    });
    await page.waitForTimeout(100);

    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.axes[1] = -1;
    });
    await expect(characterButtons.nth(0)).toBeFocused();
    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.axes[1] = 0;
    });
    await page.waitForTimeout(50);

    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.buttons[13].pressed = true;
    });
    await expect(characterButtons.nth(1)).toBeFocused();
    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.buttons[13].pressed = false;
    });
    await page.waitForTimeout(50);

    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.buttons[0].pressed = true;
    });
    await expect(
      characterSelect.getByRole('button', { name: /selecting/i }),
    ).toBeVisible();
    expect(
      await page.evaluate(() => (window as any).selectedServerCharacterId),
    ).toBe('200');
    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.buttons[0].pressed = false;
    });
    await page.waitForTimeout(50);

    await page.keyboard.press('Escape');
    await expect(characterSelect).toBeVisible();
    await expect(characterSelect.locator('section')).toBeFocused();

    await page.evaluate(() => {
      (window as any).skyrimtogether.emit('characterSelectionResult', 2);
    });
    await expect(backButton).toBeFocused();
    await page.evaluate(() => {
      (window as any).testCharacterSelectGamepad.buttons[1].pressed = true;
    });
    await expect(characterSelect).toHaveCount(0);
    await expect(
      page.locator('[data-character-select-trigger="true"]'),
    ).toBeFocused();

    await page.evaluate(() => {
      const client = (window as any).skyrimtogether;
      const deactivate = client.deactivate.bind(client);
      client.deactivate = () => {
        (window as any).escapeDeactivatedOverlay = true;
        deactivate();
      };
    });
    await page.keyboard.press('Escape');
    expect(
      await page.evaluate(() => (window as any).escapeDeactivatedOverlay),
    ).toBe(true);
  });
});
