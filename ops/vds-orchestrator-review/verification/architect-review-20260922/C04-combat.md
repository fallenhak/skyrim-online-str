# C04 — combat architect review evidence

## Review identity and exact state

- Branch: `parallel/combat-foundations`
- Production worktree: `/srv/projects/skyrim-online-str/workers/combat`
- Exact HEAD: `500bf5ea5e04341f565776ed5263119c1cf06893`
- Phase: `C04`
- State: `NEEDS_SOL_REVIEW`
- Review type: `CURRENT_PHASE_REVIEW`
- Supervisor reason: `worker reported a blocking condition`
- CI: `NOT_RUN`

Exact production Git status at capture time:

```text
## parallel/combat-foundations...origin/parallel/combat-foundations
?? Code/server/Services/ValidatedHitObservation.h
?? Code/tests/ValidatedHitObservationTests.cpp
```

Both `git diff --check` and `git diff --cached --check` returned zero. The
supervisor packet did not record structural or focused-test evidence because
the worker stopped before those validations could be recorded.

## Original phase instruction and bounded acceptance

The lane plan states exactly:

> C04 Define append-only validated hit-observation DTO with no CharacterId,
> XP, reward or authoritative damage.

The long-haul plan expands the same phase as a smallest DTO containing attacker
server ID plus ownership epoch, target server ID plus current target
incarnation evidence, bounded observation ID, and minimal diagnostics. Its hard
boundary is never to make client-provided `CharacterId`, XP, reward, kill,
damage, or classification canonical. No separate formal acceptance object was
persisted by the supervisor, so the bounded acceptance set is the phase text
plus those explicit lane boundaries.

## Worker result and validation gap

The worker reported:

```text
Implemented C04:
- Added immutable server-internal ValidatedHitObservation.h.
- Added focused DTO tests.
- No CharacterId, damage, XP, reward, packet, or service flow added.

Checks: direct C++20 compile/runtime checks passed. xmake/Catch2 were
unavailable, so TPTests could not run.

WORKER_RESULT: BLOCKED
```

The old `BLOCKED` marker was caused by an unavailable validation environment,
not by a reported design defect: `xmake` and the Catch2 headers were absent on
the worker. The worker did not add an integration bypass. A direct C++20 check
and a small append/immutability runtime check passed.

Under V3 semantics, the narrow semantic result would be
`COMPLETE_WITH_VALIDATION_GAP`: the phase's bounded artifact exists and the
only stated gap is the unavailable normal test target. The installed V3
supervisor, however, requires a valid worker `COMPLETE_WITH_VALIDATION_GAP`
marker plus a bounded `VALIDATION_GAP:` reason before it can record that
classification. This capture does not rewrite the existing `BLOCKED` state or
retry the lane.

## Architectural interaction

- Actor lifecycle/incarnation: the DTO carries `TargetLifecycleGeneration`.
  The existing server-owned `ActorLifecycleComponent` allocates monotonic,
  non-zero generations independently of reusable EnTT IDs. C04 does not look
  up or validate the generation; a future target-authorization/handler phase
  must compare it with the current component and reject stale incarnations.
- Ownership/authority: `AttackerServerId` and
  `AttackerOwnershipEpoch` match the existing server-side owner/epoch model.
  C04 does not authorize a sender, check `InWorld`, resolve an owner, or map a
  persistent character. Those are future policy boundaries.
- Combat contribution: the existing ledger keys a target by server ID plus
  lifecycle generation and stores server-resolved `Persistence::CharacterId`.
  The DTO is compatible with that key but does not call or populate the ledger.
- Health mutation: the existing health policy accepts only a current owner and
  matching ownership epoch and applies a finite signed delta. The DTO contains
  no health value and has no path to mutate health.
- Progression/reward evidence: the DTO contains no `CharacterId`, XP, reward,
  skill, loot, kill, or damage amount. It is not connected to
  `ProgressionService` or any reward path. A later phase must resolve the
  attacker identity on the server after authorization.

## Trust and append-only assessment

The proposed object is a `final` internal value type with `const` identity and
ordering members. It is not a message, has no serialization methods, and is
not included in a client/server opcode factory. Its comments explicitly say
that it is created only after canonical entity resolution and that later code
may expire/remove a whole record rather than rewrite it. The append test
demonstrates that two vector entries remain distinct and ordered; it does not
prove a production bounded store because no store or handler exists yet.

No client-controlled value becomes authoritative merely by constructing this
type. The type itself is not an authority check: any future caller could pass
arbitrary non-zero IDs, so the next phase must keep construction behind a
server-only validation boundary.

## Tests and exactly what they prove

`ValidatedHitObservationTests.cpp` contains three tests:

1. A valid sample preserves all six fields and the `const` members are not
   assignable at compile time.
2. Zero attacker ID, zero attacker epoch, zero target ID, zero target
   generation, and zero observation ID are rejected by `IsWellFormed()`.
3. Two observations can be appended to a vector without overwriting their
   IDs or observed ticks.

They do not prove server authorization, lifecycle lookup, client packet
handling, health correlation, deduplication, expiry, contribution, or
progression safety. The normal `TPTests` target would include the new test via
the existing `*.cpp` glob, but it was not runnable in the worker environment.

## Concrete residual risks

- The DTO is not wired to any handler, so no end-to-end security property is
  established by C04 alone.
- `IsWellFormed()` checks non-zero identity fields only; it does not prove
  ownership, lifecycle freshness, range, collision, target class, or tick
  bounds.
- `ObservationId` is non-zero but the type does not enforce monotonicity,
  deduplication, rollover behavior, or a bounded pending store.
- `ObservedTick` is ordering metadata and may be zero; future policy must
  define its source and validity window.
- The comments describe a server-validated construction boundary, but C++
  visibility alone does not enforce that boundary.
- Full Catch2/TPTests and configured CI evidence remain unavailable for the
  dirty worktree.

## Exact bounded untracked-file diff

```diff
diff --git a/Code/server/Services/ValidatedHitObservation.h b/Code/server/Services/ValidatedHitObservation.h
new file mode 100644
--- /dev/null
+++ b/Code/server/Services/ValidatedHitObservation.h
@@ -0,0 +1,71 @@
+#pragma once
+
+#include <cstdint>
+
+/**
+ * @brief One server-validated hit observation, captured without applying damage.
+ *
+ * This is an internal value object, not a network message or a damage command.
+ * A caller constructs it only after resolving the sender and the canonical
+ * attacker/target entities.  Its identity fields are immutable so an accepted
+ * observation can only be appended to a bounded pending-observation store; a
+ * later policy may expire or remove the whole record, but cannot rewrite it.
+ *
+ * The observation deliberately contains no persistent character identifier.
+ * The server may resolve that identity later, after the attacker authorization
+ * boundary, and no client-selected identity is carried forward here.
+ * ObservationId is scoped to the attacker authority incarnation, while
+ * ObservedTick is ordering metadata only; neither is a damage or reward value.
+ */
+struct ValidatedHitObservation final
+{
+    using ServerId = std::uint32_t;
+    using OwnershipEpoch = std::uint32_t;
+    using LifecycleGeneration = std::uint64_t;
+    using ObservationSequence = std::uint64_t;
+    using ObservationTick = std::uint64_t;
+
+    static constexpr ServerId kInvalidServerId = 0;
+    static constexpr OwnershipEpoch kInvalidOwnershipEpoch = 0;
+    static constexpr LifecycleGeneration kInvalidLifecycleGeneration = 0;
+    static constexpr ObservationSequence kInvalidObservationId = 0;
+
+    constexpr ValidatedHitObservation(
+        const ServerId aAttackerServerId,
+        const OwnershipEpoch aAttackerOwnershipEpoch,
+        const ServerId aTargetServerId,
+        const LifecycleGeneration aTargetLifecycleGeneration,
+        const ObservationSequence aObservationId,
+        const ObservationTick aObservedTick) noexcept
+        : AttackerServerId(aAttackerServerId)
+        , AttackerOwnershipEpoch(aAttackerOwnershipEpoch)
+        , TargetServerId(aTargetServerId)
+        , TargetLifecycleGeneration(aTargetLifecycleGeneration)
+        , ObservationId(aObservationId)
+        , ObservedTick(aObservedTick)
+    {
+    }
+
+    /**
+     * @brief Checks only the DTO's identity invariants.
+     *
+     * This does not authorize an attacker, classify a target, prove range or
+     * collision, correlate health, or establish a kill. Those checks belong to
+     * later server-side policies.
+     */
+    [[nodiscard]] constexpr bool IsWellFormed() const noexcept
+    {
+        return AttackerServerId != kInvalidServerId && AttackerOwnershipEpoch != kInvalidOwnershipEpoch &&
+               TargetServerId != kInvalidServerId && TargetLifecycleGeneration != kInvalidLifecycleGeneration &&
+               ObservationId != kInvalidObservationId;
+    }
+
+    friend constexpr bool operator==(const ValidatedHitObservation&, const ValidatedHitObservation&) noexcept = default;
+
+    const ServerId AttackerServerId;
+    const OwnershipEpoch AttackerOwnershipEpoch;
+    const ServerId TargetServerId;
+    const LifecycleGeneration TargetLifecycleGeneration;
+    const ObservationSequence ObservationId;
+    const ObservationTick ObservedTick;
+};
diff --git a/Code/tests/ValidatedHitObservationTests.cpp b/Code/tests/ValidatedHitObservationTests.cpp
new file mode 100644
--- /dev/null
+++ b/Code/tests/ValidatedHitObservationTests.cpp
@@ -0,0 +1,45 @@
+#include <Services/ValidatedHitObservation.h>
+
+#include <catch2/catch.hpp>
+
+#include <cstdint>
+#include <type_traits>
+#include <vector>
+
+TEST_CASE("validated hit observations contain only immutable server entity identity", "[combat_authority]")
+{
+    const ValidatedHitObservation observation{17, 4, 29, 81, 12, 900};
+
+    REQUIRE(observation.IsWellFormed());
+    REQUIRE(observation.AttackerServerId == 17);
+    REQUIRE(observation.AttackerOwnershipEpoch == 4);
+    REQUIRE(observation.TargetServerId == 29);
+    REQUIRE(observation.TargetLifecycleGeneration == 81);
+    REQUIRE(observation.ObservationId == 12);
+    REQUIRE(observation.ObservedTick == 900);
+
+    static_assert(!std::is_assignable_v<decltype(observation.AttackerServerId)&, std::uint32_t>);
+    static_assert(!std::is_assignable_v<decltype(observation.TargetLifecycleGeneration)&, std::uint64_t>);
+}
+
+TEST_CASE("validated hit observations reject missing identity components", "[combat_authority]")
+{
+    REQUIRE_FALSE(ValidatedHitObservation{0, 1, 2, 3, 4, 5}.IsWellFormed());
+    REQUIRE_FALSE(ValidatedHitObservation{1, 0, 2, 3, 4, 5}.IsWellFormed());
+    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 0, 3, 4, 5}.IsWellFormed());
+    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 0, 4, 5}.IsWellFormed());
+    REQUIRE_FALSE(ValidatedHitObservation{1, 1, 2, 3, 0, 5}.IsWellFormed());
+}
+
+TEST_CASE("validated hit observations can be appended without overwriting prior records", "[combat_authority]")
+{
+    std::vector<ValidatedHitObservation> observations;
+    observations.emplace_back(1, 2, 3, 4, 5, 6);
+    observations.emplace_back(1, 2, 3, 4, 6, 7);
+
+    REQUIRE(observations.size() == 2);
+    REQUIRE(observations[0].ObservationId == 5);
+    REQUIRE(observations[1].ObservationId == 6);
+    REQUIRE(observations[0].ObservedTick == 6);
+    REQUIRE(observations[1].ObservedTick == 7);
+}
```
