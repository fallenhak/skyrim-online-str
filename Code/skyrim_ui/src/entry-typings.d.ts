/* Entry flow bridge (Discord auth, loading stages, character slots/creation). */
declare namespace SkyrimTogetherTypes {
  type AuthState = 'connecting' | 'authenticating' | 'authenticated' | 'failed';

  type LoadingStage =
    | 'connecting'
    | 'authenticating'
    | 'fetchingCharacters'
    | 'creatingCharacter'
    | 'loadingWorld'
    | 'applyingCharacter'
    | 'raceMenu'
    | 'enteringWorld'
    | 'done';

  /** 0 ok, 1 nameInvalid, 2 nameTaken, 3 slotLocked, 4 slotOccupied, 5 error */
  type CharacterCreateStatus = 0 | 1 | 2 | 3 | 4 | 5;

  interface CharacterSummaryBridge {
    slotIndex?: number;
  }

  type AuthStateCallback = (
    state: AuthState,
    displayName: string,
    avatarUrl: string,
    errorKey: string,
  ) => void;
  type LoadingStageCallback = (stage: LoadingStage, progress: number) => void;
  type CharacterSlotsCallback = (total: number, unlocked: number) => void;
  type CharacterCreateResultCallback = (
    status: CharacterCreateStatus,
    characterId: CharacterId,
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
