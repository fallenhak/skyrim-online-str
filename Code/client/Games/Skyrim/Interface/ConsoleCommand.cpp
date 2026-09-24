#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/Interface/ConsoleCommand.h>

namespace
{
// These are the engine layouts consumed by Console::ExecuteCommand. The values
// are unmanaged Scaleform values, so their string pointer remains valid for the
// synchronous delegate call and no Scaleform retain/release is required.
struct NativeGFxValue final
{
    void* ObjectInterface{};
    std::uint32_t Type{};
    std::uint32_t Padding{};
    union Value
    {
        const char* String;
        std::uint64_t Bits;
    } Data{};
};
static_assert(sizeof(NativeGFxValue) == 0x18);

struct NativeFxDelegateArgs final
{
    NativeGFxValue ResponseId{};
    IMenu* Handler{};
    void* MovieView{};
    const NativeGFxValue* Arguments{};
    std::uint32_t ArgumentCount{};
    std::uint32_t Padding{};
};
static_assert(sizeof(NativeFxDelegateArgs) == 0x38);

bool QueueMessage(const BSFixedString& acMenu, const UIMessage::UI_MESSAGE_TYPE aMessage) noexcept
{
    using TAddMessage = void(void*, const BSFixedString*, std::uint32_t, void*);
    POINTER_SKYRIMSE(TAddMessage, addMessage, 13631);
    POINTER_SKYRIMSE(void*, queueSingleton, 400445);

    const auto pAddMessage = addMessage.Get();
    const auto ppQueue = queueSingleton.Get();
    if (!pAddMessage || !ppQueue || !*ppQueue)
        return false;

    pAddMessage(*ppQueue, &acMenu, static_cast<std::uint32_t>(aMessage), nullptr);
    return true;
}
} // namespace

bool ConsoleCommand::QueueConsole(const UIMessage::UI_MESSAGE_TYPE aMessage) noexcept
{
    static BSFixedString s_consoleMenu("Console");
    const auto* pUi = UI::Get();
    if (!pUi)
        return false;

    const bool isOpen = pUi->GetMenuOpen(s_consoleMenu);
    if ((aMessage == UIMessage::kShow || aMessage == UIMessage::kReshow) && isOpen)
        return true;
    if ((aMessage == UIMessage::kHide || aMessage == UIMessage::kForceHide) && !isOpen)
        return true;

    return QueueMessage(s_consoleMenu, aMessage);
}

bool ConsoleCommand::Execute(const char* acCommand) noexcept
{
    if (!acCommand || !*acCommand)
        return false;

    static BSFixedString s_consoleMenu("Console");
    auto* pUi = UI::Get();
    if (!pUi || !pUi->GetMenuOpen(s_consoleMenu) || pUi->menuStack.Empty())
        return false;

    POINTER_SKYRIMSE(void(NativeFxDelegateArgs*), executeCommand, 51084);
    const auto pFunction = executeCommand.Get();
    if (!pFunction)
        return false;

    NativeGFxValue command{};
    command.Type = 0x04; // GFxValue::ValueType::kString
    command.Data.String = acCommand;

    NativeFxDelegateArgs args{};
    args.ResponseId.Type = 0x01; // GFxValue::ValueType::kNull
    args.Handler = pUi->menuStack[0];
    args.Arguments = &command;
    args.ArgumentCount = 1;
    pFunction(&args);
    return true;
}
