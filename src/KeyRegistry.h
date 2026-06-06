#pragma once
#include "PCH.h"

// ============================================================
//  KeyRegistry
// ============================================================

class KeyRegistry
{
public:
    // One display row per (keyCode, pluginName) pair
    struct DisplayEntry {
        std::string   left;    // e.g. "[ 38] L : CoolMod.esp"
        std::string   right;   // "" / "[MOD CONFLICT]" / "[ORPHAN]" /
                               //    "[GAME CONFLICT]" / "[MOD+GAME CONFLICT]"
        std::int32_t  status;  // 0=normal  1=mod conflict  2=orphan
                               // 3=game conflict  4=mod+game conflict
                               // (game display entries use only 0 and 1)
        std::uint32_t keyCode{ 0 };  // for category detection (keyboard/mouse/controller)
        std::string   plugin;         // raw plugin name for RemoveEntry
    };

    static KeyRegistry& GetSingleton() noexcept;

    // Hook callbacks
    void Register  (std::uint32_t a_keyCode, FormID a_formID);
    void Unregister(std::uint32_t a_keyCode, FormID a_formID);
    void UnregisterAll(FormID a_formID);

    // Conflict query — returns conflicting plugin name or "" if the key is free.
    std::string CheckConflict(std::uint32_t a_keyCode,
                              FormID        a_callerFormID) const;

    // Lifecycle
    void Clear();
    void ClearOrphans();
    void MarkLoadTime();

    // Popup warnings toggle (called from Papyrus on config change)
    void SetPopupWarningsEnabled(bool a_enabled);
    void SetGameConflictWarningsEnabled(bool a_enabled);

    // ControlMap monitoring (called from ControlMapWatcher and main.cpp)
    void                     SnapshotControlMap();
    std::vector<std::string> CheckControlMapChanges();
    void SetDebugModeEnabled(bool a_enabled);

    // SKSE cosave
    void SaveToStream(SKSE::SerializationInterface* a_intfc) const;
    void LoadFromStream(SKSE::SerializationInterface* a_intfc);

    // MCM display — call GetAssignedKeyCount() first to rebuild cache
    std::int32_t GetAssignedKeyCount();
    std::string  GetKeyDisplayLeft  (std::int32_t a_index) const;
    std::string  GetKeyDisplayRight (std::int32_t a_index) const;
    std::int32_t GetKeyDisplayStatus(std::int32_t a_index) const;
    std::int32_t GetModConflictCount()                    const;
    std::int32_t GetGameConflictCount()                   const;
    std::int32_t GetGameKeyCount();
    std::string  GetGameKeyDisplayLeft  (std::int32_t a_index) const;
    std::string  GetGameKeyDisplayRight (std::int32_t a_index) const;
    std::int32_t GetGameKeyDisplayStatus  (std::int32_t a_index) const;
    std::int32_t GetKeyDisplayKeyCode     (std::int32_t a_index) const;
    std::int32_t GetGameKeyDisplayKeyCode (std::int32_t a_index) const;
    std::string  GetKeyDisplayPlugin      (std::int32_t a_index) const;

    // Registry entry removal (user-initiated from Mod Keys page)
    void RemoveEntry(std::uint32_t a_keyCode, const std::string& a_plugin);

    // Unused Keys display — call GetUnusedKeyCount() first to rebuild cache,
    // then use the section counts to iterate each category.
    // Combined index order: keyboard (0..kbCount-1), mouse (kbCount..kbCount+moCount-1),
    // controller (kbCount+moCount..) 
    std::int32_t GetUnusedKeyCount();            // triggers rebuild, returns total
    std::int32_t GetUnusedKeyboardCount()   const;
    std::int32_t GetUnusedMouseCount()      const;
    std::int32_t GetUnusedControllerCount() const;
    std::string  GetUnusedKeyDisplayLeft(std::int32_t a_index) const;

private:
    KeyRegistry()  = default;
    ~KeyRegistry() = default;
    KeyRegistry(const KeyRegistry&)            = delete;
    KeyRegistry& operator=(const KeyRegistry&) = delete;

    bool IsStartupWindow() const;
    std::string GetOwningPluginName(FormID a_formID) const;
    std::string CheckVanillaConflict(std::uint32_t a_keyCode) const;
    bool IsPluginLoaded(RE::TESDataHandler* a_handler,
                        const std::string&  a_name) const;
    std::string GetKeyName(std::uint32_t a_keyCode) const;
    void        RebuildDisplayCache();
    void        RebuildGameDisplayCache();
    void        RebuildUnusedDisplayCache();

    mutable std::shared_mutex _mutex;

    std::unordered_map<std::uint32_t,
                       std::unordered_set<std::string>> _keyToPlugins;
    std::unordered_map<std::string,
                       std::unordered_set<std::uint32_t>> _pluginToKeys;
    std::unordered_map<FormID, std::string> _formToPlugin;

    std::chrono::steady_clock::time_point _loadTime{};

    std::vector<DisplayEntry> _displayEntries;
    std::vector<DisplayEntry> _gameDisplayEntries;
    std::int32_t              _modConflictCount{ 0 };
    std::int32_t              _gameConflictCount{ 0 };

    std::vector<std::string>  _unusedDisplayEntries;
    std::int32_t              _unusedKeyboardCount{ 0 };
    std::int32_t              _unusedMouseCount{ 0 };
    std::int32_t              _unusedControllerCount{ 0 };

    std::atomic<bool> _popupEnabled{ true };
    std::atomic<bool> _gameConflictPopupEnabled{ true };
    std::atomic<bool> _debugEnabled{ false };
    std::atomic<bool> _startupWindowExpiredLogged{ false };

    // ControlMap snapshot - indexed by [actionIndex * k_deviceCount + deviceIndex]
    // 0 = unbound.  Rebuilt by SnapshotControlMap; compared by CheckControlMapChanges.
    std::vector<std::uint32_t> _controlMapSnapshot;
};
