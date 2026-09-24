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

  public constructor(
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
      if (event.key === 'Escape') {
        this.cancelCreate();
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
      this.activate(this.slots[this.focusedSlot]);
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
