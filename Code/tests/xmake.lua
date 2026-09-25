
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../client", "../server")
    add_headerfiles("**.h")
    add_files("*.cpp|PersistenceTests.cpp|RenewableEncounterRepositoryTests.cpp|SessionServiceTests.cpp|ActorPopulationTests.cpp|CharacterNamePolicyTests.cpp|AuthTokenVerifierTests.cpp")
    add_files("../server/Game/Animation/ActionReplayCache.cpp", "../server/Game/Animation/AnimationEventLists.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm",
        "entt")

target("PersistenceTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../server")
    add_files("PersistenceTests.cpp", "RenewableEncounterRepositoryTests.cpp", "CharacterNamePolicyTests.cpp", "../TestMain.cpp")
    add_files("../server/Persistence/*.cpp")
    add_files("../server/Services/CharacterNamePolicy.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "gtest",
        "sqlite3",
        "spdlog",
        "glm")

target("SessionTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../server")
    add_files("SessionServiceTests.cpp", "AuthTokenVerifierTests.cpp", "../TestMain.cpp")
    add_files("../server/Services/SessionService.cpp")
    add_files("../server/Services/CharacterNamePolicy.cpp")
    add_files("../server/Services/AuthTokenVerifier.cpp")
    add_files("../server/Persistence/*.cpp")
    add_deps("SkyrimEncoding", "TiltedConnect")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "cryptopp",
        "gtest",
        "gamenetworkingsockets",
        "sqlite3",
        "spdlog",
        "glm")

target("ActorPopulationTests")
    set_kind("binary")
    set_group("Tests")
    set_pcxxheader("../components/es_loader/stdafx.h")
    add_includedirs(
        ".", "../encoding", "../server", "../components/es_loader")
    add_files("ActorPopulationTests.cpp", "../TestMain.cpp")
    add_files("../server/Services/ActorPopulationPolicy.cpp")
    add_files("../server/Services/ActorPopulationAssignmentPolicy.cpp")
    add_files("../server/Components/ModsComponent.cpp")
    add_files("../server/Services/ActorPopulationIdentityResolver.cpp")
    add_deps("ESLoader", "SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "gtest",
        "zlib",
        "glm",
        "spdlog")
