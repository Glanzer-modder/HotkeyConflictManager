#include "PCH.h"
#include "PapyrusFunctions.h"
#include "KeyRegistry.h"

namespace
{
    // ---- CheckKeyConflict ----------------------------------------
    std::string CheckKeyConflict(RE::StaticFunctionTag*,
                                 std::int32_t a_keyCode,
                                 RE::TESForm* a_caller)
    {
        if (a_keyCode <= 0 ||
            static_cast<std::uint32_t>(a_keyCode) > Plugin::AbsMaxKeyCode)
            return {};
        const FormID callerID = a_caller ? a_caller->GetFormID() : 0u;
        return KeyRegistry::GetSingleton()
                   .CheckConflict(static_cast<std::uint32_t>(a_keyCode),
                                  callerID);
    }

    // ---- MCM display helpers -------------------------------------
    std::int32_t GetAssignedKeyCount(RE::StaticFunctionTag*)
    {
        return KeyRegistry::GetSingleton().GetAssignedKeyCount();
    }

    std::string GetKeyDisplayLeft(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetKeyDisplayLeft(a_index);
    }

    std::string GetKeyDisplayRight(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetKeyDisplayRight(a_index);
    }

    std::int32_t GetKeyDisplayStatus(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetKeyDisplayStatus(a_index);
    }

    std::int32_t GetModConflictCount(RE::StaticFunctionTag*)
    {
        return KeyRegistry::GetSingleton().GetModConflictCount();
    }

    std::int32_t GetGameConflictCount(RE::StaticFunctionTag*)
    {
        return KeyRegistry::GetSingleton().GetGameConflictCount();
    }

    std::int32_t GetGameKeyCount(RE::StaticFunctionTag*)
    {
        return KeyRegistry::GetSingleton().GetGameKeyCount();
    }

    std::string GetGameKeyDisplayLeft(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetGameKeyDisplayLeft(a_index);
    }

    std::string GetGameKeyDisplayRight(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetGameKeyDisplayRight(a_index);
    }

    std::int32_t GetGameKeyDisplayStatus(RE::StaticFunctionTag*, std::int32_t a_index)
    {
        return KeyRegistry::GetSingleton().GetGameKeyDisplayStatus(a_index);
    }

    // ---- Configuration page --------------------------------------
    void SetPopupWarningsEnabled(RE::StaticFunctionTag*, bool a_enabled)
    {
        KeyRegistry::GetSingleton().SetPopupWarningsEnabled(a_enabled);
    }

    void ClearOrphans(RE::StaticFunctionTag*)
    {
        KeyRegistry::GetSingleton().ClearOrphans();
        logger::info("[HCM] Orphaned registry entries cleared by user.");
    }

    void ClearRegistry(RE::StaticFunctionTag*)
    {
        KeyRegistry::GetSingleton().Clear();
        logger::info("[HCM] Registry cleared by user.");
    }

    void SetDebugModeEnabled(RE::StaticFunctionTag*, bool a_enabled)
    {
        KeyRegistry::GetSingleton().SetDebugModeEnabled(a_enabled);
    }

    void SetGameConflictWarningsEnabled(RE::StaticFunctionTag*, bool a_enabled)
    {
        KeyRegistry::GetSingleton().SetGameConflictWarningsEnabled(a_enabled);
    }
}

bool PapyrusFunctions::RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
{
    if (!a_vm) {
        logger::error("[HCM] Papyrus VM is null.");
        return false;
    }

    const auto s = Plugin::PapyrusScript.data();

    a_vm->RegisterFunction("CheckKeyConflict",               s, CheckKeyConflict,               true);
    a_vm->RegisterFunction("GetAssignedKeyCount",            s, GetAssignedKeyCount,            true);
    a_vm->RegisterFunction("GetKeyDisplayLeft",              s, GetKeyDisplayLeft,              true);
    a_vm->RegisterFunction("GetKeyDisplayRight",             s, GetKeyDisplayRight,             true);
    a_vm->RegisterFunction("GetKeyDisplayStatus",            s, GetKeyDisplayStatus,            true);
    a_vm->RegisterFunction("GetModConflictCount",            s, GetModConflictCount,            true);
    a_vm->RegisterFunction("GetGameConflictCount",           s, GetGameConflictCount,           true);
    a_vm->RegisterFunction("GetGameKeyCount",                s, GetGameKeyCount,                true);
    a_vm->RegisterFunction("GetGameKeyDisplayLeft",          s, GetGameKeyDisplayLeft,          true);
    a_vm->RegisterFunction("GetGameKeyDisplayRight",         s, GetGameKeyDisplayRight,         true);
    a_vm->RegisterFunction("GetGameKeyDisplayStatus",        s, GetGameKeyDisplayStatus,        true);
    a_vm->RegisterFunction("SetPopupWarningsEnabled",        s, SetPopupWarningsEnabled,        true);
    a_vm->RegisterFunction("ClearOrphans",                   s, ClearOrphans,                   true);
    a_vm->RegisterFunction("ClearRegistry",                  s, ClearRegistry,                  true);
    a_vm->RegisterFunction("SetDebugModeEnabled",            s, SetDebugModeEnabled,            true);
    a_vm->RegisterFunction("SetGameConflictWarningsEnabled", s, SetGameConflictWarningsEnabled, true);

    logger::info("[HCM] Papyrus functions registered on script '{}'.",
                 Plugin::PapyrusScript);
    return true;
}
