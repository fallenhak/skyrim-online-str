
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding")
    add_headerfiles("**.h")
    add_files("*.cpp|PersistenceTests.cpp|SessionServiceTests.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm")

target("PersistenceTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../server")
    add_files("PersistenceTests.cpp", "../TestMain.cpp")
    add_files("../server/Persistence/*.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "gtest",
        "sqlite3",
        "spdlog")

target("SessionTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../server")
    add_files("SessionServiceTests.cpp", "../TestMain.cpp")
    add_files("../server/Services/SessionService.cpp")
    add_files("../server/Persistence/*.cpp")
    add_deps("SkyrimEncoding", "TiltedConnect")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "gtest",
        "gamenetworkingsockets",
        "sqlite3",
        "spdlog")
