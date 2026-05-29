#include "PCH.h"
#include "Serialization.h"
#include "KeyRegistry.h"

namespace Serialization
{
    void OnSave(SKSE::SerializationInterface* a_intfc)
    {
        if (!a_intfc->OpenRecord(RecordType, RecordVersion)) {
            logger::error("[HCM] Failed to open serialization record for save.");
            return;
        }
        KeyRegistry::GetSingleton().SaveToStream(a_intfc);
    }

    void OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        std::uint32_t type    = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;  // populated by API; unused - ReadRecordData handles bounds internally

        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type == RecordType) {
                if (version != RecordVersion) {
                    logger::warn(
                        "[HCM] Cosave record version mismatch "
                        "(expected {}, got {}) - skipping.",
                        RecordVersion, version);
                    continue;
                }
                KeyRegistry::GetSingleton().LoadFromStream(a_intfc);
            } else {
                logger::warn("[HCM] Unknown cosave record type {:08X} - skipping.",
                             type);
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        // Called when a save load is about to begin.
        // Clear the registry so stale data from a previous
        // session doesn't linger before OnLoad populates it.
        logger::info("[HCM] Cosave revert - clearing registry.");
        KeyRegistry::GetSingleton().Clear();
    }
}
