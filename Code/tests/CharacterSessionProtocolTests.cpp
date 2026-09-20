#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/SelectCharacterRequest.h>
#include <Messages/ServerMessageFactory.h>

#include <limits>

TEST_CASE("Character session protocol messages round trip", "[encoding.character_session]")
{
    SECTION("client requests")
    {
        RequestCharacterList listRequest{};
        TiltedPhoques::Buffer listBuffer(256);
        TiltedPhoques::Buffer::Writer listWriter(&listBuffer);
        listRequest.Serialize(listWriter);

        TiltedPhoques::Buffer::Reader listReader(&listBuffer);
        const ClientMessageFactory clientFactory;
        auto listMessage = clientFactory.Extract(listReader);
        REQUIRE(listMessage);
        auto parsedListRequest = TiltedPhoques::CastUnique<RequestCharacterList>(std::move(listMessage));
        REQUIRE(*parsedListRequest == listRequest);

        SelectCharacterRequest selectionRequest{};
        selectionRequest.CharacterId = std::numeric_limits<std::uint64_t>::max();
        TiltedPhoques::Buffer selectionBuffer(256);
        TiltedPhoques::Buffer::Writer selectionWriter(&selectionBuffer);
        selectionRequest.Serialize(selectionWriter);

        TiltedPhoques::Buffer::Reader selectionReader(&selectionBuffer);
        auto selectionMessage = clientFactory.Extract(selectionReader);
        REQUIRE(selectionMessage);
        auto parsedSelectionRequest = TiltedPhoques::CastUnique<SelectCharacterRequest>(std::move(selectionMessage));
        REQUIRE(*parsedSelectionRequest == selectionRequest);
    }

    SECTION("server list and selection result")
    {
        NotifyCharacterList list{};
        list.Characters = {
            CharacterSummary{42, "Aela", GameId(0x01, 0x00013746), 0, 18},
            CharacterSummary{std::numeric_limits<std::uint64_t>::max(), "O'Reilly", GameId(0x02, 0x0000003c), 1, 27}};

        TiltedPhoques::Buffer listBuffer(1024);
        TiltedPhoques::Buffer::Writer listWriter(&listBuffer);
        list.Serialize(listWriter);

        TiltedPhoques::Buffer::Reader listReader(&listBuffer);
        const ServerMessageFactory serverFactory;
        auto listMessage = serverFactory.Extract(listReader);
        REQUIRE(listMessage);
        auto parsedList = TiltedPhoques::CastUnique<NotifyCharacterList>(std::move(listMessage));
        REQUIRE(*parsedList == list);

        NotifyCharacterSelectionResult result{};
        result.Status = CharacterSelectionStatus::kNotFoundOrNotOwned;
        TiltedPhoques::Buffer resultBuffer(256);
        TiltedPhoques::Buffer::Writer resultWriter(&resultBuffer);
        result.Serialize(resultWriter);

        TiltedPhoques::Buffer::Reader resultReader(&resultBuffer);
        auto resultMessage = serverFactory.Extract(resultReader);
        REQUIRE(resultMessage);
        auto parsedResult = TiltedPhoques::CastUnique<NotifyCharacterSelectionResult>(std::move(resultMessage));
        REQUIRE(*parsedResult == result);
    }
}
