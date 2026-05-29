#pragma once
#include "PCH.h"

// ============================================================
//  Hooks.h  -  IFunction replacement hooks for:
//                  Form.RegisterForKey
//                  Form.UnregisterForKey
//                  Form.UnregisterForAllKeys
//              (and the same three on Alias and ActiveMagicEffect)
//
//  HOW IT WORKS
//  ------------
//  SKSE registers native Papyrus functions by calling
//  IVirtualMachine::RegisterFunction(), which stores an
//  IFunction* inside the ObjectTypeInfo::MemberFuncInfo array
//  for the relevant script type.
//
//  On kDataLoaded (after SKSE has registered everything and
//  the Papyrus type system is fully linked), I do the following:
//
//    1. Ask the VM for the ObjectTypeInfo for each script type.
//    2. Walk its MemberFuncInfo array looking for our targets.
//    3. Save the original BSTSmartPointer<IFunction>.
//    4. Replace it with a new HookFunction that:
//         - delegates all Get* queries to the original, so the
//           VM still sees the function as having the correct
//           name, parameters, return type, etc.
//         - in Call(), calls through to the original (so SKSE's
//           own registration still happens), then extracts the
//           Form and keyCode from the Papyrus stack and updates
//           KeyRegistry.
//
//  Because I replace the smart pointer in the type's own
//  function table, every future Papyrus call to RegisterForKey
//  goes through my hook transparently.  No SKSE DLL addresses
//  are required; navigation uses CommonLibSSE's VM API, which
//  resolves addresses via Address Library internally.
// ============================================================

namespace Hooks
{
    // Install hooks on all three Papyrus script types that
    // expose RegisterForKey.  Call from the kDataLoaded SKSE
    // message handler.
    void Install();
}
