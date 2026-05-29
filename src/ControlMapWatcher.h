#pragma once
#include "PCH.h"

// ============================================================
//  ControlMapWatcher
//  Listens for Journal Menu close events and checks whether
//  any ControlMap binding change conflicts with a mod key
//  registered in KeyRegistry.
//
//  Registered in main.cpp at kDataLoaded via Register().
//  The snapshot is taken at kDataLoaded, kPostLoadGame, and
//  kNewGame so the baseline always reflects the current save's
//  key configuration before the player makes any changes.
// ============================================================

class ControlMapWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static ControlMapWatcher* GetSingleton() noexcept;
    static void               Register();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent*               a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

private:
    ControlMapWatcher()  = default;
    ~ControlMapWatcher() = default;
    ControlMapWatcher(const ControlMapWatcher&)            = delete;
    ControlMapWatcher& operator=(const ControlMapWatcher&) = delete;
};
