#include "PCH.h"
#include "ControlMapWatcher.h"
#include "Settings.h"
#include "Hooks.h"
#include "KeyRegistry.h"
#include "PapyrusFunctions.h"
#include "Serialization.h"

namespace
{
    void InitializeLogging()
    {
        PWSTR buf{ nullptr };
        ::SHGetKnownFolderPath(::FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &buf);
        std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)>
            documentsPath{ buf, ::CoTaskMemFree };
        if (!documentsPath)
            SKSE::stl::report_and_fail("Unable to find documents folder.");

        std::filesystem::path path{ documentsPath.get() };
        path /= "My Games"sv;
        if SKYRIM_REL_VR_CONSTEXPR (REL::Module::IsVR()) {
            path /= "Skyrim VR"sv;
        } else if (std::filesystem::exists("steam_api64.dll"sv)) {
            path /= "Skyrim Special Edition"sv;
        } else if (std::filesystem::exists("Galaxy64.dll"sv)) {
            path /= "Skyrim Special Edition GOG"sv;
        } else if (std::filesystem::exists("eossdk-win64-shipping.dll"sv)) {
            path /= "Skyrim Special Edition EPIC"sv;
        } else {
            path /= "Skyrim Special Edition"sv;
        }

        path /= "SKSE"sv;
        path /= std::format("{}.log", Plugin::NAME);

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path.string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;

        switch (a_msg->type)
        {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("[HCM] kDataLoaded - installing hooks.");
            Hooks::Install();
            ControlMapWatcher::Register();
            KeyRegistry::GetSingleton().SnapshotControlMap();
            // Flags use C++ defaults (modfile=on, gameaction=on, debug=off) which match
            // the ESP defaults.  kPostLoadGame will override these with the
            // correct saved values once a save is loaded.
            logger::info("[HCM] kDataLoaded - flags at defaults; will sync from save at kPostLoadGame.");
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        {
            const bool modOn = []() { auto* g = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_warnings");       return g ? g->value != 0.0f : true;  }();
            const bool gameOn = []() { auto* g = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_game_warnings"); return g ? g->value != 0.0f : true;  }();
            const bool dbgOn = []() { auto* g = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_debug");          return g ? g->value != 0.0f : false; }();
            KeyRegistry::GetSingleton().SetPopupWarningsEnabled(modOn);
            KeyRegistry::GetSingleton().SetGameConflictWarningsEnabled(gameOn);
            KeyRegistry::GetSingleton().SetDebugModeEnabled(dbgOn);
            logger::info("[HCM] kPostLoadGame - synced from globals: "
                "mod conflicts={} (glz_HCM_warnings), "
                "game conflicts={} (glz_HCM_game_warnings), "
                "debug={} (glz_HCM_debug).",
                modOn ? "on" : "off", gameOn ? "on" : "off", dbgOn ? "on" : "off");
            KeyRegistry::GetSingleton().SnapshotControlMap();
            logger::info("[HCM] kPostLoadGame - ControlMap re-snapshotted.");
        }
        break;

        case SKSE::MessagingInterface::kNewGame:
            logger::info("[HCM] New game - clearing registry.");
            KeyRegistry::GetSingleton().Clear();
            KeyRegistry::GetSingleton().MarkLoadTime();
            KeyRegistry::GetSingleton().SnapshotControlMap();
            // Reset flags to the values from the ESP (no save to sync from)
            if (auto* glob = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_warnings")) {
                KeyRegistry::GetSingleton().SetPopupWarningsEnabled(glob->value != 0.0f);
            }
            else {
                KeyRegistry::GetSingleton().SetPopupWarningsEnabled(true);
            }
            if (auto* glob = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_game_warnings")) {
                KeyRegistry::GetSingleton().SetGameConflictWarningsEnabled(glob->value != 0.0f);
            }
            else {
                KeyRegistry::GetSingleton().SetGameConflictWarningsEnabled(true);
            }
            if (auto* glob = RE::TESForm::LookupByEditorID<RE::TESGlobal>("glz_HCM_debug")) {
                KeyRegistry::GetSingleton().SetDebugModeEnabled(glob->value != 0.0f);
            }
            else {
                KeyRegistry::GetSingleton().SetDebugModeEnabled(false);
            }
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            logger::info("[HCM] Pre-load game - resetting internal key hook registry.");
            KeyRegistry::GetSingleton().Clear();
            KeyRegistry::GetSingleton().MarkLoadTime();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// SKSE Plugin Metadata
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []()
{
    SKSE::PluginVersionData v{};
    v.PluginName(Plugin::NAME);
    v.PluginVersion(Plugin::VERSION);
    v.AuthorName(Plugin::AUTHOR);
    v.UsesAddressLibrary(true);
    v.HasNoStructUse(true);
    return v;
}();

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(
    const SKSE::QueryInterface* a_skse,
    SKSE::PluginInfo*           a_info)
{
    if (!a_skse || !a_info) return false;
    if (a_skse->IsEditor()) {
        logger::critical("Loaded in editor, marking as incompatible.");
        return false;
    }
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name        = Plugin::NAME.data();
    a_info->version     = Plugin::VERSION[0];
    return true;
}

// ---------------------------------------------------------------------------
// Plugin Load
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) bool SKSEAPI
SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    InitializeLogging();
    logger::info("Loading {}...", Plugin::NAME);

    SKSE::Init(a_skse);

    // Load INI settings before anything else
    Settings::GetSingleton().Load();

    // Messaging
    auto* msg = SKSE::GetMessagingInterface();
    if (!msg || !msg->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register SKSE message listener.");
        return false;
    }

    // Papyrus
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(PapyrusFunctions::RegisterFunctions)) {
        logger::critical("Failed to register Papyrus functions.");
        return false;
    }

    // Cosave serialization
    auto* serial = SKSE::GetSerializationInterface();
    if (!serial) {
        logger::critical("Failed to acquire SKSE serialization interface.");
        return false;
    }
    serial->SetUniqueID('HCM_');
    serial->SetSaveCallback(Serialization::OnSave);
    serial->SetLoadCallback(Serialization::OnLoad);
    serial->SetRevertCallback(Serialization::OnRevert);
    logger::info("[HCM] Cosave serialization registered.");

    logger::info("{} loaded successfully.", Plugin::NAME);
    return true;
}
