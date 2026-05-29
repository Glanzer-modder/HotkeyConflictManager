#include "PCH.h"
#include "ControlMapWatcher.h"
#include "KeyRegistry.h"

ControlMapWatcher* ControlMapWatcher::GetSingleton() noexcept
{
    static ControlMapWatcher instance;
    return &instance;
}

void ControlMapWatcher::Register()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("[HCM] ControlMapWatcher: UI singleton not available.");
        return;
    }
    ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
    logger::info("[HCM] ControlMapWatcher registered for MenuOpenCloseEvent.");
}

RE::BSEventNotifyControl ControlMapWatcher::ProcessEvent(
    const RE::MenuOpenCloseEvent*               a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!a_event || a_event->opening)
        return RE::BSEventNotifyControl::kContinue;

    // The Controls rebinding screen is a sub-panel within the Journal Menu
    // (Journal_SystemTab).  There is no dedicated menu event for it, so I have to
    // check on every Journal Menu close.  The comparison is 129 integers and
    // is negligible even if the player never touched Controls.
    // Journal Menu open events do not fire reliably in Skyrim SE (the menu
    // persists in memory and only close events fire consistently).
    // I therefore only handle the close event, which is sufficient for
    // detecting ControlMap key remapping.
    if (a_event->menuName == RE::JournalMenu::MENU_NAME) {
        auto conflicts = KeyRegistry::GetSingleton().CheckControlMapChanges();
        for (const auto& msg : conflicts) {
            SKSE::GetTaskInterface()->AddTask([msg]() {
                RE::DebugMessageBox(msg.c_str());
            });
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}
