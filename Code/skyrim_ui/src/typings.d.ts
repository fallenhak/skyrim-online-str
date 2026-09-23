declare const module: NodeModule;

interface NodeModule {
  id: string;
}

/** Skyim: Together type definitions */
declare namespace SkyrimTogetherTypes {
  /** Client initialization callback */
  type InitCallback = () => void;

  /** UI activation callback */
  type ActivateCallback = () => void;

  /** UI deactivation callback */
  type DeactivateCallback = () => void;

  /** Player game entry callback */
  type EnterGameCallback = () => void;

  /** Player game exit callback */
  type ExitGameCallback = () => void;

  /** Player open/close game menu callback */
  type OpeningMenuCallback = (openingMenu: boolean) => void;

  /** Chat message reception callback */
  type MessageCallback = (
    type: number,
    content: string,
    sender: string,
  ) => void;

  /** Connection callback */
  type ConnectCallback = () => void;

  /** Disconnection callback */
  type DisconnectCallback = (isError: boolean) => void;

  /** Name change callback */
  type SetNameCallback = (name: string) => void;

  /** Version set callback */
  type SetVersionCallback = (version: string) => void;

  /**  */
  type OnDebugCallback = (isDebug: boolean) => void;

  type UpdateDebugCallback = (
    numPacketsSent: number,
    numPacketsReceived: number,
    RTT: number,
    packetLoss: number,
    sentBandwidth: number,
    receivedBandwidth: number,
  ) => void;

  type UserDataSetCallback = (password: string, username: string) => void;

  type PlayerConnectedCallback = (
    playerId: number,
    username: string,
    level: number,
    cellName: string,
  ) => void;

  type PlayerDisconnectedCallback = (
    playerId: number,
    username: string,
  ) => void;

  type SetHealthCallback = (playerId: number, health: number) => void;

  type SetLevelCallback = (playerId: number, level: number) => void;

  type SetCellCallback = (playerId: number, cellName: string) => void;

  type SetPlayer3dLoadedCallback = (playerId: number, health: number) => void;

  type SetPlayer3dUnloadedCallback = (playerId: number) => void;

  type SetLocalPlayerIdCallback = (playerId: number) => void;

  type ProtocolMismatch = () => void;

  type TriggerError = () => void;

  type DummyDataCallback = (data: Array<number>) => void;

  type PartyInfoCallback = (playerIds: Array<number>, leaderId: number) => void;

  type PartyCreatedCallback = () => void;

  type PartyLeftCallback = (inviterId: number) => void;

  type PartyInviteReceivedCallback = (inviterId: number) => void;

  /** Server-owned character identifier encoded as canonical unsigned decimal. */
  type CharacterId = string;

  /** Opaque game form identifier components encoded as unsigned decimal strings. */
  interface CharacterGameId {
    baseId: string;
    modId: string;
  }

  /** Character data returned by the server character-list protocol message. */
  interface CharacterSummaryBridge {
    characterId: CharacterId;
    name: string;
    race: CharacterGameId;
    sex: number;
    level: number;
  }

  /** Positional CefListValue row used to preserve uint64 and uint32 precision. */
  type CharacterSummaryWireRow = [
    characterId: CharacterId,
    name: string,
    raceBaseId: string,
    raceModId: string,
    sex: number,
    level: number,
  ];

  /** Numeric CharacterSelectionStatus values from the existing protocol. */
  type CharacterSelectionStatus = 0 | 1 | 2 | 3;

  /** Local protocol phase, advanced only by existing server/session events. */
  type CharacterSessionState =
    | 'disconnected'
    | 'awaitingCharacterSelection'
    | 'characterSelected'
    | 'applyingCharacter'
    | 'awaitingClientReady'
    | 'awaitingPlayerAssignment'
    | 'inWorld';

  type CharacterListCallback = (rows: CharacterSummaryWireRow[]) => void;

  type CharacterSelectionResultCallback = (
    status: CharacterSelectionStatus,
  ) => void;

  type CharacterSessionStateCallback = (state: CharacterSessionState) => void;
}

/** Global Skyrim: Together object. */
declare const skyrimtogether: SkyrimTogether;

/** Global Skyrim: Together object type. */
interface SkyrimTogether {
  /** Add listener to when the UI is first initialized. */
  on(event: 'init', callback: SkyrimTogetherTypes.InitCallback): void;

  /** Add listener to when the UI is activated. */
  on(event: 'activate', callback: SkyrimTogetherTypes.ActivateCallback): void;

  /** Add listener to when the UI is deactivated. */
  on(
    event: 'deactivate',
    callback: SkyrimTogetherTypes.DeactivateCallback,
  ): void;

  /** Add listener to when the player enters a game. */
  on(event: 'enterGame', callback: SkyrimTogetherTypes.EnterGameCallback): void;

  /** Add listener to when the player exits a game. */
  on(event: 'exitGame', callback: SkyrimTogetherTypes.ExitGameCallback): void;

  /** Add listener to when the player open/close a game menu. */
  on(
    event: 'openingMenu',
    callback: SkyrimTogetherTypes.OpeningMenuCallback,
  ): void;

  /** Add listener to when a player message is received. */
  on(event: 'message', callback: SkyrimTogetherTypes.MessageCallback): void;

  /** Add listener to when the player connects to a server. */
  on(event: 'connect', callback: SkyrimTogetherTypes.ConnectCallback): void;

  /** Receive a complete server-owned character list. */
  on(
    event: 'characterList',
    callback: SkyrimTogetherTypes.CharacterListCallback,
  ): void;

  /** Receive the server's character-selection result status. */
  on(
    event: 'characterSelectionResult',
    callback: SkyrimTogetherTypes.CharacterSelectionResultCallback,
  ): void;

  /** Receive transitions from the existing character load/session flow. */
  on(
    event: 'characterSessionState',
    callback: SkyrimTogetherTypes.CharacterSessionStateCallback,
  ): void;

  /** Add listener to when the player disconnects from a server. */
  on(
    event: 'disconnect',
    callback: SkyrimTogetherTypes.DisconnectCallback,
  ): void;

  /** Add listener to when the player's name changes. */
  on(event: 'setName', callback: SkyrimTogetherTypes.SetNameCallback): void;

  /** Add listener to when the client's version is set. */
  on(
    event: 'setVersion',
    callback: SkyrimTogetherTypes.SetVersionCallback,
  ): void;

  /** Add listener to when the player's press the F3 key */
  on(event: 'debug', callback: SkyrimTogetherTypes.OnDebugCallback): void;

  on(
    event: 'debugData',
    callback: SkyrimTogetherTypes.UpdateDebugCallback,
  ): void;

  /** Add listener to when the player's is connected with the launcher */
  on(
    event: 'userDataSet',
    callback: SkyrimTogetherTypes.UserDataSetCallback,
  ): void;

  /** Add listener to when one player connect in server. */
  on(
    event: 'playerConnected',
    callback: SkyrimTogetherTypes.PlayerConnectedCallback,
  ): void;

  /** Add listener to when one player disconnect in server. */
  on(
    event: 'playerDisconnected',
    callback: SkyrimTogetherTypes.PlayerDisconnectedCallback,
  ): void;

  on(event: 'setHealth', callback: SkyrimTogetherTypes.SetHealthCallback): void;

  /** Add listener to when one player change level in server. */
  on(event: 'setLevel', callback: SkyrimTogetherTypes.SetLevelCallback): void;

  /** Add listener to when one player change cell in server. */
  on(event: 'setCell', callback: SkyrimTogetherTypes.SetCellCallback): void;

  /** Add listener to when a player is loaded or unloaded in 3D.  */
  on(
    event: 'setPlayer3dLoaded',
    callback: SkyrimTogetherTypes.SetPlayer3dLoadedCallback,
  ): void;

  on(
    event: 'setPlayer3dUnloaded',
    callback: SkyrimTogetherTypes.SetPlayer3dUnloadedCallback,
  ): void;

  on(
    event: 'setLocalPlayerId',
    callback: SkyrimTogetherTypes.SetLocalPlayerIdCallback,
  ): void;

  on(
    event: 'protocolMismatch',
    callback: SkyrimTogetherTypes.ProtocolMismatch,
  ): void;

  on(event: 'triggerError', callback: SkyrimTogetherTypes.TriggerError): void;

  on(event: 'dummyData', callback: SkyrimTogetherTypes.DummyDataCallback): void;

  on(event: 'partyInfo', callback: SkyrimTogetherTypes.PartyInfoCallback): void;

  on(
    event: 'partyCreated',
    callback: SkyrimTogetherTypes.PartyCreatedCallback,
  ): void;

  on(event: 'partyLeft', callback: SkyrimTogetherTypes.PartyLeftCallback): void;

  on(
    event: 'partyInviteReceived',
    callback: SkyrimTogetherTypes.PartyInviteReceivedCallback,
  ): void;

  /** Remove listener from when the application is first initialized. */
  off(event: 'init', callback?: SkyrimTogetherTypes.InitCallback): void;

  /** Remove listener from when the UI is activated. */
  off(event: 'activate', callback?: SkyrimTogetherTypes.ActivateCallback): void;

  /** Remove listener from when the UI is deactivated. */
  off(
    event: 'deactivate',
    callback?: SkyrimTogetherTypes.DeactivateCallback,
  ): void;

  /** Remove listener from when the player enters a game. */
  off(
    event: 'enterGame',
    callback?: SkyrimTogetherTypes.EnterGameCallback,
  ): void;

  /** Remove listener from when the player exits a game. */
  off(event: 'exitGame', callback?: SkyrimTogetherTypes.ExitGameCallback): void;

  /** Add listener to when the player open/close a game menu. */
  off(
    event: 'openingMenu',
    callback?: SkyrimTogetherTypes.OpeningMenuCallback,
  ): void;

  /** Remove listener from when a player message is received. */
  off(event: 'message', callback?: SkyrimTogetherTypes.MessageCallback): void;

  /** Remove listener from when the player connects to a server. */
  off(event: 'connect', callback?: SkyrimTogetherTypes.ConnectCallback): void;

  off(
    event: 'characterList',
    callback?: SkyrimTogetherTypes.CharacterListCallback,
  ): void;

  off(
    event: 'characterSelectionResult',
    callback?: SkyrimTogetherTypes.CharacterSelectionResultCallback,
  ): void;

  off(
    event: 'characterSessionState',
    callback?: SkyrimTogetherTypes.CharacterSessionStateCallback,
  ): void;

  /** Remove listener from when the player disconnects from a server. */
  off(
    event: 'disconnect',
    callback?: SkyrimTogetherTypes.DisconnectCallback,
  ): void;

  /** Remove listener from when the player's name changes. */
  off(event: 'setName', callback?: SkyrimTogetherTypes.SetNameCallback): void;

  /** Remove listener from when the client's version is set. */
  off(
    event: 'setVersion',
    callback?: SkyrimTogetherTypes.SetVersionCallback,
  ): void;

  /** Remove listener to when the player's press the F3 key */
  off(event: 'debug', callback?: SkyrimTogetherTypes.OnDebugCallback): void;

  off(
    event: 'debugData',
    callback?: SkyrimTogetherTypes.UpdateDebugCallback,
  ): void;

  /** Add listener to when one player connect in server. */
  off(
    event: 'playerConnected',
    callback?: SkyrimTogetherTypes.PlayerConnectedCallback,
  ): void;

  /** Add listener to when one player disconnect in server. */
  off(
    event: 'playerDisconnected',
    callback?: SkyrimTogetherTypes.PlayerDisconnectedCallback,
  ): void;

  off(
    event: 'userDataSet',
    callback?: SkyrimTogetherTypes.UserDataSetCallback,
  ): void;

  off(
    event: 'setHealth',
    callback?: SkyrimTogetherTypes.SetHealthCallback,
  ): void;

  off(event: 'setLevel', callback?: SkyrimTogetherTypes.SetLevelCallback): void;

  off(event: 'setCell', callback?: SkyrimTogetherTypes.SetCellCallback): void;

  /** Add listener to when a player is loaded or unloaded in 3D.  */
  off(
    event: 'setPlayer3dLoaded',
    callback?: SkyrimTogetherTypes.SetPlayer3dLoadedCallback,
  ): void;

  off(
    event: 'setPlayer3dUnloaded',
    callback?: SkyrimTogetherTypes.SetPlayer3dUnloadedCallback,
  ): void;

  off(
    event: 'setLocalPlayerId',
    callback?: SkyrimTogetherTypes.SetLocalPlayerIdCallback,
  ): void;

  off(
    event: 'protocolMismatch',
    callback?: SkyrimTogetherTypes.ProtocolMismatch,
  ): void;

  off(event: 'triggerError', callback?: SkyrimTogetherTypes.TriggerError): void;

  off(
    event: 'dummyData',
    callback?: SkyrimTogetherTypes.DummyDataCallback,
  ): void;

  off(
    event: 'partyInfo',
    callback?: SkyrimTogetherTypes.PartyInfoCallback,
  ): void;

  off(
    event: 'partyCreated',
    callback?: SkyrimTogetherTypes.PartyCreatedCallback,
  ): void;

  off(
    event: 'partyLeft',
    callback?: SkyrimTogetherTypes.PartyLeftCallback,
  ): void;

  off(
    event: 'partyInviteReceived',
    callback?: SkyrimTogetherTypes.PartyInviteReceivedCallback,
  ): void;

  /**
   * Connect to server at given address and port.
   *
   * @param host IP address or hostname.
   * @param port Port.
   * @param password Server password.
   */
  connect(host: string, port: number, password: string): void;

  /**
   * Disconnect from server or cancel connection.
   */
  disconnect(): void;

  /** Request the server-owned character list for this authenticated session. */
  requestCharacterList(): void;

  /** Ask the server to select a character by its opaque decimal ID. */
  selectCharacter(characterId: SkyrimTogetherTypes.CharacterId): void;

  /**
   * Reveal other players in the immediate area.
   */
  revealPlayers(): void;

  /**
   * Send message to server.
   */
  sendMessage(type: number, message: string): void;

  /**
   * Send a request to the server for changing the in-game time.
   */
  setTime(hours: number, minutes: number): void;

  /**
   * Deactivate UI and release control.
   */
  deactivate(): void;

  /**
   * Teleport to given player
   *
   * @param playerId Id of the player to which the requester should be teleported to
   */
  teleportToPlayer(playerId: number): void;

  /**
   * Reconnect the client.
   */
  reconnect(): void;

  /**
   * Launch a party.
   */
  launchParty(): void;

  /**
   * Send a party invite to player with player id.
   *
   * @param playerId Id of the player to which the invite should be sent.
   */
  createPartyInvite(playerId: number): void;

  /**
   * Accept a party invite.
   *
   * @param inviterId Id of the player who sent the invite.
   */
  acceptPartyInvite(inviterId: number): void;

  /**
   * As a party leader, kick a member from the party.
   *
   * @param playerId Id of the player who gets kicked.
   */
  kickPartyMember(playerId: number): void;

  /**
   * Leave the currently joined party.
   */
  leaveParty(): void;

  /**
   * As a party leader, make someone else the leader.
   *
   * @param playerId Id of the new leader.
   */
  changePartyLeader(playerId: number): void;
}
