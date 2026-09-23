import {
  Component,
  EventEmitter,
  HostListener,
  OnDestroy,
  OnInit,
  Output,
} from '@angular/core';
import { Subscription } from 'rxjs';
import { ClientService } from '../../services/client.service';
import { Sound, SoundService } from '../../services/sound.service';

type CharacterSelectState = 'loading' | 'empty' | 'error' | 'list';

@Component({
  selector: 'app-character-select',
  templateUrl: './character-select.component.html',
  styleUrls: ['./character-select.component.scss'],
})
export class CharacterSelectComponent implements OnInit, OnDestroy {
  public state: CharacterSelectState = 'loading';
  public characters: SkyrimTogetherTypes.CharacterSummaryBridge[] = [];
  public messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
  public pendingCharacterId: SkyrimTogetherTypes.CharacterId | null = null;
  public selectionFlowActive = false;

  @Output() public done = new EventEmitter<void>();

  private readonly subscriptions: Subscription[] = [];

  public constructor(
    private readonly client: ClientService,
    private readonly sound: SoundService,
  ) {}

  public ngOnInit(): void {
    this.subscriptions.push(
      this.client.characterListChange.subscribe(characters => {
        this.characters = characters;
        this.messageKey = '';
        this.state = characters.length === 0 ? 'empty' : 'list';
      }),
      this.client.characterSelectionResultChange.subscribe(status => {
        if (status === 0) {
          this.selectionFlowActive = true;
          this.characters = [];
          this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING_CHARACTER';
          this.state = 'loading';
          return;
        }

        this.selectionFlowActive = false;
        this.messageKey = this.selectionErrorKey(status);
        this.state = 'error';
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
            break;
          case 'inWorld':
            this.selectionFlowActive = false;
            this.done.emit();
            break;
          case 'awaitingCharacterSelection':
            if (this.selectionFlowActive) {
              this.selectionFlowActive = false;
              this.characters = [];
              this.messageKey = 'COMPONENT.CHARACTER_SELECT.LOADING';
              this.state = 'loading';
              this.client.requestCharacterList();
            }
            break;
          case 'disconnected':
            this.selectionFlowActive = false;
            this.characters = [];
            this.messageKey = 'COMPONENT.CHARACTER_SELECT.DISCONNECTED';
            this.state = 'error';
            break;
        }
      }),
      this.client.characterSelectionPendingIdChange.subscribe(characterId => {
        this.pendingCharacterId = characterId;
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
      }),
    );
  }

  public ngOnDestroy(): void {
    this.subscriptions.forEach(subscription => subscription.unsubscribe());
  }

  public close(): void {
    if (this.selectionFlowActive || this.pendingCharacterId !== null) {
      return;
    }

    this.sound.play(Sound.Cancel);
    this.done.next();
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

  @HostListener('window:keydown.escape', ['$event'])
  // @ts-ignore
  private onEscape(event: KeyboardEvent): void {
    this.close();
    event.stopPropagation();
    event.preventDefault();
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
