import { Injectable } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';
import { ClientService } from './client.service';

export interface EntryAuth {
  state: SkyrimTogetherTypes.AuthState;
  displayName: string;
  avatarUrl: string;
  errorKey: string;
}

export interface EntryStage {
  stage: SkyrimTogetherTypes.LoadingStage;
  progress: number;
}

export interface CharacterSlot {
  index: number;
  locked: boolean;
  character: SkyrimTogetherTypes.CharacterSummaryBridge | null;
}

/**
 * Pre-world entry flow: launcher-driven connect, Discord auth, slot-based
 * character select/create and the no-save world load. The screen stays up
 * until the native side reports the 'done' stage.
 */
@Injectable({ providedIn: 'root' })
export class EntryService {
  public readonly active$ = new BehaviorSubject<boolean>(true);
  public readonly auth$ = new BehaviorSubject<EntryAuth>({
    state: 'connecting',
    displayName: '',
    avatarUrl: '',
    errorKey: '',
  });
  public readonly stage$ = new BehaviorSubject<EntryStage>({
    stage: 'connecting',
    progress: 0,
  });
  public readonly slots$ = new BehaviorSubject<CharacterSlot[] | null>(null);
  public readonly createResult$ = new Subject<SkyrimTogetherTypes.CharacterCreateStatus>();

  private slotTotal = 3;
  private slotUnlocked = 1;

  public constructor(
    private readonly client: ClientService,
  ) {
    if (typeof skyrimtogether === 'undefined') {
      return;
    }

    // The native bridge keeps one handler per event name and ClientService registers first,
    // so consume its streams instead of calling skyrimtogether.on() again here.
    this.client.authStateChange.subscribe(auth => this.auth$.next(auth));
    this.client.loadingStageChange.subscribe(({ stage, progress }) => {
      this.stage$.next({ stage, progress });
      // RaceMenu is a native Skyrim menu; the entry overlay must not cover it.
      if (stage === 'done' || stage === 'raceMenu') {
        this.active$.next(false);
      }
    });
    this.client.characterSlotsChange.subscribe(({ total, unlocked }) => {
      this.slotTotal = Math.max(1, total);
      this.slotUnlocked = Math.max(0, Math.min(unlocked, this.slotTotal));
    });
    this.client.characterCreateResultChange.subscribe(({ status }) =>
      this.createResult$.next(status),
    );

    this.client.characterListChange.subscribe(characters =>
      this.slots$.next(this.buildSlots(characters)),
    );
    this.client.connectionStateChange.subscribe(connected => {
      if (!connected && this.active$.getValue()) {
        this.slots$.next(null);
      }
    });
  }

  public requestCharacters(): void {
    this.slots$.next(null);
    this.client.requestCharacterList();
  }

  public select(characterId: SkyrimTogetherTypes.CharacterId): void {
    this.client.selectCharacter(characterId);
  }

  public create(slotIndex: number, name: string): void {
    skyrimtogether.createCharacter(slotIndex, name.trim());
  }

  public retry(): void {
    this.auth$.next({ state: 'connecting', displayName: '', avatarUrl: '', errorKey: '' });
    this.stage$.next({ stage: 'connecting', progress: 0 });
    skyrimtogether.retryConnect();
  }

  public quit(): void {
    skyrimtogether.quitGame();
  }

  private buildSlots(
    characters: SkyrimTogetherTypes.CharacterSummaryBridge[],
  ): CharacterSlot[] {
    const slots: CharacterSlot[] = [];
    for (let index = 0; index < this.slotTotal; index++) {
      slots.push({ index, locked: index >= this.slotUnlocked, character: null });
    }
    // Older servers send no slotIndex; fill free unlocked slots in order.
    const unplaced: SkyrimTogetherTypes.CharacterSummaryBridge[] = [];
    for (const character of characters) {
      const slot = character.slotIndex !== undefined ? slots[character.slotIndex] : undefined;
      if (slot && !slot.character) {
        slot.character = character;
      } else {
        unplaced.push(character);
      }
    }
    for (const character of unplaced) {
      const free = slots.find(slot => !slot.character);
      if (free) {
        free.character = character;
      }
    }
    return slots;
  }
}
