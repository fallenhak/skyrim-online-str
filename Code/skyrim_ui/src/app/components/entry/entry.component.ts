import { TranslocoService } from '@ngneat/transloco';
import {
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  HostListener,
  OnDestroy,
  OnInit,
  ViewChild,
} from '@angular/core';
import { combineLatest, Subscription } from 'rxjs';
import { map } from 'rxjs/operators';
import { CharacterSlot, EntryService } from '../../services/entry.service';
import { Sound, SoundService } from '../../services/sound.service';

type EntryView = 'loading' | 'failed' | 'select' | 'create';

const NAME_PATTERN = /^[A-Za-zÇĞİÖŞÜçğıöşüÂâÎîÛû' -]{3,24}$/;

/** Skyrim.esm playable race base form ids → i18n key suffix. */
const RACES: Record<number, string> = {
  0x13740: 'ARGONIAN',
  0x13741: 'BRETON',
  0x13742: 'DARK_ELF',
  0x13743: 'HIGH_ELF',
  0x13744: 'IMPERIAL',
  0x13745: 'KHAJIIT',
  0x13746: 'NORD',
  0x13747: 'ORC',
  0x13748: 'REDGUARD',
  0x13749: 'WOOD_ELF',
};

const CREATE_ERRORS: Record<number, string> = {
  1: 'NAME_INVALID',
  2: 'NAME_TAKEN',
  3: 'SLOT_LOCKED',
  4: 'SLOT_OCCUPIED',
  5: 'CREATE_FAILED',
};

const TIP_COUNT = 6;

const SMOKE_PUFFS = [{"v": 0, "x": -14.0, "y": -10.1, "w": 51.8, "h": 32.1, "o": 0.49, "r0": 13, "r1": -19, "d1": 67, "d2": 12, "dl": -41}, {"v": 1, "x": -9.7, "y": -18.6, "w": 37.8, "h": 23.4, "o": 0.47, "r0": 4, "r1": 14, "d1": 44, "d2": 12, "dl": 0}, {"v": 2, "x": 4.5, "y": -22.4, "w": 43.8, "h": 27.2, "o": 0.47, "r0": 4, "r1": -10, "d1": 42, "d2": 11, "dl": -39}, {"v": 0, "x": 12.5, "y": -26.0, "w": 37.0, "h": 23.0, "o": 0.47, "r0": -7, "r1": -7, "d1": 48, "d2": 11, "dl": -18}, {"v": 1, "x": 18.9, "y": -8.5, "w": 38.8, "h": 24.0, "o": 0.42, "r0": -9, "r1": -8, "d1": 62, "d2": 13, "dl": -1}, {"v": 2, "x": 28.1, "y": -23.1, "w": 38.0, "h": 23.5, "o": 0.29, "r0": -1, "r1": 18, "d1": 38, "d2": 14, "dl": -4}, {"v": 0, "x": 36.6, "y": -16.4, "w": 53.6, "h": 33.3, "o": 0.35, "r0": 10, "r1": 10, "d1": 49, "d2": 9, "dl": -16}, {"v": 1, "x": 50.7, "y": -11.0, "w": 34.5, "h": 21.4, "o": 0.47, "r0": -19, "r1": 15, "d1": 64, "d2": 14, "dl": -24}, {"v": 2, "x": 56.4, "y": -25.1, "w": 34.2, "h": 21.2, "o": 0.32, "r0": -8, "r1": -13, "d1": 53, "d2": 16, "dl": -22}, {"v": 0, "x": 64.7, "y": -21.0, "w": 55.4, "h": 34.4, "o": 0.38, "r0": 17, "r1": 3, "d1": 56, "d2": 9, "dl": -27}, {"v": 1, "x": 77.0, "y": -19.2, "w": 36.2, "h": 22.4, "o": 0.41, "r0": -11, "r1": 1, "d1": 55, "d2": 17, "dl": -5}, {"v": 2, "x": 80.8, "y": -22.5, "w": 41.6, "h": 25.8, "o": 0.3, "r0": -11, "r1": -1, "d1": 68, "d2": 11, "dl": -46}, {"v": 0, "x": 87.5, "y": -15.3, "w": 57.7, "h": 35.8, "o": 0.37, "r0": -5, "r1": 18, "d1": 60, "d2": 13, "dl": -29}, {"v": 1, "x": 101.2, "y": -6.4, "w": 37.5, "h": 23.2, "o": 0.42, "r0": 11, "r1": 1, "d1": 51, "d2": 11, "dl": -46}, {"v": 0, "x": 3.3, "y": 16.8, "w": 37.9, "h": 18.9, "o": 0.16, "r0": -10, "r1": -2, "d1": 61, "d2": 14, "dl": -3}, {"v": 1, "x": 19.5, "y": 11.4, "w": 41.8, "h": 20.9, "o": 0.22, "r0": 12, "r1": -10, "d1": 66, "d2": 19, "dl": -31}, {"v": 2, "x": 21.8, "y": 9.3, "w": 39.6, "h": 19.8, "o": 0.17, "r0": 13, "r1": -11, "d1": 53, "d2": 18, "dl": -52}, {"v": 0, "x": 86.0, "y": 13.5, "w": 43.8, "h": 21.9, "o": 0.17, "r0": -5, "r1": -10, "d1": 52, "d2": 19, "dl": -17}, {"v": 1, "x": 44.8, "y": 12.0, "w": 37.2, "h": 18.6, "o": 0.13, "r0": 10, "r1": 9, "d1": 61, "d2": 12, "dl": -48}, {"v": 2, "x": 45.9, "y": 14.0, "w": 34.3, "h": 17.1, "o": 0.2, "r0": -7, "r1": 9, "d1": 72, "d2": 16, "dl": -60}] as const;

@Component({
  selector: 'app-entry',
  templateUrl: './entry.component.html',
  styleUrls: ['./entry.component.scss'],
  changeDetection: ChangeDetectionStrategy.Default,
})
export class EntryComponent implements OnInit, OnDestroy {
  public view: EntryView = 'loading';
  public slots: CharacterSlot[] = [];
  public focusedSlot = 0;
  public createSlot: number | null = null;
  public name = '';
  public nameErrorKey = '';
  public busy = false;
  public tipIndex = Math.floor(Math.random() * TIP_COUNT);

  public readonly auth$ = this.entry.auth$;
  public readonly stage$ = this.entry.stage$;
  public readonly progressPercent$ = this.entry.stage$.pipe(
    map(stage => Math.round(stage.progress * 100)),
  );

  @ViewChild('nameInput') private nameInput?: ElementRef<HTMLInputElement>;

  private readonly subscriptions: Subscription[] = [];
  private tipTimer: ReturnType<typeof setInterval> | null = null;

  public readonly lang$ = this.transloco.langChanges$;

  public constructor(
    private readonly transloco: TranslocoService,
    private readonly entry: EntryService,
    private readonly sound: SoundService,
  ) {}

  public ngOnInit(): void {
    this.subscriptions.push(
      combineLatest([this.entry.auth$, this.entry.stage$, this.entry.slots$]).subscribe(
        ([auth, stage, slots]) => this.resolveView(auth.state, stage.stage, slots),
      ),
      this.entry.auth$.subscribe(auth => {
        if (auth.state === 'authenticated' && !this.entry.slots$.getValue()) {
          this.entry.requestCharacters();
        }
      }),
      this.entry.createResult$.subscribe(status => {
        this.busy = false;
        if (status === 0) {
          this.createSlot = null;
          return;
        }
        this.nameErrorKey = `COMPONENT.ENTRY.ERROR.${CREATE_ERRORS[status] ?? 'CREATE_FAILED'}`;
        this.sound.play(Sound.Fail);
      }),
    );
    this.tipTimer = setInterval(() => (this.tipIndex = (this.tipIndex + 1) % TIP_COUNT), 9000);
  }

  public ngOnDestroy(): void {
    this.subscriptions.forEach(subscription => subscription.unsubscribe());
    if (this.tipTimer) {
      clearInterval(this.tipTimer);
    }
  }

  public get tipKey(): string {
    return `COMPONENT.ENTRY.TIP.${this.tipIndex}`;
  }

  public stageKey(stage: SkyrimTogetherTypes.LoadingStage): string {
    return `COMPONENT.ENTRY.STAGE.${stage}`;
  }

  public raceKey(character: SkyrimTogetherTypes.CharacterSummaryBridge): string {
    const raw = String(character.race.baseId);
    const id = raw.startsWith('0x') ? parseInt(raw, 16) : parseInt(raw, 10);
    return `COMPONENT.ENTRY.RACE.${RACES[id & 0xffffff] ?? 'UNKNOWN'}`;
  }

  public sexKey(character: SkyrimTogetherTypes.CharacterSummaryBridge): string {
    return character.sex === 1 ? 'COMPONENT.ENTRY.SEX.FEMALE' : 'COMPONENT.ENTRY.SEX.MALE';
  }

  /** Detail column is closed until the player picks a slot (like the vanilla confirm column). */
  public detailOpen = false;

  public readonly puffs = SMOKE_PUFFS;

  public openDetail(slot: CharacterSlot): void {
    if (this.busy) {
      return;
    }
    this.focus(slot);
    if (!this.detailOpen) {
      this.sound.play(Sound.Ok);
    }
    this.detailOpen = true;
  }

  public closeDetail(): void {
    if (this.detailOpen && !this.busy) {
      this.detailOpen = false;
      this.sound.play(Sound.Cancel);
    }
  }

  public get focusedSlotData(): CharacterSlot | undefined {
    return this.slots[this.focusedSlot];
  }

  public focus(slot: CharacterSlot): void {
    if (this.focusedSlot !== slot.index) {
      this.focusedSlot = slot.index;
      this.sound.play(Sound.Focus);
    }
  }

  public activate(slot: CharacterSlot): void {
    if (this.busy) {
      return;
    }
    if (slot.character) {
      this.busy = true;
      this.sound.play(Sound.Ok);
      this.entry.select(slot.character.characterId);
      return;
    }
    if (slot.locked) {
      this.sound.play(Sound.Fail);
      return;
    }
    this.sound.play(Sound.Ok);
    this.createSlot = slot.index;
    this.name = '';
    this.nameErrorKey = '';
    this.view = 'create';
    setTimeout(() => this.nameInput?.nativeElement.focus());
  }

  public submitCreate(): void {
    if (this.busy || this.createSlot === null) {
      return;
    }
    const name = this.name.trim().replace(/\s+/g, ' ');
    if (!NAME_PATTERN.test(name)) {
      this.nameErrorKey = 'COMPONENT.ENTRY.ERROR.NAME_INVALID';
      this.sound.play(Sound.Fail);
      return;
    }
    this.busy = true;
    this.nameErrorKey = '';
    this.entry.create(this.createSlot, name);
  }

  public cancelCreate(): void {
    if (this.busy) {
      return;
    }
    this.sound.play(Sound.Cancel);
    this.createSlot = null;
    this.detailOpen = false;
    this.view = 'select';
  }

  public retry(): void {
    this.busy = false;
    this.entry.retry();
  }

  public quit(): void {
    this.entry.quit();
  }

  @HostListener('document:keydown', ['$event'])
  public onKey(event: KeyboardEvent): void {
    if (this.view === 'create') {
      if (event.key === 'Escape' || event.key === 'Tab') {
        event.preventDefault();
        this.cancelCreate();
      }
      return;
    }
    if (this.view === 'failed') {
      if (event.key === 'Enter') {
        this.retry();
      } else if (event.key === 'Tab' || event.key === 'Escape') {
        event.preventDefault();
        this.quit();
      }
      return;
    }
    if (this.view !== 'select' || this.slots.length === 0) {
      return;
    }
    const step = { ArrowLeft: -1, ArrowUp: -1, ArrowRight: 1, ArrowDown: 1 }[event.key];
    if (step) {
      event.preventDefault();
      this.focus(this.slots[(this.focusedSlot + step + this.slots.length) % this.slots.length]);
    } else if (event.key === 'Enter') {
      if (this.detailOpen) {
        this.activate(this.slots[this.focusedSlot]);
      } else {
        this.openDetail(this.slots[this.focusedSlot]);
      }
    } else if (event.key === 'Tab' || event.key === 'Escape') {
      event.preventDefault();
      this.closeDetail();
    }
  }

  public trackBySlot(_: number, slot: CharacterSlot): number {
    return slot.index;
  }

  private resolveView(
    auth: SkyrimTogetherTypes.AuthState,
    stage: SkyrimTogetherTypes.LoadingStage,
    slots: CharacterSlot[] | null,
  ): void {
    if (auth === 'failed') {
      this.busy = false;
      this.view = 'failed';
      return;
    }
    // The native side selects the same character again; no selection screen while reconnecting.
    if (this.entry.reconnecting$.getValue()) {
      this.view = 'loading';
      return;
    }
    const worldBound = ['creatingCharacter', 'loadingWorld', 'applyingCharacter', 'raceMenu', 'enteringWorld', 'done'];
    if (worldBound.includes(stage) && stage !== 'creatingCharacter') {
      this.view = 'loading';
      return;
    }
    if (this.view === 'create') {
      return;
    }
    if (slots && auth === 'authenticated') {
      this.slots = slots;
      const current = slots[this.focusedSlot];
      if (this.view !== 'select' || !current || (current.locked && !current.character)) {
        const first = slots.find(slot => slot.character) ?? slots.find(slot => !slot.locked);
        this.focusedSlot = first ? first.index : 0;
      }
      this.view = this.busy ? 'loading' : 'select';
      return;
    }
    this.view = 'loading';
  }
}
