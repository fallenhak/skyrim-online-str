# Cross-lane architect analysis

## C04 ↔ A04: authority and lifecycle

C04's attacker key uses a server ID plus `OwnershipEpoch`; A04 makes the
weapon-drawn path require the same current-owner/epoch proof. C04's target key
uses server ID plus `TargetLifecycleGeneration`, matching the existing
server-owned actor lifecycle component and contribution-ledger key. There is no
direct source conflict: C04 adds an internal, non-wire DTO and A04 changes only
the weapon-drawn wire payload and handler.

The future C04 attacker/target policies must use the existing owner component,
session/`InWorld` gate, and lifecycle component rather than trusting fields
carried by a client. A04 does not replace lifecycle generation, and C04 does
not authorize weapon state. A future combat client producer must also use the
same exact-build protocol contract acknowledged for A04.

## C04 ↔ L03: actor classification and lifecycle

C04 intentionally does not carry or decide population class. Future target
authorization must reject stale target generations and require a trusted
Creature identity before health/death contribution. L03 improves the
server-owned form-ID namespace used by ACHR/NPC/RACE resolution, but it does
not itself add a Creature classification or enable the humanoid gate. C04 can
be architect-reviewed independently; later C06/C10/C11 work must not turn
unresolved L03 data or a client claim into a trusted target.

## L03 ↔ future M01-WORLD renewable encounters

The control-plane roadmap explicitly keeps W01-W10
`BLOCKED_EXTERNAL_GATE` until there is a reviewed integration branch containing
the required combat lifecycle and population-classification foundations. L03's
TES4 header correction is useful input for that gate but is not the gate itself:
it does not implement renewable encounter state, spawn identity, reset
incarnations, concurrent ownership/reconnect handling, or runtime acceptance.
No integration branch was created in this capture.

## U02 ↔ Character Session/Persistence protocol

U02 is consistent with the existing server-owned path. The server lists only
characters belonging to the authenticated owner profile, validates a selected
ID through `GetCharacterForOwner`, rechecks the selected character during load,
ready, assignment, and entered-world transitions, and exposes a typed result
enum without a client-selected result ID. The native client owns snapshot
application and sends `CharacterReadyRequest`; Angular is only a projection and
request surface.

The proposed bridge carries `CharacterId` and race IDs as decimal strings for
precision safety, but that does not make them client authority. U02 changes no
network opcode or wire payload. Its only present recovery issue is the staged
EOF whitespace error recorded in [U02 evidence](U02-ui.md).

## Incompatible or risky assumptions

1. A04 changes the payload of an existing opcode. U02 correctly says its own
   bridge does not change network opcodes, but that does not waive A04's
   client/server build-pair requirement. The authentication `BUILD_COMMIT`
   check is the compatibility barrier; there is no per-message capability
   negotiation.
2. C04's DTO comments assume server validation before construction, but the
   type does not enforce that. No future handler may treat non-zero fields as
   authorization.
3. L03 falls back to filename extension for unreadable/malformed headers. That
   preserves compatibility but means malformed plugin metadata can still affect
   namespace assignment. Unknown actor classification must remain conservative.
4. U02's Angular states are deliberately projections. Implementing them as a
   local store or allowing them to close the UI on selection success would
   contradict the native/session authority boundary.
5. Review packets were generated while the supervisor packet metadata said
   `RUNNING`; the current production status captured separately is
   `GLOBAL: PAUSED`. Current status wins, and no packet metadata was edited on
   a development lane.

## Independent recovery/decision safety

No decision was issued. As an architectural dependency assessment only:

- A04 is independently reviewable from C04, L03, and U02. Its decision should
  explicitly acknowledge the `DrawWeaponRequest` wire change and exact-build
  gate. No other lane must be approved first.
- C04 is independently reviewable as an internal DTO. Its future authorization
  and health/death phases must wait for server-owned lifecycle/authority
  evidence; a validation-gap classification does not authorize future work.
- L03 is independently reviewable as parser metadata work. M01-WORLD must
  still wait for the reviewed integration branch and the missing combat/
  population foundations; L03 alone cannot unlock W01.
- U02 is independently reviewable as documentation. If accepted, the only
  recovery needed for this snapshot is the EOF whitespace correction; U03
  should not start until the architect decision and supervisor bookkeeping are
  complete.

Therefore approving A04 and retrying C04/L03/U02 as an automatic bundle would
not be safe or necessary. Each lane requires its own explicit architect
decision; retries would be mutating operations and remain forbidden while the
global mode is paused. No lane is required to wait for another merely to be
reviewed, but future combat/world integration must wait for the relevant
cross-lane authority and classification evidence.

## Exact protected heads and statuses

```text
combat     parallel/combat-foundations  500bf5ea5e04341f565776ed5263119c1cf06893  ?? two files
authority  parallel/interaction-authority b8fc40415fceee88ae6424d25bd68a2a0ddeb70a clean
population parallel/population-loader   52c97ba4d5da993e6ef2fa4bdf398f13c22b456a  M six files
ui         parallel/character-ui        a473531ad16a82cecc8a4cdecc460934ba7efcfa  M/A staged two docs
```

## Mutation and secret boundary

The production actions were status, review-status, healthcheck, process scan,
test discovery, bounded packet reads, and Git status/diff reads. No operator
command (`approve`, `retry`, `block`, `start`, `resume`, or `stop`) was called.
No development source, documentation, index, branch, or worktree was changed.
The capture contains no credentials, tokens, raw authentication files, or raw
service logs.
