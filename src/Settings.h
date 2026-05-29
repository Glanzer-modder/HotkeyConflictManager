#pragma once
#include "PCH.h"

// ============================================================
//  Settings  -  Reads HotkeyConflictManager.ini from
//               Data/SKSE/Plugins/ at plugin load time.
//
//  Call Settings::GetSingleton().Load() early in SKSEPlugin_Load,
//  before hooks are installed or any registrations occur.
// ============================================================

class Settings
{
public:
    static Settings& GetSingleton() noexcept;

    void Load();

    // Accessors
    std::chrono::seconds StartupWindow() const noexcept { return _startupWindow; }

private:
    Settings()  = default;
    ~Settings() = default;
    Settings(const Settings&)            = delete;
    Settings& operator=(const Settings&) = delete;

    // Parses a simple INI file and calls a_handler for each key=value pair.
    void ParseFile(
        const std::string& a_path,
        std::function<void(const std::string& section,
                           const std::string& key,
                           const std::string& value)> a_handler);

    static std::int32_t ParseInt(const std::string& a_value, std::int32_t a_default);

    // ---- Settings values (defaults match the shipped INI) ----

    // Seconds after game load during which conflict popups are suppressed.
    // Mods re-register their hotkeys during this window; showing popups
    // for those would be false positives.
    std::chrono::seconds _startupWindow{ 60 };
};
