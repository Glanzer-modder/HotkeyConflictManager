#include "PCH.h"
#include "Settings.h"

#include <fstream>
#include <functional>

Settings& Settings::GetSingleton() noexcept
{
    static Settings instance;
    return instance;
}

// ============================================================
//  Load
// ============================================================

void Settings::Load()
{
    const auto path = std::format("Data/SKSE/Plugins/{}.ini", Plugin::NAME);

    ParseFile(path, [this](const std::string& section,
                           const std::string& key,
                           const std::string& value)
    {
        if (section == "General")
        {
            if (key == "iStartupWindow")
            {
                std::int32_t val = ParseInt(value, 60);
                if (val < 0) {
                    logger::warn("[HCM] iStartupWindow must be >= 0. Using default 60.");
                    val = 60;
                } else if (val > 300) {
                    logger::warn("[HCM] iStartupWindow must be <= 300. Using 300.");
                    val = 300;
                }
                _startupWindow = std::chrono::seconds(val);
            }
        }
    });

    logger::info("[HCM] Settings loaded. iStartupWindow={}s.",
                 _startupWindow.count());
}

// ============================================================
//  ParseFile  -  Minimal INI parser
//  Supports:
//    [Section] headers
//    Key = Value pairs (leading/trailing whitespace trimmed)
//    ; and # comment lines
//    Blank lines
// ============================================================

void Settings::ParseFile(
    const std::string& a_path,
    std::function<void(const std::string&,
                       const std::string&,
                       const std::string&)> a_handler)
{
    std::ifstream file(a_path);
    if (!file.is_open()) {
        logger::warn("[HCM] Settings file not found: {} - using defaults.", a_path);
        return;
    }

    auto trim = [](std::string& s) {
        const auto first = s.find_first_not_of(" \t");
        if (first == std::string::npos) { s.clear(); return; }
        s.erase(0, first);
        const auto last = s.find_last_not_of(" \t\r");
        if (last != std::string::npos) s.erase(last + 1);
        };

    std::string  section;
    std::string  line;
    std::uint32_t lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;
        trim(line);

        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[') {
            const auto end = line.find(']');
            if (end != std::string::npos)
                section = line.substr(1, end - 1);
            trim(section);
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            logger::warn("[HCM] Settings line {}: no '=' found, skipping.", lineNum);
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);

        if (!section.empty() && !key.empty())
            a_handler(section, key, val);
    }
}

// ============================================================
//  ParseInt
// ============================================================

std::int32_t Settings::ParseInt(const std::string& a_value, std::int32_t a_default)
{
    try {
        return std::stoi(a_value);
    } catch (...) {
        logger::warn("[HCM] Could not parse integer value '{}'. Using default {}.",
                     a_value, a_default);
        return a_default;
    }
}
