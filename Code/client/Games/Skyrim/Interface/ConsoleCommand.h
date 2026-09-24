#pragma once

#include <Games/Skyrim/Interface/UI.h>

/**
 * @brief Opens Skyrim's native console and invokes its command handler.
 *
 * The command is invoked through the same Scaleform delegate path used by the
 * native Console menu. This avoids synthetic keyboard input and never writes a
 * Skyrim save.
 */
struct ConsoleCommand final
{
    static bool QueueConsole(UIMessage::UI_MESSAGE_TYPE aMessage) noexcept;
    static bool Execute(const char* acCommand) noexcept;
};
