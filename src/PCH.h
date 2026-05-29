#pragma once

// ============================================================
//  PCH.h  -  Precompiled header / central definition hub
// ============================================================

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <Windows.h>
#include <ShlObj.h>
#undef GetObject  // Windows.h defines this as GetObjectA/W

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::literals;
namespace logger = SKSE::log;

using FormID   = RE::FormID;

namespace Plugin
{
    inline constexpr std::string_view NAME   { "HotkeyConflictManager" };
    inline constexpr std::string_view AUTHOR { "Glanzer" };
    inline constexpr REL::Version     VERSION{ 1, 0, 0, 0 };

    inline constexpr std::string_view PapyrusScript{ "HCM_API" };

    inline constexpr std::uint32_t MinKeyCode    =   2u;
    inline constexpr std::uint32_t AbsMaxKeyCode = 281u;

    // Default startup window - see Settings.h / HotkeyConflictManager.ini.
    // The runtime value is loaded from the INI at startup and may differ.
    inline constexpr std::int32_t StartupWindowDefault{ 60 };  // seconds
}
