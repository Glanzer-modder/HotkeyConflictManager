#pragma once
#include "PCH.h"

// ============================================================
//  Serialization  -  SKSE cosave callbacks for KeyRegistry.
//
//  Registration in main.cpp:
//    auto* serial = SKSE::GetSerializationInterface();
//    serial->SetUniqueID('HCM_');
//    serial->SetSaveCallback(Serialization::OnSave);
//    serial->SetLoadCallback(Serialization::OnLoad);
//    serial->SetRevertCallback(Serialization::OnRevert);
// ============================================================

namespace Serialization
{
    // Record type written inside the cosave chunk.
    // 'KEYS' identifies my key registry data.
    inline constexpr std::uint32_t RecordType    = 'KEYS';
    inline constexpr std::uint32_t RecordVersion = 1;

    void OnSave  (SKSE::SerializationInterface* a_intfc);
    void OnLoad  (SKSE::SerializationInterface* a_intfc);
    void OnRevert(SKSE::SerializationInterface* a_intfc);
}
