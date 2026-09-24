/* Entry flow bridge (Discord auth, loading stages, character slots/creation). */
declare namespace SkyrimTogetherTypes {
  type AuthState = 'connecting' | 'authenticating' | 'authenticated' | 'failed';

  // LoadingStage, CharacterCreateStatus, slot/create callbacks and
  // CharacterSummaryBridge.slotIndex are declared in typings.d.ts.

  type AuthStateCallback = (
    state: AuthState,
    displayName: string,
    avatarUrl: string,
    errorKey: string,
  ) => void;
}

interface SkyrimTogether {
  on(event: 'authState', callback: SkyrimTogetherTypes.AuthStateCallback): void;
  on(
    event: 'loadingStage',
    callback: SkyrimTogetherTypes.LoadingStageCallback,
  ): void;
  on(
    event: 'characterSlots',
    callback: SkyrimTogetherTypes.CharacterSlotsCallback,
  ): void;
  on(
    event: 'characterCreateResult',
    callback: SkyrimTogetherTypes.CharacterCreateResultCallback,
  ): void;
  createCharacter(slotIndex: number, name: string): void;
  retryConnect(): void;
  quitGame(): void;
}
