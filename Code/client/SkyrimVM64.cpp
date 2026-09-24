#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include <Misc/GameVM.h>
#include <World.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct Main;

TP_THIS_FUNCTION(TVMUpdate, int, GameVM, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

// Set when the VM hook ran the full world update during the current main loop iteration.
static bool s_worldUpdatedThisFrame = false;

int TP_MAKE_THISCALL(HookVMUpdate, GameVM, float a2)
{
    if (apThis->inactive == 0)
    {
        g_appInstance->Update();
        s_worldUpdatedThisFrame = true;
    }

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    const auto result = TiltedPhoques::ThisCall(MainLoop, apThis);

    // The Papyrus VM does not tick on the title menu, but the launcher session connects from there.
    if (!s_worldUpdatedThisFrame && World::IsCreated())
    {
        static bool s_loggedMenuPump = false;
        if (!s_loggedMenuPump)
        {
            s_loggedMenuPump = true;
            spdlog::info("[Runner] VM idle, pumping network from main loop");
        }
        World::Get().UpdateNetworkOnly();
    }
    s_worldUpdatedThisFrame = false;

    return result;
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

        VMUpdate = cVMUpdate.Get();
        MainLoop = cMainLoop.Get();
        VMDestructor = cVMDestructor.Get();

        TP_HOOK(&VMUpdate, HookVMUpdate);
        TP_HOOK(&MainLoop, HookMainLoop);
        TP_HOOK(&VMDestructor, HookVMDestructor);
    });

