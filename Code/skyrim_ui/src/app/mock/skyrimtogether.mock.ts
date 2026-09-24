import { createStore } from '@ngneat/elf';
import {
  addEntities,
  deleteEntities,
  getAllEntities,
  getEntity,
  selectAllEntities,
  updateEntities,
  withEntities,
} from '@ngneat/elf-entities';
import { EventEmitter } from 'events';
import { fromEvent } from 'rxjs';
import { MessageTypes } from '../services/chat.service';
import { ErrorEvents } from '../services/error.service';
import { MockPlayer } from './mock-player';

let nextPlayerId = 1;

type MockServerCharacterSummary = SkyrimTogetherTypes.CharacterSummaryBridge;

const playerStore = createStore(
  { name: 'players' },
  withEntities<MockPlayer>(),
);

export class SkyrimtogetherMock extends EventEmitter implements SkyrimTogether {
  private connected = false;
  public characterConnectionGeneration = 0;
  private active = false;
  private version = 'browser';
  private playerName = 'Local Player';
  private showEvents = true;
  private localPlayerId: number;
  private readonly mockServerCharacterList: MockServerCharacterSummary[] = [
    {
      characterId: '1',
      name: 'Traveler',
      race: { baseId: '0', modId: '0' },
      sex: 0,
      level: 1,
    },
  ];
  public readonly players$ = playerStore.pipe(selectAllEntities());

  constructor() {
    super();
    // Browser preview: play the entry flow the launcher/native side drives in game.
    setTimeout(() => this.startEntryFlow(), 400);
  }

  connect(host: string, port: number, password: string): void {
    if (!this.connected) {
      let error: ErrorEvents | boolean;
      switch (host) {
        case 't-port':
        case 't-host':
          error = true;
          break;
        case 't-password':
          if (password !== 'test') {
            error = { error: 'wrong_password' };
          }
          break;
        case 't-version':
          error = {
            error: 'wrong_version',
            data: {
              version: '[current-version]',
              expectedVersion: '[expected-version]',
            },
          };
          break;
        case 't-full':
          error = { error: 'server_full' };
          break;
        case 't-client-mods':
          error = { error: 'client_mods_disallowed', data: { mods: ['SKSE'] } };
          break;
        case 't-mods':
          error = {
            error: 'mods_mismatch',
            data: {
              mods: [
                ['missing.esp', '0'],
                ['remove.esp', '12'],
                ['missing_2.esp', '0'],
                ['remove_2.esp', '12'],
              ],
            },
          };
          break;
      }
      setTimeout(() => {
        this.connected = !error;
        const connectionGeneration = ++this.characterConnectionGeneration;
        if (error) {
          this.emit('disconnect', false, connectionGeneration);
        } else {
          this.emit('characterSessionState', 'awaitingCharacterSelection');
          this.emit('connect', connectionGeneration);
        }
        if (error && typeof error !== 'boolean') {
          this.emit('triggerError', JSON.stringify(error));
        } else {
          this.emit('setLocalPlayerId', (this.localPlayerId = nextPlayerId++));
        }
        for (const player of playerStore.query(getAllEntities())) {
          this.emit(
            'playerConnected',
            player.id,
            player.name,
            player.level,
            player.cellName,
          );
          this.emit('setPlayer3dLoaded', player.id, player.health);
        }
      }, 250);
    }
  }

  createCharacter(slotIndex: number, name: string): void {
    this.emit('loadingStage', 'creatingCharacter', 0.35);
    setTimeout(() => {
      const trimmed = name.trim();
      let status: SkyrimTogetherTypes.CharacterCreateStatus = 0;
      if (trimmed.length < 3 || trimmed.length > 24) status = 1;
      else if (
        this.mockServerCharacterList.some(
          c => c.name.toLowerCase() === trimmed.toLowerCase(),
        )
      )
        status = 2;
      else if (slotIndex > 0) status = 3;
      const characterId = status === 0 ? String(Date.now()) : '0';
      if (status === 0) {
        this.mockServerCharacterList.push({
          characterId,
          name: trimmed,
          race: { baseId: '0', modId: '0' },
          sex: 0,
          level: 1,
          slotIndex,
        });
      }
      this.emit('characterCreateResult', status, characterId);
      if (status === 0) this.playWorldEntry(true);
    }, 600);
  }

  /** Preview of the native save-free world entry the game drives after create/select. */
  playWorldEntry(isNew: boolean): void {
    const stages: Array<[SkyrimTogetherTypes.LoadingStage, number]> = [
      ['loadingWorld', 0.7],
      ['applyingCharacter', 0.8],
      ...(isNew ? ([['raceMenu', 0.9]] as Array<[SkyrimTogetherTypes.LoadingStage, number]>) : []),
      ['enteringWorld', 0.95],
      ['done', 1],
    ];
    stages.forEach(([stage, progress], i) =>
      setTimeout(() => this.emit('loadingStage', stage, progress), 900 * (i + 1)),
    );
  }

  retryConnect(): void {
    this.startEntryFlow();
  }

  quitGame(): void {
    console.info('[mock] quitGame');
  }

  /** Browser preview of the launcher -> auth -> slots flow. */
  startEntryFlow(): void {
    const steps: Array<[number, () => void]> = [
      [0, () => this.emit('loadingStage', 'connecting', 0.1)],
      [500, () => this.emit('authState', 'connecting', '', '', '')],
      [900, () => this.emit('loadingStage', 'authenticating', 0.3)],
      [1300, () => this.emit('authState', 'authenticating', '', '', '')],
      [
        2000,
        () => this.emit('authState', 'authenticated', 'Burak', '', ''),
      ],
      [2300, () => this.emit('loadingStage', 'fetchingCharacters', 0.6)],
      [2400, () => this.emit('characterSlots', 3, 1)],
      [2500, () => this.connect('mock-server', 10578, '')],
      [3000, () => this.requestCharacterList()],
    ];
    for (const [delay, fn] of steps) setTimeout(fn, delay);
  }

  disconnect(): void {
    if (this.connected) {
      this.connected = false;
      const connectionGeneration = ++this.characterConnectionGeneration;
      this.emit('disconnect', false, connectionGeneration);
    }
  }

  requestCharacterList(): void {
    if (this.connected) {
      // This is a fixed mock server response fixture, not a local character store.
      this.emit(
        'characterList',
        this.mockServerCharacterList.map(
          (character): SkyrimTogetherTypes.CharacterSummaryWireRow => [
            character.characterId,
            character.name,
            character.race.baseId,
            character.race.modId,
            character.sex,
            character.level,
          ],
        ),
        this.characterConnectionGeneration,
      );
    }
  }

  selectCharacter(characterId: SkyrimTogetherTypes.CharacterId): void {
    if (!this.connected) {
      return;
    }

    const hasServerCharacter = this.mockServerCharacterList.some(
      character => character.characterId === characterId,
    );
    const status: SkyrimTogetherTypes.CharacterSelectionStatus =
      hasServerCharacter ? 0 : 2;
    if (status === 0) {
      this.emit('characterSessionState', 'characterSelected');
      this.playWorldEntry(false);
    }
    this.emit(
      'characterSelectionResult',
      status,
      this.characterConnectionGeneration,
    );
  }

  reconnect(): void {
    throw new Error('NOT YET IMPLEMENTED');
  }

  revealPlayers(): void {
    this.sendMessage(MessageTypes.SYSTEM_MESSAGE, "Revealing players...");
  }

  setTime(hours: number, minutes: number): void {
    this.sendMessage(MessageTypes.SYSTEM_MESSAGE, `Setting time to "${hours}:${minutes}"!`);
  }

  sendMessage(type: MessageTypes, message: string): void {
    if (this.connected) {
      this.emit('message', type, message, this.playerName);
    }
  }

  deactivate(): void {
    if (this.active) {
      this.active = false;
      this.emit('deactivate');
    }
  }

  teleportToPlayer(playerId: number): void {
    if (this.connected) {
      const player = playerStore.query(getEntity(playerId));
      if (player) {
        console.log(
          `%cTELEPORT`,
          'background: #F09688; color: #fff; padding: 3px; font-size: 9px;',
          'Teleport to player',
          JSON.stringify(player.name),
          `(${player.id})`,
          'in',
          JSON.stringify(player.cellName),
        );
      }
    }
  }

  launchParty(): void {
    if (this.connected) {
      this.emit('partyCreated');
    }
  }

  createPartyInvite(playerId: number): void {
    playerStore.update(updateEntities(playerId, { invited: true }));
  }

  acceptPartyInvite(inviterId: number): void {
    this.emit('partyCreated');
    this.emit(
      'partyInfo',
      [
        ...playerStore
          .query(getAllEntities())
          .filter(p => p.isInGroup)
          .map(p => p.id),
      ],
      inviterId,
    );
  }

  kickPartyMember(playerId: number): void {
    playerStore.update(updateEntities(playerId, { isInGroup: false }));
    this.emit(
      'partyInfo',
      playerStore
        .query(getAllEntities())
        .filter(p => p.isInGroup)
        .map(p => p.id),
      this.localPlayerId,
    );
  }

  leaveParty(): void {
    if (this.connected) {
      this.emit('partyInfo', [], -1);
      this.emit('partyLeft');
    }
  }

  changePartyLeader(playerId: number): void {
    playerStore.update(updateEntities(playerId, { hasOwnParty: true }));
    this.emit(
      'partyInfo',
      [
        ...playerStore
          .query(getAllEntities())
          .filter(p => p.isInGroup)
          .map(p => p.id),
      ],
      playerId,
    );
  }

  initMock() {
    Object.keys((this as any)._events).forEach(e => {
      this.on(e, (...params) => {
        if (this.showEvents) {
          const eventName = e
            .replace(/(\G(?!^)|\b[a-zA-Z][a-z]*)([A-Z][a-z]*|\d+)/gm, `$1_$2`)
            .toUpperCase();
          console.log(
            `%cEVENT`,
            'background: #009688; color: #fff; padding: 3px; font-size: 9px;',
            `[${eventName}]`,
            ...params.map(v => JSON.stringify(v)),
          );
        }
      });
    });
    this.emit('init');
    this.emit('enterGame');
    this.emit('setVersion', this.version);
    this.emit('setName', this.playerName);

    fromEvent(window, 'keydown').subscribe((event: KeyboardEvent) => {
      if (event.ctrlKey && event.location === 2) {
        this.active = !this.active;
        this.emit(this.active ? 'activate' : 'deactivate');
        event.preventDefault();
        return;
      }
      switch (event.key) {
        case 'F2': {
          this.active = !this.active;
          this.emit(this.active ? 'activate' : 'deactivate');
          event.preventDefault();
          break;
        }
      }
    });
  }

  setMockVersion(version: string): void {
    this.version = version;
  }

  setMockPlayerName(name: string): void {
    this.playerName = name;
  }

  setShowEvents(show: boolean): void {
    this.showEvents = show;
  }

  addMockPlayer() {
    const newPlayerId = nextPlayerId++;
    const cities = [
      'Whiterun',
      'Dawnstar',
      'Falkreath',
      'Markarth',
      'Morthal',
      'Riften',
      'Solitude',
      'Windhelm',
    ];
    const newPlayer: MockPlayer = {
      name: 'Player ' + newPlayerId,
      id: newPlayerId,
      level: Math.floor(Math.random() * 100),
      health: Math.floor(Math.random() * 100),
      cellName: cities[Math.floor(Math.random() * cities.length)],
      hasOwnParty: false,
      isInGroup: false,
      invited: false,
      invitedLocalPlayer: false,
    };
    playerStore.update(addEntities(newPlayer));
    if (this.connected) {
      this.emit(
        'playerConnected',
        newPlayer.id,
        newPlayer.name,
        newPlayer.level,
        newPlayer.cellName,
      );
      this.emit('setPlayer3dLoaded', newPlayer.id, newPlayer.health);
    }
    // this.emit('setHealth', newPlayer.id, newPlayer.health);
    return newPlayer;
  }

  disconnectMockPlayer(playerId: number) {
    const mockPlayer = playerStore.query(getEntity(playerId));
    if (mockPlayer) {
      if (this.connected) {
        this.emit('playerDisconnected', mockPlayer.id, mockPlayer.name);
      }
      playerStore.update(deleteEntities(mockPlayer.id));
    }
  }

  accteptMockPlayerInvite(playerId: number) {
    playerStore.update(
      updateEntities(playerId, { invited: false, isInGroup: true }),
    );
    this.emit(
      'partyInfo',
      playerStore
        .query(getAllEntities())
        .filter(p => p.isInGroup)
        .map(p => p.id),
      this.localPlayerId,
    );
  }

  inviteToPlayerMockParty(playerId: number) {
    playerStore.update(updateEntities(playerId, { invitedLocalPlayer: true }));
    this.emit('partyInviteReceived', playerId);
  }

  startPlayerMockParty(playerId: number) {
    playerStore.update(
      updateEntities(playerId, { hasOwnParty: true, isInGroup: true }),
    );
  }

  mockPlayerLeaveParty(playerId: number) {
    const player = playerStore.query(getEntity(playerId));

    playerStore.update(
      updateEntities(playerId, {
        hasOwnParty: false,
        invitedLocalPlayer: false,
        isInGroup: false,
      }),
    );
    if (this.connected) {
      if (player.isInGroup) {
        this.emit(
          'partyInfo',
          playerStore
            .query(getAllEntities())
            .filter(p => p.isInGroup)
            .map(p => p.id),
          this.localPlayerId,
        );
      } else if (player.hasOwnParty) {
        this.emit('partyInfo', [], -1);
      }
    }
  }

  updateMockDebugData() {
    const debugData = [
      Math.floor(Math.random() * 100),
      Math.floor(Math.random() * 100),
      Math.floor(Math.random() * 100),
      Math.floor((Math.random() / 4) * 100),
      Math.random() * 100,
      Math.random() * 100,
    ];
    this.emit('debugData', ...debugData);
    return debugData;
  }
}

export function mockSkyrimTogether() {
  (globalThis as any).skyrimtogether = new SkyrimtogetherMock();
}
