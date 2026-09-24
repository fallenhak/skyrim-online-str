import {
  AfterViewInit,
  Component,
  ElementRef,
  EventEmitter,
  HostListener,
  NgZone,
  OnDestroy,
  OnInit,
  Output,
  ViewChild,
} from '@angular/core';
import { Subscription } from 'rxjs';
import { ClientService } from '../../services/client.service';
import { Sound, SoundService } from '../../services/sound.service';

type CharacterSelectState = 'loading' | 'empty' | 'error' | 'list';
type NavigationDirection = 'up' | 'down';

interface GamepadActionState {
  up: boolean;
  down: boolean;
  confirm: boolean;
  back: boolean;
}

@Component({
  selector: 'app-character-select',
  templateUrl: './character-select.component.html',
  styleUrls: ['./character-select.component.scss'],
})
export class CharacterSelectComponent
  implements OnInit, AfterViewInit, OnDestroy
{
  public state: CharacterSelectState = 'loading';
  public characters: SkyrimTogetherTypes.CharacterSummaryBridge[] = [];
  public messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
  public pendingCharacterId: SkyrimTogetherTypes.CharacterId | null = null;
  public selectionFlowActive = false;

  @Output() public done = new EventEmitter<void>();

  @ViewChild('host') private host!: ElementRef<HTMLElement>;

  private readonly subscriptions: Subscription[] = [];
  private readonly previousGamepadState = new Map<number, GamepadActionState>();
  private focusFrameId: number | null = null;
  private gamepadFrameId: number | null = null;
  private returnFocus: HTMLElement | null = null;
  private destroyed = false;

  public constructor(
    private readonly client: ClientService,
    private readonly sound: SoundService,
    private readonly zone: NgZone,
  ) {}

  public ngOnInit(): void {
    this.subscriptions.push(
      this.client.characterListChange.subscribe(characters => {
        this.characters = characters;
        this.messageKey = '';
        this.state = characters.length === 0 ? 'empty' : 'list';
        this.scheduleFocus();
      }),
      this.client.characterSelectionResultChange.subscribe(status => {
        if (status === 0) {
          this.selectionFlowActive = true;
          this.characters = [];
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING_CHARACTER';
          this.state = 'loading';
          this.scheduleFocus(true);
          return;
        }

        this.selectionFlowActive = false;
        this.messageKey = this.selectionErrorKey(status);
        this.state = 'error';
        this.scheduleFocus();
      }),
      this.client.characterSessionStateChange.subscribe(sessionState => {
        switch (sessionState) {
          case 'characterSelected':
          case 'applyingCharacter':
          case 'awaitingClientReady':
          case 'awaitingPlayerAssignment':
            this.selectionFlowActive = true;
            this.characters = [];
            this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING_CHARACTER';
            this.state = 'loading';
            this.scheduleFocus(true);
            break;
          case 'inWorld':
            this.selectionFlowActive = false;
            this.finish();
            break;
          case 'awaitingCharacterSelection':
            if (this.selectionFlowActive) {
              this.selectionFlowActive = false;
              this.characters = [];
              this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
              this.state = 'loading';
              this.scheduleFocus(true);
              this.client.requestCharacterList();
            }
            break;
          case 'disconnected':
            this.selectionFlowActive = false;
            this.characters = [];
            this.messageKey = 'COMPONENT.CHARACTER_SELECT.DISCONNECTED';
            this.state = 'error';
            this.scheduleFocus();
            break;
        }
      }),
      this.client.characterSelectionPendingIdChange.subscribe(characterId => {
        this.pendingCharacterId = characterId;
        if (characterId !== null) {
          this.scheduleFocus(true);
        }
      }),
      this.client.characterUiResetChange.subscribe(reason => {
        this.characters = [];
        this.selectionFlowActive = false;

        if (reason === 'connecting') {
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
          this.state = 'loading';
        } else if (reason === 'disconnected') {
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.DISCONNECTED';
          this.state = 'error';
        } else {
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.ERROR.GENERIC';
          this.state = 'error';
        }

        this.scheduleFocus(reason === 'connecting');
      }),
      this.client.connectionStateChange.subscribe(connected => {
        this.characters = [];
        if (connected) {
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
          this.state = 'loading';
          this.client.requestCharacterList();
        } else {
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.DISCONNECTED';
          this.state = 'error';
        }
        this.scheduleFocus();
      }),
    );
  }

  public ngAfterViewInit(): void {
    const activeElement = document.activeElement;
    if (
      activeElement instanceof HTMLElement &&
      activeElement !== document.body &&
      !this.host.nativeElement.contains(activeElement)
    ) {
      this.returnFocus = activeElement;
    }

    this.scheduleFocus();
    this.zone.runOutsideAngular(() => this.pollGamepads());
  }

  public ngOnDestroy(): void {
    this.destroyed = true;
    this.subscriptions.forEach(subscription => subscription.unsubscribe());

    if (this.focusFrameId !== null) {
      window.cancelAnimationFrame(this.focusFrameId);
    }
    if (this.gamepadFrameId !== null) {
      window.cancelAnimationFrame(this.gamepadFrameId);
    }
  }

  public close(): void {
    if (this.selectionFlowActive || this.pendingCharacterId !== null) {
      return;
    }

    this.sound.play(Sound.Cancel);
    this.finish();
  }

  public trackByCharacterId(
    _index: number,
    character: SkyrimTogetherTypes.CharacterSummaryBridge,
  ): SkyrimTogetherTypes.CharacterId {
    return character.characterId;
  }

  public selectCharacter(characterId: SkyrimTogetherTypes.CharacterId): void {
    if (this.state !== 'list' || this.pendingCharacterId !== null) {
      return;
    }

    this.client.selectCharacter(characterId);
  }

  @HostListener('window:keydown', ['$event'])
  private onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      this.close();
      event.stopPropagation();
      event.preventDefault();
      return;
    }

    if (event.key === 'Tab') {
      this.keepTabFocusInside(event);
      return;
    }

    if (event.altKey || event.ctrlKey || event.metaKey) {
      return;
    }

    const direction = this.getNavigationDirection(event);
    if (direction) {
      this.moveFocus(direction);
      event.stopPropagation();
      event.preventDefault();
    }
  }

  private getFocusableButtons(): HTMLButtonElement[] {
    return Array.from(
      this.host.nativeElement.querySelectorAll<HTMLButtonElement>(
        'button:not(:disabled)',
      ),
    );
  }

  private scheduleFocus(focusFirst = false): void {
    if (this.focusFrameId !== null) {
      window.cancelAnimationFrame(this.focusFrameId);
    }

    this.focusFrameId = window.requestAnimationFrame(() => {
      this.focusFrameId = null;
      const host = this.host.nativeElement;
      if (!host.isConnected) {
        return;
      }

      const activeElement = document.activeElement;
      const activeButton = activeElement instanceof HTMLButtonElement;
      if (
        !focusFirst &&
        host.contains(activeElement) &&
        activeButton &&
        !activeElement.disabled
      ) {
        return;
      }

      const firstButton = this.getFocusableButtons()[0];
      (firstButton ?? this.host.nativeElement).focus();
    });
  }

  private keepTabFocusInside(event: KeyboardEvent): void {
    const buttons = this.getFocusableButtons();
    if (buttons.length === 0) {
      this.host.nativeElement.focus();
      event.preventDefault();
      return;
    }

    const activeIndex = buttons.indexOf(
      document.activeElement as HTMLButtonElement,
    );
    const needsWrap = event.shiftKey
      ? activeIndex <= 0
      : activeIndex === -1 || activeIndex === buttons.length - 1;

    if (needsWrap) {
      buttons[event.shiftKey ? buttons.length - 1 : 0].focus();
      event.preventDefault();
    }
  }

  private getNavigationDirection(
    event: KeyboardEvent,
  ): NavigationDirection | null {
    const key = event.key.toLowerCase();
    if (
      key === 'arrowup' ||
      key === 'w' ||
      key === '8' ||
      event.code === 'Numpad8'
    ) {
      return 'up';
    }
    if (
      key === 'arrowdown' ||
      key === 's' ||
      key === '2' ||
      event.code === 'Numpad2'
    ) {
      return 'down';
    }
    return null;
  }

  private moveFocus(direction: NavigationDirection): void {
    const buttons = this.getFocusableButtons();
    if (buttons.length === 0) {
      this.host.nativeElement.focus();
      return;
    }

    const activeIndex = buttons.indexOf(
      document.activeElement as HTMLButtonElement,
    );
    const nextIndex =
      activeIndex === -1
        ? direction === 'down'
          ? 0
          : buttons.length - 1
        : (activeIndex + (direction === 'down' ? 1 : -1) + buttons.length) %
          buttons.length;
    buttons[nextIndex].focus();
  }

  private pollGamepads = (): void => {
    if (this.destroyed) {
      return;
    }
    this.gamepadFrameId = window.requestAnimationFrame(this.pollGamepads);

    const connectedGamepads = new Set<number>();
    for (const gamepad of Array.from(navigator.getGamepads?.() ?? [])) {
      if (!gamepad?.connected) {
        continue;
      }

      connectedGamepads.add(gamepad.index);
      const nextState: GamepadActionState = {
        up:
          this.isGamepadButtonPressed(gamepad, 12) ||
          (gamepad.axes[1] ?? 0) < -0.55,
        down:
          this.isGamepadButtonPressed(gamepad, 13) ||
          (gamepad.axes[1] ?? 0) > 0.55,
        confirm: this.isGamepadButtonPressed(gamepad, 0),
        back:
          this.isGamepadButtonPressed(gamepad, 1) ||
          this.isGamepadButtonPressed(gamepad, 8),
      };
      const previousState = this.previousGamepadState.get(gamepad.index);
      this.previousGamepadState.set(gamepad.index, nextState);

      // Ignore controls already held when the screen appears. Require a fresh
      // press so opening Character Select cannot immediately submit a choice.
      if (!previousState) {
        continue;
      }

      if (nextState.up && !previousState.up) {
        this.zone.run(() => this.moveFocus('up'));
      }
      if (nextState.down && !previousState.down) {
        this.zone.run(() => this.moveFocus('down'));
      }
      if (nextState.confirm && !previousState.confirm) {
        this.zone.run(() => this.activateFocusedButton());
      }
      if (this.destroyed) {
        return;
      }
      if (nextState.back && !previousState.back) {
        this.zone.run(() => this.close());
      }
    }

    for (const index of this.previousGamepadState.keys()) {
      if (!connectedGamepads.has(index)) {
        this.previousGamepadState.delete(index);
      }
    }
  };

  private isGamepadButtonPressed(gamepad: Gamepad, index: number): boolean {
    return gamepad.buttons[index]?.pressed ?? false;
  }

  private activateFocusedButton(): void {
    const buttons = this.getFocusableButtons();
    const activeElement = document.activeElement;
    const focusedButton = buttons.find(button => button === activeElement);
    (focusedButton ?? buttons[0])?.click();
  }

  private finish(): void {
    const returnFocus = this.returnFocus;
    this.done.emit();

    window.requestAnimationFrame(() => {
      if (returnFocus?.isConnected) {
        returnFocus.focus();
        return;
      }

      document
        .querySelector<HTMLButtonElement>(
          '[data-character-select-trigger="true"]',
        )
        ?.focus();
    });
  }

  private selectionErrorKey(
    status: SkyrimTogetherTypes.CharacterSelectionStatus,
  ): string {
    switch (status) {
      case 1:
        return 'COMPONENT.CHARACTER_SELECT.ERROR.IDENTITY_NOT_READY';
      case 2:
        return 'COMPONENT.CHARACTER_SELECT.ERROR.UNAVAILABLE';
      case 3:
        return 'COMPONENT.CHARACTER_SELECT.ERROR.INVALID_STATE';
      default:
        return 'COMPONENT.CHARACTER_SELECT.ERROR.GENERIC';
    }
  }
}
