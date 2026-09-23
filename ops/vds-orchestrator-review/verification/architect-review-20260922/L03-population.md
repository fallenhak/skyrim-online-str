# L03 — population architect review evidence

## Review identity and exact state

- Branch: `parallel/population-loader`
- Production worktree: `/srv/projects/skyrim-online-str/workers/population`
- Exact HEAD: `52c97ba4d5da993e6ef2fa4bdf398f13c22b456a`
- Phase: `L03`
- State: `NEEDS_SOL_REVIEW`
- Review type: `CURRENT_PHASE_REVIEW`
- Supervisor reason: `worker reported a blocking condition`
- CI: `NOT_RUN`

Exact production Git status:

```text
## parallel/population-loader...origin/parallel/population-loader
 M Code/components/es_loader/ESLoader.cpp
 M Code/components/es_loader/Records/Record.h
 M Code/components/es_loader/TESFile.cpp
 M Code/components/es_loader/TESFile.h
 M Code/tests/ActorPopulationTests.cpp
 M docs/ACTOR_POPULATION.md
```

Worktree and index `git diff --check` both returned zero. The supervisor packet
did not record structural or focused-test evidence because the worker stopped
before those validations could be recorded.

## Original phase instruction and bounded acceptance

The lane plan states exactly:

> L03 Research TES4 plugin flags in loader. Correctly identify ESL/light
> namespace including ESL-flagged .esp if current extension heuristic is
> insufficient. Use plugin header authority, not client claim.

The same plan keeps record loading opt-in, forbids treating `Unknown` as
`Creature` or `Humanoid`, and forbids combat/reward/UI changes. The bounded
acceptance is therefore server-owned TES4 header authority for light-plugin
namespace selection, correct standard/light ID assignment for the covered
cases, preservation of metadata-only behavior, and no population trust-boundary
regression. The supervisor did not persist a separate formal acceptance object.

## Worker result and validation gap

The worker reported TES4-header-authoritative ESL detection, synthetic coverage
for flagged `.esp` and `.esl` cases, documentation updates, and:

```text
Tests: git diff --check and C++20 syntax probe passed.
xmake -y ActorPopulationTests was unavailable because xmake is not installed.
WORKER_RESULT: BLOCKED
```

The old `BLOCKED` marker is attributable to the missing xmake validation path;
the packet reports no implementation failure. Under V3 semantics this is
semantically a `COMPLETE_WITH_VALIDATION_GAP` candidate only if the worker
emits the required bounded marker. This capture does not reinterpret the
recorded state or retry the lane.

## Exact actor-record information parsed

`TESFile::ReadHeaderFlags` reads only the fixed 24-byte `Record` header:

- `formType` at offset 0, expected to be `FormEnum::TES4`;
- `dataSize` at offset 4, required to fit within the file after the header;
- `flags` at offset 8, returned to the load-order classifier.

The new `Record::FLAGS::kESL` value is `0x00000200`. The normal loader then
assigns a light namespace (`FE` prefix plus light ID) when that bit is set,
including for a filename ending in `.esp`. A readable `.esl` without the bit
is treated as standard. If the header cannot be read or validated, the code
falls back to the filename extension for compatibility. Full record indexing
and NPC/RACE/ACHR parsing remain separate and opt-in.

## Trust boundary and population semantics

The namespace decision is made from the server's own `loadorder.txt` and files
under its Data directory. The client does not provide the header flags. The
existing resolver maps client/network identities through server mod metadata;
client high form-ID prefixes and client actor/leveled-pick claims remain
diagnostics, not authoritative identity.

L03 does not change `Humanoid`, `Creature`, or `Unknown` classification. The
existing policy still maps only configured race editor IDs, returns `Unknown`
when the record collection, NPC, RNAM, RACE, editor ID, or policy rule is
missing, and treats the player reference separately. The assignment policy
rejects trusted humanoids only when its startup gate is enabled, allows trusted
creatures, and has an explicit setting for unknown identities.

The current code path therefore provides proof that an unresolved or
unsupported record cannot silently become `Creature`: the default class is
`kUnknown`, the resolver keeps untrusted client claims separate, and only an
explicit server policy entry changes the class. L03 changes the form-ID
namespace inputs that can make a record resolvable; it does not add a fallback
from `Unknown` to `Creature`.

Modded actors and races remain conservative. A correctly flagged mod plugin can
now receive the correct light namespace even when its extension is `.esp`, so
ACHR/NPC/RACE resolution has a better foundation. Modded race editor IDs are
still `Unknown` unless the server explicitly configures them; duplicate editor
IDs across plugins, missing masters, and deeper record-override behavior are
outside L03 and remain future risks.

## Malformed/truncated plugin behavior

Header probing returns no flags when the path is not a regular readable file,
the file is shorter than `sizeof(Record)`, the read is short, the first record
is not TES4, or `dataSize` exceeds the remaining file size. The caller then
uses the filename extension. This avoids a crash and preserves metadata for
missing/partial plugin installations, but it means a malformed `.esl` can
still be assigned a light namespace and a malformed `.esp` can still be
assigned a standard namespace. That compatibility fallback is the main L03
residual trust/diagnostic risk and should be revisited before arbitrary
modlists are treated as production-safe.

## Tests and performance/compatibility assessment

The new synthetic test creates four plugins in a temporary directory:

1. `Flagged.esp` with the ESL bit → light ID 0;
2. `Standard.esp` without the bit → standard ID 0;
3. `Flagged.esl` with the bit → light ID 1; and
4. `Unflagged.esl` without the bit → standard ID 1.

It also checks metadata-only loading returns an empty record collection and
preserves load-order entries. It does not test truncated headers, wrong TES4
types, huge data sizes, missing files with extension fallback, master-file
edge cases, ID overflow, symlinked plugin paths, or full record loading. The
normal `ActorPopulationTests` target was not runnable because xmake was absent.

The runtime cost is one filesystem stat and a bounded header read for each
existing plugin during load-order parsing, even when full record loading is
disabled. It does not parse full plugin records in the metadata-only path.
The code assumes the platform's little-endian representation, which matches
the supported Windows/Linux targets. Standard/light counter overflow and
full malformed-record bounds remain later hardening work.

## Exact bounded combined diff

```diff
diff --git a/Code/components/es_loader/ESLoader.cpp b/Code/components/es_loader/ESLoader.cpp
index 54d01c20..7fba3a40 100644
--- a/Code/components/es_loader/ESLoader.cpp
+++ b/Code/components/es_loader/ESLoader.cpp
@@ -87,6 +87,29 @@ PluginType GetPluginType(const String& acFilename) noexcept
         return PluginType::kLite;
     return PluginType::kInvalid;
 }
+
+PluginType GetAuthoritativePluginType(const String& acFilename, const fs::path& acPath) noexcept
+{
+    const auto extensionType = GetPluginType(acFilename);
+    const auto headerFlags = TESFile::ReadHeaderFlags(acPath);
+    if (!headerFlags)
+        return extensionType;
+
+    // Skyrim uses the TES4 header's ESL bit (bit 9), rather than the filename,
+    // to select the FE/light form-ID namespace. In particular, an ESL-flagged
+    // .esp must not consume a standard-plugin slot.
+    if ((*headerFlags & Record::FLAGS::kESL) != 0)
+        return PluginType::kLite;
+
+    // A readable TES4 header is authoritative in both directions. An .esl
+    // without the ESL flag is not assigned a light namespace merely because
+    // of its extension. The extension remains a compatibility fallback when a
+    // plugin file is unavailable or its header cannot be read.
+    if (extensionType == PluginType::kLite)
+        return PluginType::kStandard;
+
+    return extensionType;
+}
 } // namespace

 String ReadZString(Buffer::Reader& aReader) noexcept
@@ -182,13 +205,18 @@ bool ESLoader::LoadLoadOrder()
             continue;
         }

-        const auto pluginType = GetPluginType(line);
-        if (pluginType == PluginType::kInvalid)
+        const auto extensionType = GetPluginType(line);
+        if (extensionType == PluginType::kInvalid)
         {
             spdlog::warn("Ignoring unrecognized plugin entry in loadorder.txt: {}", line);
             continue;
         }

+        // Reading this fixed-size TES4 header establishes server-owned plugin
+        // namespace metadata; full record indexing remains opt-in below.
+        const auto pluginPath = GetPath(line);
+        const auto pluginType = pluginPath.empty() ? extensionType : GetAuthoritativePluginType(line, pluginPath);
+
         if (!seenFilenames.emplace(MakeFilenameKey(line)).second)
         {
             spdlog::warn("Ignoring duplicate plugin entry in loadorder.txt: {}", line);
diff --git a/Code/components/es_loader/Records/Record.h b/Code/components/es_loader/Records/Record.h
index 49f48698..4af611b1 100644
--- a/Code/components/es_loader/Records/Record.h
+++ b/Code/components/es_loader/Records/Record.h
@@ -16,6 +16,10 @@ public:
     enum FLAGS
     {
         kMasterFile = 1,
+        // TES4 header flag 0x00000200 marks a plugin as light/ESL. This is
+        // authoritative for the plugin namespace even when the filename has
+        // an .esp extension.
+        kESL = 0x200,
         kCompressed = 0x40000,
         kIgnored = 0x1000,
         kIsMarker = 0x800000,
diff --git a/Code/components/es_loader/TESFile.cpp b/Code/components/es_loader/TESFile.cpp
index a7aad2d8..21c2235c 100644
--- a/Code/components/es_loader/TESFile.cpp
+++ b/Code/components/es_loader/TESFile.cpp
@@ -1,5 +1,7 @@
 #include "TESFile.h"

+#include <array>
+#include <cstring>
 #include <filesystem>
 #include <fstream>
 #include <system_error>
@@ -23,6 +25,38 @@ void TESFile::Setup(uint16_t aLiteId)
     m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);
 }
+
+std::optional<uint32_t> TESFile::ReadHeaderFlags(const std::filesystem::path& acPath) noexcept
+{
+    std::error_code fileSizeError;
+    const uintmax_t fileSize = std::filesystem::file_size(acPath, fileSizeError);
+    if (fileSizeError || fileSize < sizeof(Record))
+        return std::nullopt;
+
+    std::ifstream file(acPath, std::ios::binary);
+    if (!file)
+        return std::nullopt;
+
+    std::array<uint8_t, sizeof(Record)> header{};
+    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
+    if (file.gcount() != static_cast<std::streamsize>(header.size()))
+        return std::nullopt;
+
+    uint32_t formType = 0;
+    uint32_t dataSize = 0;
+    uint32_t flags = 0;
+    std::memcpy(&formType, header.data(), sizeof(formType));
+    std::memcpy(&dataSize, header.data() + sizeof(formType), sizeof(dataSize));
+    std::memcpy(&flags, header.data() + sizeof(formType) + sizeof(dataSize), sizeof(flags));
+
+    if (formType != static_cast<uint32_t>(FormEnum::TES4) ||
+        static_cast<uintmax_t>(dataSize) > fileSize - sizeof(Record))
+    {
+        return std::nullopt;
+    }
+
+    return flags;
+}

 bool TESFile::LoadFile(const std::filesystem::path& acPath) noexcept
 {
diff --git a/Code/components/es_loader/TESFile.h b/Code/components/es_loader/TESFile.h
index 6e083cd3..829bee53 100644
--- a/Code/components/es_loader/TESFile.h
+++ b/Code/components/es_loader/TESFile.h
@@ -21,6 +21,7 @@ public:

     void Setup(uint8_t aStandardId);
     void Setup(uint16_t aLiteId);
+    [[nodiscard]] static std::optional<uint32_t> ReadHeaderFlags(const std::filesystem::path& acPath) noexcept;
     bool LoadFile(const std::filesystem::path& acPath) noexcept;
     bool IndexRecords(RecordCollection& aRecordCollection) noexcept;
diff --git a/Code/tests/ActorPopulationTests.cpp b/Code/tests/ActorPopulationTests.cpp
index 43c97284..dbdb502f 100644
--- a/Code/tests/ActorPopulationTests.cpp
+++ b/Code/tests/ActorPopulationTests.cpp
@@ -19,6 +19,7 @@
 #include <limits>
 #include <string>
 #include <system_error>
+#include <utility>
 #include <vector>
@@ -153,11 +154,11 @@ Bytes MakeActorReferenceData(const uint32_t aBaseRawId)
     return data;
 }

-void AppendRecord(Bytes& aBytes, FormEnum aFormType, uint32_t aFormId, const Bytes& aData)
+void AppendRecord(Bytes& aBytes, FormEnum aFormType, uint32_t aFormId, const Bytes& aData, const uint32_t aFlags = 0)
 {
     AppendValue(aBytes, static_cast<uint32_t>(aFormType));
     AppendValue(aBytes, static_cast<uint32_t>(aData.size()));
-    AppendValue(aBytes, uint32_t{}); // flags
+    AppendValue(aBytes, aFlags);
     AppendValue(aBytes, aFormId);
     AppendValue(aBytes, uint32_t{}); // version control info
     AppendValue(aBytes, uint16_t{}); // form version
@@ -165,6 +166,13 @@ void AppendRecord(Bytes& aBytes, FormEnum aFormType, uint32_t aFormId, const Byt
     aBytes.insert(aBytes.end(), aData.begin(), aData.end());
 }
+
+Bytes MakePluginHeader(const uint32_t aFlags)
+{
+    Bytes data;
+    AppendRecord(data, FormEnum::TES4, 0, {}, aFlags);
+    return data;
+}
@@ -367,6 +375,54 @@ TEST(ESLoader, ParsesLoadOrderMetadataSafelyWithoutPluginFiles)
     EXPECT_FALSE(records->HasAnyRecords());
 }
+
+TEST(ESLoader, UsesTES4ESLFlagForLightPluginNamespace)
+{
+    TemporaryDirectory dataDirectory;
+    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();
+
+    {
+        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
+        ASSERT_TRUE(loadOrder.good());
+        loadOrder << "Flagged.esp\n"
+                  << "Standard.esp\n"
+                  << "Flagged.esl\n"
+                  << "Unflagged.esl\n";
+    }
+
+    for (const auto& plugin : {std::pair{"Flagged.esp", static_cast<uint32_t>(Record::FLAGS::kESL)}, std::pair{"Standard.esp", uint32_t{0}},
+                               std::pair{"Flagged.esl", static_cast<uint32_t>(Record::FLAGS::kESL)}, std::pair{"Unflagged.esl", uint32_t{0}}})
+    {
+        std::ofstream pluginFile(dataDirectory.Path() / plugin.first, std::ios::binary);
+        ASSERT_TRUE(pluginFile.good());
+        const Bytes data = MakePluginHeader(plugin.second);
+        pluginFile.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
+    }
+
+    ESLoader::ESLoader loader(dataDirectory.Path());
+    const auto metadataOnly = loader.BuildRecordCollection(false);
+    ASSERT_NE(metadataOnly, nullptr);
+    EXPECT_FALSE(metadataOnly->HasAnyRecords());
+
+    const auto& plugins = loader.GetLoadOrder();
+    ASSERT_EQ(plugins.size(), 4U);
+
+    EXPECT_EQ(plugins[0].m_filename, "Flagged.esp");
+    EXPECT_TRUE(plugins[0].IsLite());
+    EXPECT_EQ(plugins[0].m_liteId, 0U);
+
+    EXPECT_EQ(plugins[1].m_filename, "Standard.esp");
+    EXPECT_FALSE(plugins[1].IsLite());
+    EXPECT_EQ(plugins[1].m_standardId, 0U);
+
+    EXPECT_EQ(plugins[2].m_filename, "Flagged.esl");
+    EXPECT_TRUE(plugins[2].IsLite());
+    EXPECT_EQ(plugins[2].m_liteId, 1U);
+
+    EXPECT_EQ(plugins[3].m_filename, "Unflagged.esl");
+    EXPECT_FALSE(plugins[3].IsLite());
+    EXPECT_EQ(plugins[3].m_standardId, 1U);
+}
diff --git a/docs/ACTOR_POPULATION.md b/docs/ACTOR_POPULATION.md
index e40fb78d..1c8dff19 100644
--- a/docs/ACTOR_POPULATION.md
+++ b/docs/ACTOR_POPULATION.md
@@ -110,6 +110,11 @@ Load-order metadata is kept in the file's declared order. Blank/comment lines,
 UTF-8 BOMs, CR/LF endings, and surrounding whitespace are normalized; duplicate
 or unsupported/unsafe plugin entries (including path-bearing or control-character
 names) are ignored without consuming an ID.
+For readable plugin files, the server uses the TES4 header ESL flag
+(`0x00000200`) as the namespace authority, so an ESL-flagged `.esp` receives a
+light-plugin ID and `FE` form prefix. Filename extension is only a compatibility
+fallback when the plugin is absent or its header cannot be read; client-reported
+mod kind does not select the server namespace.
 When full record loading is enabled, absent plugin files are warned about and
 skipped while the parsed metadata remains available.
```
