#include "PCH.h"
#include "KeyRegistry.h"
#include "Settings.h"

KeyRegistry& KeyRegistry::GetSingleton() noexcept
{
    static KeyRegistry instance;
    return instance;
}

// ============================================================
//  Startup detection
// ============================================================

void KeyRegistry::MarkLoadTime()
{
    _loadTime = std::chrono::steady_clock::now();
}

bool KeyRegistry::IsStartupWindow() const
{
    return (std::chrono::steady_clock::now() - _loadTime)
           < Settings::GetSingleton().StartupWindow();
}

// ============================================================
//  Popup warnings toggle
// ============================================================

void KeyRegistry::SetPopupWarningsEnabled(bool a_enabled)
{
    if (_popupEnabled.exchange(a_enabled) != a_enabled)
        logger::info("[HCM] Mod conflict warnings {}.", a_enabled ? "enabled" : "disabled");
}

void KeyRegistry::SetGameConflictWarningsEnabled(bool a_enabled)
{
    if (_gameConflictPopupEnabled.exchange(a_enabled) != a_enabled)
        logger::info("[HCM] Game conflict warnings {}.", a_enabled ? "enabled" : "disabled");
}

void KeyRegistry::SetDebugModeEnabled(bool a_enabled)
{
    if (_debugEnabled.exchange(a_enabled) != a_enabled)
        logger::info("[HCM] Debug mode {}.", a_enabled ? "enabled" : "disabled");
}

// ============================================================
//  Hook callbacks
// ============================================================

void KeyRegistry::Register(std::uint32_t a_keyCode, FormID a_formID)
{
    if (a_keyCode < Plugin::MinKeyCode || a_keyCode > Plugin::AbsMaxKeyCode)
        return;

    std::string pluginName = GetOwningPluginName(a_formID);
    if (pluginName.empty()) {
        logger::warn("[HCM] Could not resolve plugin name for FormID "
                     "{:#010X} - skipping.", a_formID);
        return;
    }

    const bool        isStartup = IsStartupWindow();
    if (!isStartup && !_startupWindowExpiredLogged.exchange(true)) {
        logger::info("[HCM] Startup window expired - conflict popups now active.");
    }
    const std::string keyName   = GetKeyName(a_keyCode);

    // Check vanilla game action conflict before acquiring our lock -
    // reads only game engine data, not our maps.
    // Skipped during startup to avoid noise from mass re-registration on load.
    const std::string vanillaConflict =
        isStartup ? std::string{} : CheckVanillaConflict(a_keyCode);

    std::unique_lock lock(_mutex);
    auto* handler = RE::TESDataHandler::GetSingleton();
    _formToPlugin[a_formID] = pluginName;

    // Collect all loaded plugins that share this key (excluding the registrant itself).
    std::vector<std::string> modConflicts;
    if (auto it = _keyToPlugins.find(a_keyCode); it != _keyToPlugins.end()) {
        for (const auto& existing : it->second) {
            if (existing != pluginName &&
                (!handler || IsPluginLoaded(handler, existing)))
                modConflicts.push_back(existing);
        }
    }
    // Build a single display string for use in logs and the popup.
    std::string modConflict;
    for (const auto& c : modConflicts) {
        if (!modConflict.empty()) modConflict += ", ";
        modConflict += c;
    }

    // Log and optionally show popup for any conflict found
    if (!vanillaConflict.empty() || !modConflict.empty()) {
        if (isStartup) {
            logger::info(
                "[HCM] Startup conflict: key {} ({}) by '{}' "
                "- gameaction='{}' modfile='{}' - logged only.",
                a_keyCode, keyName, pluginName, vanillaConflict, modConflict);
        } else {
            logger::info(
                "[HCM] User conflict: key {} ({}) by '{}' "
                "- gameaction='{}' modfile='{}' - checking popup flags.",
                a_keyCode, keyName, pluginName, vanillaConflict, modConflict);

            // Conflicts detected while the Journal Menu is open are
            // always shown — the user is actively assigning keys and needs
            // to know.  Outside the Journal Menu, each flag controls its
            // own conflict type independently.
            //
            // Note: the Journal Menu open event does not reliably fire in
            // Skyrim SE (the menu persists in memory), so we use IsMenuOpen
            // directly.  This is safe because when a mod registers a key in
            // its MCM, the Journal Menu is still fully open and there is no
            // timing race.
            auto* ui = RE::UI::GetSingleton();
            const bool journalOpen =
                ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME);

            const bool wantMod  = !modConflict.empty()     &&
                                  (_popupEnabled.load()             || journalOpen);
            const bool wantGame = !vanillaConflict.empty() &&
                                  (_gameConflictPopupEnabled.load() || journalOpen);

            if (wantMod || wantGame) {
                std::string msg = fmt::format(
                    "Hotkey Conflict Detected!\n\n"
                    "Key '{}' (code {}) is being registered by:\n"
                    "  {}",
                    keyName, a_keyCode, pluginName);

                if (wantMod) {
                    msg += modConflicts.size() == 1
                        ? fmt::format("\n\nConflicts with mod:\n  {}", modConflict)
                        : "\n\nConflicts with mods:";
                    if (modConflicts.size() > 1)
                        for (const auto& c : modConflicts)
                            msg += fmt::format("\n  {}", c);
                }

                if (wantGame)
                    msg += fmt::format(
                        "\n\nConflicts with game action:\n  {}", vanillaConflict);

                msg += "\n\nYou may want to reassign one of these keys.";

                SKSE::GetTaskInterface()->AddTask([msg]() {
                    RE::DebugMessageBox(msg.c_str());
                });
            }
        }
    }

    _keyToPlugins[a_keyCode].insert(pluginName);
    _pluginToKeys[pluginName].insert(a_keyCode);

    if (_debugEnabled.load()) {
        const std::string note = fmt::format(
            "[HCM] +{} ({}) : {}", GetKeyName(a_keyCode), a_keyCode, pluginName);
        SKSE::GetTaskInterface()->AddTask([note]() {
            RE::DebugNotification(note.c_str());
        });
    }
}

// ============================================================
//  Shared game action list
//  Used by both CheckVanillaConflict and RebuildGameDisplayCache.
// ============================================================

namespace
{
    using UE = RE::UserEvents;

    static const RE::BSFixedString UE::* k_gameActions[] = {
        &UE::forward,        &UE::back,           &UE::strafeLeft,     &UE::strafeRight,
        &UE::activate,       &UE::leftAttack,     &UE::rightAttack,    &UE::dualAttack,
        &UE::pause,          &UE::readyWeapon,    &UE::togglePOV,      &UE::jump,
        &UE::journal,        &UE::sprint,         &UE::sneak,          &UE::shout,
        &UE::grab,           &UE::toggleRun,      &UE::autoMove,
        &UE::quicksave,      &UE::quickload,
        &UE::inventory,      &UE::stats,          &UE::map,            &UE::console,
        &UE::tweenMenu,      &UE::takeAll,        &UE::accept,         &UE::cancel,
        &UE::hotkey1,        &UE::hotkey2,        &UE::hotkey3,        &UE::hotkey4,
        &UE::hotkey5,        &UE::hotkey6,        &UE::hotkey7,        &UE::hotkey8,
        &UE::quickInventory, &UE::quickMagic,     &UE::quickStats,     &UE::quickMap,
        &UE::wait,           &UE::favorites,      &UE::toggleFavorite,
    };

    // Device list shared by all ControlMap-querying functions.
    // Offset converts the device-local key index to our unified keyCode scheme.
    struct DeviceInfo {
        RE::INPUT_DEVICE device;
        std::uint32_t    offset;
    };

    static const DeviceInfo k_devices[] = {
        { RE::INPUT_DEVICE::kKeyboard, 0   },  // keyCodes   0-255
        { RE::INPUT_DEVICE::kMouse,    256 },  // keyCodes 256-265
        { RE::INPUT_DEVICE::kGamepad,  266 },  // keyCodes 266+
    };

    constexpr std::size_t k_gameActionCount = std::size(k_gameActions);
    constexpr std::size_t k_deviceCount     = std::size(k_devices);

    // All known DX scan codes and their display names.
    // Promoted to namespace scope so RebuildUnusedDisplayCache can
    // iterate the full set.  GetKeyName reads from this table too.
    // Adding a new entry here automatically picks it up on every
    // page that displays key information.
    static const std::unordered_map<std::uint32_t, std::string_view> k_names{
        {0x01,"Esc"   },{0x02,"1"     },{0x03,"2"     },{0x04,"3"     },{0x05,"4"     },
        {0x06,"5"     },{0x07,"6"     },{0x08,"7"     },{0x09,"8"     },{0x0A,"9"     },
        {0x0B,"0"     },{0x0C,"-"     },{0x0D,"="     },{0x0E,"Bksp"  },{0x0F,"Tab"   },
        {0x10,"Q"     },{0x11,"W"     },{0x12,"E"     },{0x13,"R"     },{0x14,"T"     },
        {0x15,"Y"     },{0x16,"U"     },{0x17,"I"     },{0x18,"O"     },{0x19,"P"     },
        {0x1A,"["     },{0x1B,"]"     },{0x1C,"Enter" },{0x1D,"LCtrl" },
        {0x1E,"A"     },{0x1F,"S"     },{0x20,"D"     },{0x21,"F"     },{0x22,"G"     },
        {0x23,"H"     },{0x24,"J"     },{0x25,"K"     },{0x26,"L"     },{0x27,";"     },
        {0x28,"'"    },{0x29,"`"     },{0x2A,"LShft" },{0x2B,"\\"  },
        {0x2C,"Z"     },{0x2D,"X"     },{0x2E,"C"     },{0x2F,"V"     },{0x30,"B"     },
        {0x31,"N"     },{0x32,"M"     },{0x33,","     },{0x34,"."     },{0x35,"/"     },
        {0x36,"RShft" },{0x37,"Num*"  },{0x38,"LAlt"  },{0x39,"Space" },
        {0x3A,"Caps"  },{0x3B,"F1"    },{0x3C,"F2"    },{0x3D,"F3"    },{0x3E,"F4"    },
        {0x3F,"F5"    },{0x40,"F6"    },{0x41,"F7"    },{0x42,"F8"    },{0x43,"F9"    },
        {0x44,"F10"   },{0x45,"NumLk" },{0x46,"ScrlLk"},{0x47,"NUM7"  },{0x48,"NUM8"  },
        {0x49,"NUM9"  },{0x4A,"NUM-"  },{0x4B,"NUM4"  },{0x4C,"NUM5"  },{0x4D,"NUM6"  },
        {0x4E,"NUM+"  },{0x4F,"NUM1"  },{0x50,"NUM2"  },{0x51,"NUM3"  },{0x52,"NUM0"  },
        {0x53,"NUM."  },{0x57,"F11"   },{0x58,"F12"   },{0xC5,"Pause" },
        {0x9C,"NEnter"},{0x9D,"RCtrl" },{0xB5,"NUM/"  },{0xB8,"RAlt"  },
        {0xC7,"Home"  },{0xC8,"Up"    },{0xC9,"PgUp"  },{0xCB,"Left"  },
        {0xCD,"Right" },{0xCF,"End"   },{0xD0,"Down"  },{0xD1,"PgDn"  },
        {0xD2,"Ins"   },{0xD3,"Del"   },
        // Mouse
        {256,"LMouse" },{257,"RMouse" },{258,"MMouse" },
        {259,"Mouse4" },{260,"Mouse5" },{261,"Mouse6" },
        {262,"Mouse7" },{263,"Mouse8" },
        {264,"MWhlUp" },{265,"MWhlDn" },
        // Controller
        {266,"DPadUp" },{267,"DPadDwn"},{268,"DPadLft"},{269,"DPadRt" },
        {270,"Start"  },{271,"Back"   },{272,"L3"     },{273,"R3"     },
        {274,"LBtn"   },{275,"RBtn"   },{276,"ContrA" },{277,"ContrB" },
        {278,"ContrX" },{279,"ContrY" },{280,"LThumb" },{281,"RThumb" },
    };

    // Converts the bitmask value returned by ControlMap::GetMappedKey for
    // the kGamepad device into our sequential keyCode scheme (266+).
    //
    // Skyrim's ControlMap stores gamepad buttons as DirectInput bitmasks
    // (each button is a power-of-2 bit) rather than sequential indices.
    // This table maps each known bitmask to our GetKeyName convention.
    //
    // Returns 0 if the bitmask is not recognised (caller should skip).
    static std::uint32_t GamepadBitmaskToKeyCode(std::uint32_t a_bitmask)
    {
        static const std::unordered_map<std::uint32_t, std::uint32_t> k_table{
            {1,      266},  // DPadUp
            {2,      267},  // DPadDwn
            {4,      268},  // DPadLft
            {8,      269},  // DPadRt
            {16,     270},  // Start
            {32,     271},  // Back
            {64,     272},  // L3
            {128,    273},  // R3
            {256,    274},  // LBtn
            {512,    275},  // RBtn
            {1024,   280},  // LThumb (LT)
            {2048,   281},  // RThumb (RT)
            {4096,   276},  // ContrA
            {8192,   277},  // ContrB
            {16384,  278},  // ContrX
            {32768,  279},  // ContrY
        };
        auto it = k_table.find(a_bitmask);
        return it != k_table.end() ? it->second : 0;
    }
}

// ============================================================
//  CheckVanillaConflict
//  Checks whether a_keyCode is already bound to a game action
//  in the gameplay context, using the same ControlMap data that
//  SkyUI queries to generate its conflictControl parameter.
//  Returns the action name (e.g. "Journal", "Map") or "" if
//  no vanilla binding is found for this key.
// ============================================================

std::string KeyRegistry::CheckVanillaConflict(std::uint32_t a_keyCode) const
{
    auto* cm = RE::ControlMap::GetSingleton();
    auto* ue = RE::UserEvents::GetSingleton();
    if (!cm || !ue) return {};

    // Determine which input device owns this keyCode in our scheme:
    //   0-255   -> keyboard (device index == keyCode)
    //   256-265 -> mouse    (device index == keyCode - 256)
    //   266+    -> gamepad  (GetMappedKey returns a bitmask, not a sequential index)
    RE::INPUT_DEVICE  device;
    std::uint32_t     deviceKey = 0;  // only used for keyboard and mouse

    if (a_keyCode < 256) {
        device    = RE::INPUT_DEVICE::kKeyboard;
        deviceKey = a_keyCode;
    } else if (a_keyCode < 266) {
        device    = RE::INPUT_DEVICE::kMouse;
        deviceKey = a_keyCode - 256;
    } else {
        device = RE::INPUT_DEVICE::kGamepad;
        // deviceKey unused for gamepad - bitmask conversion used instead
    }

    // 0xFF is the sentinel returned by GetMappedKey when no key is bound.
    constexpr std::uint32_t kUnmapped = 0xFF;

    for (auto memberPtr : k_gameActions) {
        const auto& actionName = ue->*memberPtr;
        if (actionName.empty()) continue;

        const auto mapped = cm->GetMappedKey(actionName, device);

        if (device == RE::INPUT_DEVICE::kGamepad) {
            // ControlMap returns a bitmask for gamepad buttons.
            // Convert to our sequential scheme and compare directly.
            const std::uint32_t ourCode = GamepadBitmaskToKeyCode(mapped);
            if (ourCode != 0 && ourCode == a_keyCode)
                return std::string(actionName.c_str());
        } else {
            if (mapped != kUnmapped && mapped == deviceKey)
                return std::string(actionName.c_str());
        }
    }

    return {};
}

void KeyRegistry::Unregister(std::uint32_t a_keyCode, FormID a_formID)
{
    std::string pluginName = GetOwningPluginName(a_formID);
    if (pluginName.empty()) {
        std::shared_lock lock(_mutex);
        if (auto it = _formToPlugin.find(a_formID);
            it != _formToPlugin.end())
            pluginName = it->second;
    }
    if (pluginName.empty()) return;

    std::unique_lock lock(_mutex);

    if (auto kit = _keyToPlugins.find(a_keyCode);
        kit != _keyToPlugins.end()) {
        kit->second.erase(pluginName);
        if (kit->second.empty())
            _keyToPlugins.erase(kit);
    }
    if (auto pit = _pluginToKeys.find(pluginName);
        pit != _pluginToKeys.end()) {
        pit->second.erase(a_keyCode);
        if (pit->second.empty())
            _pluginToKeys.erase(pit);
    }

    if (_debugEnabled.load()) {
        const std::string note = fmt::format(
            "[HCM] -{} ({}) : {}",
            GetKeyName(a_keyCode), a_keyCode, pluginName);
        SKSE::GetTaskInterface()->AddTask([note]() {
            RE::DebugNotification(note.c_str());
        });
    }
}

void KeyRegistry::UnregisterAll(FormID a_formID)
{
    std::string pluginName = GetOwningPluginName(a_formID);
    if (pluginName.empty()) {
        std::shared_lock lock(_mutex);
        if (auto it = _formToPlugin.find(a_formID);
            it != _formToPlugin.end())
            pluginName = it->second;
    }
    if (pluginName.empty()) return;

    std::unique_lock lock(_mutex);

    auto pit = _pluginToKeys.find(pluginName);
    if (pit == _pluginToKeys.end()) return;

    for (std::uint32_t key : pit->second) {
        if (auto kit = _keyToPlugins.find(key);
            kit != _keyToPlugins.end()) {
            kit->second.erase(pluginName);
            if (kit->second.empty())
                _keyToPlugins.erase(kit);
        }
    }
    _pluginToKeys.erase(pit);

    if (_debugEnabled.load()) {
        const std::string note = fmt::format(
            "[HCM] -ALL keys : {}", pluginName);
        SKSE::GetTaskInterface()->AddTask([note]() {
            RE::DebugNotification(note.c_str());
        });
    }
}

// ============================================================
//  Conflict query
// ============================================================

std::string KeyRegistry::CheckConflict(std::uint32_t a_keyCode,
                                        FormID        a_callerFormID) const
{
    std::shared_lock lock(_mutex);

    std::string callerPlugin;
    if (auto it = _formToPlugin.find(a_callerFormID);
        it != _formToPlugin.end())
        callerPlugin = it->second;

    auto it = _keyToPlugins.find(a_keyCode);
    if (it == _keyToPlugins.end()) return {};

    for (const auto& plugin : it->second)
        if (plugin != callerPlugin)
            return plugin;

    return {};
}

// ============================================================
//  Lifecycle
// ============================================================

void KeyRegistry::Clear()
{
    std::unique_lock lock(_mutex);
    _keyToPlugins.clear();
    _pluginToKeys.clear();
    _formToPlugin.clear();
    _displayEntries.clear();
    _gameDisplayEntries.clear();
    _modConflictCount  = 0;
    _gameConflictCount = 0;
    _unusedDisplayEntries.clear();
    _unusedKeyboardCount   = 0;
    _unusedMouseCount      = 0;
    _unusedControllerCount = 0;
}

void KeyRegistry::ClearOrphans()
{
    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!handler) {
        logger::error("[HCM] ClearOrphans: TESDataHandler not available.");
        return;
    }

    std::unique_lock lock(_mutex);

    // Collect names of plugins that are no longer in the load order
    std::vector<std::string> orphans;
    for (const auto& [pluginName, keys] : _pluginToKeys)
        if (!IsPluginLoaded(handler, pluginName))
            orphans.push_back(pluginName);

    // Remove orphaned entries from both maps
    for (const auto& plugin : orphans) {
        auto pit = _pluginToKeys.find(plugin);
        if (pit == _pluginToKeys.end()) continue;

        for (std::uint32_t key : pit->second) {
            if (auto kit = _keyToPlugins.find(key); kit != _keyToPlugins.end()) {
                kit->second.erase(plugin);
                if (kit->second.empty())
                    _keyToPlugins.erase(kit);
            }
        }
        _pluginToKeys.erase(pit);
    }

    logger::info("[HCM] Cleared {} orphaned plugin(s) from registry.", orphans.size());
}

// ============================================================
//  SKSE cosave
// ============================================================

void KeyRegistry::SaveToStream(SKSE::SerializationInterface* a_intfc) const
{
    std::shared_lock lock(_mutex);

    const auto count = static_cast<std::uint32_t>(_keyToPlugins.size());
    a_intfc->WriteRecordData(&count, sizeof(count));

    for (const auto& [keyCode, plugins] : _keyToPlugins) {
        a_intfc->WriteRecordData(&keyCode, sizeof(keyCode));
        const auto pluginCount = static_cast<std::uint32_t>(plugins.size());
        a_intfc->WriteRecordData(&pluginCount, sizeof(pluginCount));
        for (const auto& name : plugins) {
            const auto len = static_cast<std::uint32_t>(name.size());
            a_intfc->WriteRecordData(&len, sizeof(len));
            a_intfc->WriteRecordData(name.data(), len);
        }
    }

    logger::info("[HCM] Saved {} key entries to cosave.", count);
}

void KeyRegistry::LoadFromStream(SKSE::SerializationInterface* a_intfc)
{
    std::unique_lock lock(_mutex);
    _keyToPlugins.clear();
    _pluginToKeys.clear();

    std::uint32_t count = 0;
    if (!a_intfc->ReadRecordData(&count, sizeof(count))) return;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t keyCode = 0;
        if (!a_intfc->ReadRecordData(&keyCode, sizeof(keyCode))) break;
        std::uint32_t pluginCount = 0;
        if (!a_intfc->ReadRecordData(&pluginCount, sizeof(pluginCount))) break;
        for (std::uint32_t j = 0; j < pluginCount; ++j) {
            std::uint32_t len = 0;
            if (!a_intfc->ReadRecordData(&len, sizeof(len))) break;
            std::string name(len, '\0');
            if (!a_intfc->ReadRecordData(name.data(), len)) break;
            _keyToPlugins[keyCode].insert(name);
            _pluginToKeys[name].insert(keyCode);
        }
    }

    logger::info("[HCM] Loaded {} key entries from cosave.", count);
}

// ============================================================
//  MCM display cache
// ============================================================

bool KeyRegistry::IsPluginLoaded(RE::TESDataHandler* a_handler,
                                  const std::string&  a_name) const
{
    // LookupLoadedModByName / LookupLoadedLightModByName use case-sensitive
    // comparison internally, which fails for light plugins and sometimes for
    // regular ones.  Iterate compiledFileCollection directly with _stricmp.
    const auto& col =
        REL::RelocateMember<RE::TESFileCollection>(a_handler, 0xD70, 0);

    for (auto* file : col.files) {
        if (file && _stricmp(file->GetFilename().data(), a_name.c_str()) == 0)
            return true;
    }
    for (auto* file : col.smallFiles) {
        if (file && _stricmp(file->GetFilename().data(), a_name.c_str()) == 0)
            return true;
    }
    return false;
}

void KeyRegistry::RebuildDisplayCache()
{
    // Called under write lock from GetAssignedKeyCount().
    _displayEntries.clear();
    _modConflictCount  = 0;
    _gameConflictCount = 0;

    auto* handler = RE::TESDataHandler::GetSingleton();

    std::vector<std::uint32_t> sortedKeys;
    sortedKeys.reserve(_keyToPlugins.size());
    for (const auto& [k, _] : _keyToPlugins)
        sortedKeys.push_back(k);
    std::sort(sortedKeys.begin(), sortedKeys.end());

    for (std::uint32_t keyCode : sortedKeys) {
        const auto& plugins = _keyToPlugins.at(keyCode);

        // Only count plugins that are actually loaded for conflict detection.
        int activeCount = 0;
        for (const auto& plugin : plugins)
            if (!handler || IsPluginLoaded(handler, plugin))
                ++activeCount;

        const bool hasModConflict = activeCount > 1;

        // Check vanilla game action conflict.
        // CheckVanillaConflict reads only game engine data - safe to call
        // while holding our write lock.
        const bool hasGameConflict = !CheckVanillaConflict(keyCode).empty();

        // Update per-keyCode counts (once per keyCode, not per plugin entry)
        if (hasModConflict)                      ++_modConflictCount;
        if (hasGameConflict && activeCount > 0)  ++_gameConflictCount;

        std::vector<std::string> sorted(plugins.begin(), plugins.end());
        std::sort(sorted.begin(), sorted.end());

        for (const auto& plugin : sorted) {
            const bool orphaned =
                handler ? !IsPluginLoaded(handler, plugin) : false;

            std::int32_t status;
            std::string  right;

            if (orphaned) {
                status = 2;
                right  = "[ORPHAN]";
            } else if (hasModConflict && hasGameConflict) {
                status = 4;
                right  = "[MOD+GAME CONFLICT]";
            } else if (hasModConflict) {
                status = 1;
                right  = "[MOD CONFLICT]";
            } else if (hasGameConflict) {
                status = 3;
                right  = "[GAME CONFLICT]";
            } else {
                status = 0;
                right  = "";
            }

            _displayEntries.push_back({
                fmt::format("[{:3}] {:6} : {}",
                    keyCode, GetKeyName(keyCode), plugin),
                right,
                status,
                keyCode,
                plugin
            });
        }
    }
}

void KeyRegistry::RebuildGameDisplayCache()
{
    // Called under write lock from GetGameKeyCount().
    // Iterates all game actions across all input devices, building one display
    // entry per bound key.  Entries are sorted by keyCode.
    _gameDisplayEntries.clear();
    _gameConflictCount = 0;

    auto* cm      = RE::ControlMap::GetSingleton();
    auto* ue      = RE::UserEvents::GetSingleton();
    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!cm || !ue) return;

    constexpr std::uint32_t kUnmapped = 0xFF;

    // Uses file-scope k_devices array defined in the anonymous namespace above.

    struct RawEntry {
        std::uint32_t keyCode;
        std::string   actionName;
        bool          hasModConflict;
    };
    std::vector<RawEntry> raw;

    for (auto memberPtr : k_gameActions) {
        const auto& actionName = ue->*memberPtr;
        if (actionName.empty()) continue;

        for (const auto& [device, offset] : k_devices) {
            const auto mapped = cm->GetMappedKey(actionName, device);

            // Determine our keyCode for this binding.
            // Gamepad: GetMappedKey returns a bitmask - convert to sequential scheme.
            // Keyboard/mouse: add the device offset to the returned index.
            std::uint32_t fullKeyCode = 0;
            if (device == RE::INPUT_DEVICE::kGamepad) {
                fullKeyCode = GamepadBitmaskToKeyCode(mapped);
                if (fullKeyCode == 0) continue;  // unknown bitmask, skip
            } else {
                if (mapped == kUnmapped) continue;
                fullKeyCode = mapped + offset;
            }

            // Check if any loaded mod has registered this key
            bool hasModConflict = false;
            if (handler) {
                if (auto it = _keyToPlugins.find(fullKeyCode);
                    it != _keyToPlugins.end()) {
                    for (const auto& plugin : it->second) {
                        if (IsPluginLoaded(handler, plugin)) {
                            hasModConflict = true;
                            break;
                        }
                    }
                }
            }

            raw.push_back({ fullKeyCode, std::string(actionName.c_str()),
                            hasModConflict });
        }
    }

    // Sort by keyCode for consistent display
    std::sort(raw.begin(), raw.end(),
        [](const RawEntry& a, const RawEntry& b) {
            return a.keyCode < b.keyCode;
        });

    for (const auto& entry : raw) {
        if (entry.hasModConflict) ++_gameConflictCount;

        _gameDisplayEntries.push_back({
            fmt::format("[{:3}] {:6} : {}",
                entry.keyCode, GetKeyName(entry.keyCode), entry.actionName),
            entry.hasModConflict ? "[MOD CONFLICT]" : "",
            entry.hasModConflict ? 1 : 0,
            entry.keyCode
        });
    }
}

std::int32_t KeyRegistry::GetAssignedKeyCount()
{
    std::unique_lock lock(_mutex);
    RebuildDisplayCache();
    return static_cast<std::int32_t>(_displayEntries.size());
}

std::string KeyRegistry::GetKeyDisplayLeft(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_displayEntries.size()))
        return {};
    return _displayEntries[a_index].left;
}

std::string KeyRegistry::GetKeyDisplayRight(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_displayEntries.size()))
        return {};
    return _displayEntries[a_index].right;
}

std::int32_t KeyRegistry::GetKeyDisplayStatus(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_displayEntries.size()))
        return 0;
    return _displayEntries[a_index].status;
}

std::int32_t KeyRegistry::GetModConflictCount() const
{
    std::shared_lock lock(_mutex);
    return _modConflictCount;
}

std::int32_t KeyRegistry::GetGameConflictCount() const
{
    std::shared_lock lock(_mutex);
    return _gameConflictCount;
}

std::int32_t KeyRegistry::GetKeyDisplayKeyCode(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_displayEntries.size()))
        return 0;
    return static_cast<std::int32_t>(_displayEntries[a_index].keyCode);
}

std::string KeyRegistry::GetKeyDisplayPlugin(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_displayEntries.size()))
        return {};
    return _displayEntries[a_index].plugin;
}

void KeyRegistry::RemoveEntry(std::uint32_t a_keyCode, const std::string& a_plugin)
{
    std::unique_lock lock(_mutex);

    // Remove from key→plugins map
    if (auto it = _keyToPlugins.find(a_keyCode); it != _keyToPlugins.end()) {
        it->second.erase(a_plugin);
        if (it->second.empty())
            _keyToPlugins.erase(it);
    }

    // Remove from plugin→keys map
    if (auto it = _pluginToKeys.find(a_plugin); it != _pluginToKeys.end()) {
        it->second.erase(a_keyCode);
        if (it->second.empty())
            _pluginToKeys.erase(it);
    }

    logger::info("[HCM] Registry entry removed by user: key {} ({}) from '{}'.",
        a_keyCode, GetKeyName(a_keyCode), a_plugin);
}

std::int32_t KeyRegistry::GetGameKeyDisplayKeyCode(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_gameDisplayEntries.size()))
        return 0;
    return static_cast<std::int32_t>(_gameDisplayEntries[a_index].keyCode);
}

// ============================================================
//  ControlMap snapshot and change detection
// ============================================================

void KeyRegistry::SnapshotControlMap()
{
    auto* cm = RE::ControlMap::GetSingleton();
    auto* ue = RE::UserEvents::GetSingleton();
    if (!cm || !ue) return;

    const std::size_t total = k_gameActionCount * k_deviceCount;
    std::vector<std::uint32_t> snapshot(total, 0);

    for (std::size_t ai = 0; ai < k_gameActionCount; ++ai) {
        const auto& actionName = ue->*(k_gameActions[ai]);
        if (actionName.empty()) continue;

        for (std::size_t di = 0; di < k_deviceCount; ++di) {
            const auto& dev    = k_devices[di];
            const auto  mapped = cm->GetMappedKey(actionName, dev.device);

            std::uint32_t keyCode = 0;
            if (dev.device == RE::INPUT_DEVICE::kGamepad) {
                keyCode = GamepadBitmaskToKeyCode(mapped);
            } else if (mapped != 0xFF) {
                keyCode = mapped + dev.offset;
            }

            snapshot[ai * k_deviceCount + di] = keyCode;
        }
    }

    std::unique_lock lock(_mutex);
    _controlMapSnapshot = std::move(snapshot);
    logger::info("[HCM] ControlMap snapshot taken ({} entries).", total);
}

std::vector<std::string> KeyRegistry::CheckControlMapChanges()
{
    // CheckControlMapChanges is only called on Journal Menu close
    // (via ControlMapWatcher) so it is always in a Journal-context
    // event and should always fire regardless of toggle state.

    auto* cm      = RE::ControlMap::GetSingleton();
    auto* ue      = RE::UserEvents::GetSingleton();
    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!cm || !ue) return {};

    // Build new snapshot outside the lock
    const std::size_t total = k_gameActionCount * k_deviceCount;
    std::vector<std::uint32_t> newSnapshot(total, 0);

    for (std::size_t ai = 0; ai < k_gameActionCount; ++ai) {
        const auto& actionName = ue->*(k_gameActions[ai]);
        if (actionName.empty()) continue;

        for (std::size_t di = 0; di < k_deviceCount; ++di) {
            const auto& dev    = k_devices[di];
            const auto  mapped = cm->GetMappedKey(actionName, dev.device);

            std::uint32_t keyCode = 0;
            if (dev.device == RE::INPUT_DEVICE::kGamepad) {
                keyCode = GamepadBitmaskToKeyCode(mapped);
            } else if (mapped != 0xFF) {
                keyCode = mapped + dev.offset;
            }

            newSnapshot[ai * k_deviceCount + di] = keyCode;
        }
    }

    std::unique_lock lock(_mutex);
    std::vector<std::string> conflicts;

    if (_controlMapSnapshot.size() == total) {
        for (std::size_t ai = 0; ai < k_gameActionCount; ++ai) {
            const auto& actionName = ue->*(k_gameActions[ai]);
            if (actionName.empty()) continue;

            for (std::size_t di = 0; di < k_deviceCount; ++di) {
                const std::size_t    idx     = ai * k_deviceCount + di;
                const std::uint32_t  oldCode = _controlMapSnapshot[idx];
                const std::uint32_t  newCode = newSnapshot[idx];

                if (newCode == oldCode || newCode == 0) continue;

                // Binding changed — check whether any loaded mods use this key.
                // Collect all conflicting mods so the popup names every one.
                auto it = _keyToPlugins.find(newCode);
                if (it == _keyToPlugins.end()) continue;

                std::vector<std::string> bindingConflicts;
                for (const auto& plugin : it->second) {
                    if (!handler || IsPluginLoaded(handler, plugin))
                        bindingConflicts.push_back(plugin);
                }
                if (bindingConflicts.empty()) continue;

                // Build mod list for message (singular or plural)
                std::string modList;
                for (const auto& m : bindingConflicts) {
                    modList += fmt::format("\n  {}", m);
                }
                const std::string modHeader = bindingConflicts.size() == 1
                    ? fmt::format("which is also registered by:\n  {}",
                                  bindingConflicts[0])
                    : "which is also registered by:" + modList;

                logger::info(
                    "[HCM] ControlMap conflict: action '{}' remapped to "
                    "key {} ({}) - conflicts with '{}'.",
                    actionName.c_str(), newCode,
                    GetKeyName(newCode), modList);

                conflicts.push_back(fmt::format(
                    "Game Key Conflict Detected!\n\n"
                    "The game action '{}' was remapped to\n"
                    "key '{}' (code {}),\n"
                    "{}\n\n"
                    "You may want to reassign one of these keys.",
                    actionName.c_str(),
                    GetKeyName(newCode),
                    newCode,
                    modHeader));
            }
        }
    }

    // Always update the snapshot so next comparison has a fresh baseline
    _controlMapSnapshot = std::move(newSnapshot);

    return conflicts;
}

std::int32_t KeyRegistry::GetGameKeyCount()
{
    std::unique_lock lock(_mutex);
    RebuildGameDisplayCache();
    return static_cast<std::int32_t>(_gameDisplayEntries.size());
}

std::string KeyRegistry::GetGameKeyDisplayLeft(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_gameDisplayEntries.size()))
        return {};
    return _gameDisplayEntries[a_index].left;
}

std::string KeyRegistry::GetGameKeyDisplayRight(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_gameDisplayEntries.size()))
        return {};
    return _gameDisplayEntries[a_index].right;
}

std::int32_t KeyRegistry::GetGameKeyDisplayStatus(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_gameDisplayEntries.size()))
        return 0;
    return _gameDisplayEntries[a_index].status;
}

// ============================================================
//  GetOwningPluginName
// ============================================================

std::string KeyRegistry::GetOwningPluginName(FormID a_formID) const
{
    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!handler) return {};

    const std::uint8_t modIndex = static_cast<std::uint8_t>(a_formID >> 24);

    if (modIndex == 0xFE) {
        const std::uint16_t eslIndex =
            static_cast<std::uint16_t>((a_formID >> 12) & 0x0FFF);

        if SKYRIM_REL_VR_CONSTEXPR (!REL::Module::IsVR()) {
            const auto& collection =
                REL::RelocateMember<RE::TESFileCollection>(handler, 0xD70, 0);
            if (eslIndex < static_cast<std::uint16_t>(
                    collection.smallFiles.size())) {
                const RE::TESFile* file = collection.smallFiles[eslIndex];
                if (file) return std::string(file->GetFilename());
            }
        } else {
            const RE::TESFile* file =
                handler->LookupLoadedLightModByIndex(eslIndex);
            if (file) return std::string(file->GetFilename());
        }
    } else {
        const RE::TESFile* file = handler->LookupLoadedModByIndex(modIndex);
        if (file) return std::string(file->GetFilename());
    }
    return {};
}

// ============================================================
//  Unused Keys display cache
//  Iterates all keys in k_names, skipping any that are
//  registered by a loaded mod or bound to a game action.
//  Results are in scan-code order: keyboard, mouse, controller.
// ============================================================

void KeyRegistry::RebuildUnusedDisplayCache()
{
    // Called under write lock from GetUnusedKeyCount().
    _unusedDisplayEntries.clear();
    _unusedKeyboardCount   = 0;
    _unusedMouseCount      = 0;
    _unusedControllerCount = 0;

    auto* handler = RE::TESDataHandler::GetSingleton();

    // Sort all known key codes for consistent display order
    std::vector<std::uint32_t> sorted;
    sorted.reserve(k_names.size());
    for (const auto& [code, _] : k_names)
        sorted.push_back(code);
    std::sort(sorted.begin(), sorted.end());

    for (std::uint32_t keyCode : sorted) {
        // Skip if any loaded mod has registered this key
        if (auto it = _keyToPlugins.find(keyCode); it != _keyToPlugins.end()) {
            bool anyLoaded = false;
            for (const auto& plugin : it->second) {
                if (!handler || IsPluginLoaded(handler, plugin)) {
                    anyLoaded = true;
                    break;
                }
            }
            if (anyLoaded) continue;
        }

        // Skip if bound to a game action.
        // CheckVanillaConflict reads only game engine data - safe while holding lock.
        if (!CheckVanillaConflict(keyCode).empty()) continue;

        // Key is unassigned - add to display using shorter format
        const auto nameIt = k_names.find(keyCode);
        _unusedDisplayEntries.push_back(
            fmt::format("[{}] {}", keyCode,
                        nameIt != k_names.end() ? nameIt->second : "?"));

        if      (keyCode < 256) ++_unusedKeyboardCount;
        else if (keyCode < 266) ++_unusedMouseCount;
        else                    ++_unusedControllerCount;
    }
}

std::int32_t KeyRegistry::GetUnusedKeyCount()
{
    std::unique_lock lock(_mutex);
    RebuildUnusedDisplayCache();
    return static_cast<std::int32_t>(_unusedDisplayEntries.size());
}

std::int32_t KeyRegistry::GetUnusedKeyboardCount() const
{
    std::shared_lock lock(_mutex);
    return _unusedKeyboardCount;
}

std::int32_t KeyRegistry::GetUnusedMouseCount() const
{
    std::shared_lock lock(_mutex);
    return _unusedMouseCount;
}

std::int32_t KeyRegistry::GetUnusedControllerCount() const
{
    std::shared_lock lock(_mutex);
    return _unusedControllerCount;
}

std::string KeyRegistry::GetUnusedKeyDisplayLeft(std::int32_t a_index) const
{
    std::shared_lock lock(_mutex);
    if (a_index < 0 ||
        a_index >= static_cast<std::int32_t>(_unusedDisplayEntries.size()))
        return {};
    return _unusedDisplayEntries[a_index];
}

// ============================================================
//  GetKeyName
// ============================================================

std::string KeyRegistry::GetKeyName(std::uint32_t a_keyCode) const
{
    // k_names is defined in the anonymous namespace above;
    // see that section to add new key codes.
    auto it = k_names.find(a_keyCode);
    return it != k_names.end()
        ? std::string(it->second)
        : fmt::format("#{}", a_keyCode);
}
