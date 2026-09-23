# A04 — authority architect review evidence

## Review identity and exact state

- Branch: `parallel/interaction-authority`
- Production worktree: `/srv/projects/skyrim-online-str/workers/authority`
- Previous A03 head: `caf7dcc31ca4b6d0912f31b151408ba24c13938c`
- Exact A04 HEAD: `b8fc40415fceee88ae6424d25bd68a2a0ddeb70a`
- Phase: `A04`
- State: `NEEDS_SOL_REVIEW`
- Review type: `POST_PHASE_CHECKPOINT`
- Review reason: `protocol or wire-compatibility surface changed`
- Worktree: clean

Exact production Git status:

```text
## parallel/interaction-authority...origin/parallel/interaction-authority
```

Both worktree and index `git diff --check` returned zero.

## Original phase instruction and acceptance

The lane plan states exactly:

> A04 Equipment and weapon-drawn stale-incarnation audit; add epoch only if
> same-client reacquisition creates a real stale-packet bug.

The lane boundary requires interaction-authority work only, forbids combat hit
protocol, population-loader, and UI changes, and says not to create blanket
owner checks for legitimate non-owner gameplay. The bounded acceptance surface
is therefore: prove the stale weapon-drawn case, propagate the current epoch
from the owning client, require current owner plus matching epoch on the server,
preserve existing equipment semantics, and cover the message round trip. The
supervisor did not persist a separate formal acceptance object.

## Worker and validation evidence

The worker reported `WORKER_RESULT: COMPLETE` and described:

- `OwnershipEpoch` added to `DrawWeaponRequest` and populated by the client;
- server validation now requires current owner plus matching epoch;
- protocol round-trip coverage added; and
- equipment already validated epochs, so it was left unchanged.

Focused `git diff --check` passed. Full tests were unavailable because `xmake`
and dependencies were not installed. The supervisor recorded exact-SHA CI:

```text
Build linux:  run 35664506689, completed/success,
              sha b8fc40415fceee88ae6424d25bd68a2a0ddeb70a
Build windows: run 35664506683, completed/success,
              sha b8fc40415fceee88ae6424d25bd68a2a0ddeb70a
```

## Stale-incarnation and ownership-epoch analysis

The client reads `LocalComponent.OwnershipEpoch` when it emits the weapon
state update. The server looks up the actor by server ID, requires a
`CharacterComponent` plus `OwnerComponent`, and calls
`OwnerComponent::IsCurrentOwner(player, requestedEpoch)`. That helper requires
the player pointer to match, the requested epoch to be non-zero, and the
component epoch to match. A stale packet from a prior owner or prior transfer
therefore cannot change `IsWeaponDrawn`.

`CharacterService::TransferOwnership` increments the epoch on an actual owner
change, wraps zero back to one, updates the owner, and broadcasts the new epoch.
The only production `SetOwner` call is in that transfer path, followed by the
epoch update. If the same client loses ownership and later reacquires the same
actor, the intervening owner change still advances the epoch. A direct
same-owner no-op does not advance it because no stale ownership boundary was
crossed.

The protection is actor-ownership freshness, not an independent actor
incarnation generation. If a future path reuses an entity ID without going
through the ownership-transfer protocol, A04 does not by itself protect that
path; the existing lifecycle-generation work is a separate boundary.

## DrawWeaponRequest and equipment synchronization

The opcode is unchanged, but the payload layout is now:

```text
Id, OwnershipEpoch, IsWeaponDrawn
```

The equality operator includes the epoch. Client production sends the local
epoch. Server production rejects missing actors, wrong owner pointers, zero
epochs, and stale epochs before mutating the character's weapon-drawn state.
The existing equipment-change client/server paths already carry and validate
their epoch, so A04 aligns weapon-drawn state with that established pattern.

This is a real protocol/wire change even though no opcode changed. The
connection authentication path rejects a client whose `BUILD_COMMIT` differs
from the server, so a normally deployed old client cannot reach this handler.
There is no separate per-message capability negotiation; deployment must keep
the exact client/server build pair together. The architect should explicitly
acknowledge that compatibility contract.

## Authority and security assessment

- The client supplies only actor ID, epoch, and a presentation state. The
  server decides whether the sender currently owns that actor at that epoch.
- No `CharacterId`, `AccountId`, XP, reward, damage, kill, or population
  classification is accepted by this path.
- A forged actor ID is limited to a lookup; no mutation occurs without current
  owner and epoch proof. Epoch zero is rejected.
- The round-trip test proves serialization symmetry for the new field; it does
  not prove mixed-version behavior, replay handling outside the connection
  version gate, or runtime ownership transfer under load.

There is no technical blocker found inside the completed A04 scope that would
make the checkpoint inherently unsafe. The wire change and lack of a full
local test run are the reasons for explicit Sol review, not a hidden approval.
The checkpoint remains undecided because this capture is expressly
read-only and does not approve A04.

## Exact committed diff: A03 head to A04 HEAD

```diff
diff --git a/Code/client/Services/Generic/InventoryService.cpp b/Code/client/Services/Generic/InventoryService.cpp
index 8d9f2c78..962c8c24 100644
--- a/Code/client/Services/Generic/InventoryService.cpp
+++ b/Code/client/Services/Generic/InventoryService.cpp
@@ -316,6 +316,7 @@ void InventoryService::RunWeaponStateUpdates() noexcept

             DrawWeaponRequest request;
             request.Id = localComponent.Id;
+            request.OwnershipEpoch = localComponent.OwnershipEpoch;
             request.IsWeaponDrawn = isWeaponDrawn;

             m_transport.Send(request);
diff --git a/Code/encoding/Messages/DrawWeaponRequest.cpp b/Code/encoding/Messages/DrawWeaponRequest.cpp
index 24ae0f30..06dee3c3 100644
--- a/Code/encoding/Messages/DrawWeaponRequest.cpp
+++ b/Code/encoding/Messages/DrawWeaponRequest.cpp
@@ -3,6 +3,7 @@ void DrawWeaponRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
 {
     Serialization::WriteVarInt(aWriter, Id);
+    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
     Serialization::WriteBool(aWriter, IsWeaponDrawn);
 }

@@ -11,5 +12,6 @@ void DrawWeaponRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
     ClientMessage::DeserializeRaw(aReader);

     Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
+    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
     IsWeaponDrawn = Serialization::ReadBool(aReader);
 }
diff --git a/Code/encoding/Messages/DrawWeaponRequest.h b/Code/encoding/Messages/DrawWeaponRequest.h
index a1da5790..07f0f6e7 100644
--- a/Code/encoding/Messages/DrawWeaponRequest.h
+++ b/Code/encoding/Messages/DrawWeaponRequest.h
@@ -14,8 +14,12 @@ struct DrawWeaponRequest final : ClientMessage
     void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
     void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

-    bool operator==(const DrawWeaponRequest& acRhs) const noexcept { return Id == acRhs.Id && IsWeaponDrawn == acRhs.IsWeaponDrawn && GetOpcode() == acRhs.GetOpcode(); }
+    bool operator==(const DrawWeaponRequest& acRhs) const noexcept
+    {
+        return Id == acRhs.Id && OwnershipEpoch == acRhs.OwnershipEpoch && IsWeaponDrawn == acRhs.IsWeaponDrawn && GetOpcode() == acRhs.GetOpcode();
+    }

     uint32_t Id{};
+    uint32_t OwnershipEpoch{};
     bool IsWeaponDrawn{};
 };
diff --git a/Code/server/Services/InventoryService.cpp b/Code/server/Services/InventoryService.cpp
index dabc309a..60287f08 100644
--- a/Code/server/Services/InventoryService.cpp
+++ b/Code/server/Services/InventoryService.cpp
@@ -155,10 +155,19 @@ void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
     auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
     const auto it = characterView.find(static_cast<entt::entity>(message.Id));

-    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).GetOwner() == acMessage.pPlayer)
+    if (it == std::end(characterView))
+        return;
+
+    auto& ownerComponent = characterView.get<OwnerComponent>(*it);
+    if (!ownerComponent.IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
     {
-        auto& characterComponent = characterView.get<CharacterComponent>(*it);
-        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
-        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);
+        spdlog::debug(
+            "Rejected weapon drawn update from player {:X} for actor {:X}; requested epoch {} does not match current epoch {}",
+            acMessage.pPlayer->GetId(), message.Id, message.OwnershipEpoch, ownerComponent.OwnershipEpoch);
+        return;
     }
+
+    auto& characterComponent = characterView.get<CharacterComponent>(*it);
+    characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
+    spdlog::debug("Updating weapon drawn state {:x}:{} at epoch {}", message.Id, message.IsWeaponDrawn, message.OwnershipEpoch);
 }
diff --git a/Code/tests/CharacterSessionProtocolTests.cpp b/Code/tests/CharacterSessionProtocolTests.cpp
index 813bde86..c9acf488 100644
--- a/Code/tests/CharacterSessionProtocolTests.cpp
+++ b/Code/tests/CharacterSessionProtocolTests.cpp
@@ -26,6 +26,7 @@
 #include <Messages/NotifyProjectileLaunch.h>
 #include <Messages/NotifyRespawn.h>
 #include <Messages/NotifySpellCast.h>
+#include <Messages/DrawWeaponRequest.h>
 #include <Messages/ProjectileLaunchRequest.h>
 #include <Messages/RequestRespawn.h>
 #include <Messages/SelectCharacterRequest.h>
@@ -126,6 +127,20 @@ TEST_CASE("Character session protocol messages round trip", "[encoding.character_session]")
         auto parsedHealthRequest = TiltedPhoques::CastUnique<RequestHealthChangeBroadcast>(std::move(healthMessage));
         REQUIRE(*parsedHealthRequest == healthRequest);

+        DrawWeaponRequest drawWeaponRequest{};
+        drawWeaponRequest.Id = 0x2345;
+        drawWeaponRequest.OwnershipEpoch = 67;
+        drawWeaponRequest.IsWeaponDrawn = true;
+        TiltedPhoques::Buffer drawWeaponBuffer(256);
+        TiltedPhoques::Buffer::Writer drawWeaponWriter(&drawWeaponBuffer);
+        drawWeaponRequest.Serialize(drawWeaponWriter);
+
+        TiltedPhoques::Buffer::Reader drawWeaponReader(&drawWeaponBuffer);
+        auto drawWeaponMessage = clientFactory.Extract(drawWeaponReader);
+        REQUIRE(drawWeaponMessage);
+        auto parsedDrawWeaponRequest = TiltedPhoques::CastUnique<DrawWeaponRequest>(std::move(drawWeaponMessage));
+        REQUIRE(*parsedDrawWeaponRequest == drawWeaponRequest);

         ProjectileLaunchRequest projectileRequest{};
```

The substantive evidence is the new `DrawWeaponRequest` round trip with
`OwnershipEpoch == 67`. No development worktree was edited during this
capture.
