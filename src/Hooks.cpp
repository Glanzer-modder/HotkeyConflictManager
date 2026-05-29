#include "PCH.h"
#include "Hooks.h"
#include "KeyRegistry.h"

// ============================================================
//  Hooks.cpp
//
//  Replaces the IFunction* stored in the Papyrus VM's type
//  info tables for RegisterForKey, UnregisterForKey, and
//  UnregisterForAllKeys on all three script types that SKSE
//  exposes them on:
//
//    "Form", "Alias", "ActiveMagicEffect"
//
//  See Hooks.h for the full design explanation.
// ============================================================

namespace
{
    // --------------------------------------------------------
    //  HookFunction
    //
    //  Wraps any one of the three target IFunction objects.
    //  All Get* virtuals delegate to the original so the VM
    //  still sees the correct metadata.  Only Call() is
    //  intercepted.
    // --------------------------------------------------------

    class HookFunction : public RE::BSScript::IFunction
    {
    public:
        enum class HookType { kRegister, kUnregister, kUnregisterAll };

        HookFunction(RE::BSTSmartPointer<RE::BSScript::IFunction> a_original,
                     HookType                                      a_type)
            : _original(std::move(a_original))
            , _type(a_type)
        {}

        // ---- IFunction pure-virtual overrides ---------------
        //  All forwarded to the original except Call().

        const RE::BSFixedString& GetName() const override
        { return _original->GetName(); }

        const RE::BSFixedString& GetObjectTypeName() const override
        { return _original->GetObjectTypeName(); }

        const RE::BSFixedString& GetStateName() const override
        { return _original->GetStateName(); }

        RE::BSScript::TypeInfo GetReturnType() const override
        { return _original->GetReturnType(); }

        std::uint32_t GetParamCount() const override
        { return _original->GetParamCount(); }

        void GetParam(std::uint32_t       a_idx,
                      RE::BSFixedString&  a_nameOut,
                      RE::BSScript::TypeInfo& a_typeOut) const override
        { _original->GetParam(a_idx, a_nameOut, a_typeOut); }

        std::uint32_t GetStackFrameSize() const override
        { return _original->GetStackFrameSize(); }

        bool GetIsNative() const override  { return _original->GetIsNative(); }
        bool GetIsStatic() const override  { return _original->GetIsStatic(); }
        bool GetIsEmpty()  const override  { return _original->GetIsEmpty();  }

        FunctionType GetFunctionType() const override
        { return _original->GetFunctionType(); }

        std::uint32_t GetUserFlags() const override
        { return _original->GetUserFlags(); }

        const RE::BSFixedString& GetDocString() const override
        { return _original->GetDocString(); }

        void InsertLocals(RE::BSScript::StackFrame* a_frame) override
        { _original->InsertLocals(a_frame); }

        const RE::BSFixedString& GetSourceFilename() const override
        { return _original->GetSourceFilename(); }

        bool TranslateIPToLineNumber(std::uint32_t a_indexPtr,
                                     std::uint32_t& a_lineNumberOut) const override
        { return _original->TranslateIPToLineNumber(a_indexPtr, a_lineNumberOut); }

        bool GetVarNameForStackIndex(std::uint32_t    a_idx,
                                     RE::BSFixedString& a_nameOut) const override
        { return _original->GetVarNameForStackIndex(a_idx, a_nameOut); }

        bool CanBeCalledFromTasklets() const override
        { return _original->CanBeCalledFromTasklets(); }

        void SetCallableFromTasklets(bool a_callable) override
        { _original->SetCallableFromTasklets(a_callable); }

        // ---- Call() : the intercepted virtual (index 0x0F) --

        CallResult Call(
            const RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack,
            RE::BSScript::ErrorLogger*                       a_logger,
            RE::BSScript::Internal::VirtualMachine*          a_vm,
            bool                                             a_arg4) override
        {
            RE::FormID    callerFormID = 0;
            std::uint32_t keyCode      = 0;
            bool          extracted    = false;

            if (a_stack) {
                RE::BSScript::StackFrame* frame = a_stack->top;
                if (frame) {
                    const std::uint32_t pageHint =
                        a_stack->GetPageForFrame(frame);

                    // frame->self is the Variable holding the calling
                    // Form (the Papyrus "self" object).
                    auto obj = frame->self.GetObject();  // BSTSmartPointer<Object>
                    if (obj) {
                        // For TESForm-derived objects the VMHandle
                        // encodes the FormID in the lower 32 bits.
                        RE::VMHandle handle = obj->GetHandle();
                        callerFormID =
                            static_cast<RE::FormID>(handle & 0xFFFFFFFF);
                    }

                    // First argument (index 0) is the keyCode.
                    // UnregisterForAllKeys takes no arguments.
                    if (_type != HookType::kUnregisterAll) {
                        RE::BSScript::Variable& keyVar =
                            frame->GetStackFrameVariable(0, pageHint);
                        keyCode = static_cast<std::uint32_t>(
                            keyVar.GetSInt());
                    }

                    extracted = true;
                }
            }

            // Call through so SKSE's own registration still happens
            // and OnKeyDown events continue to fire correctly.
            CallResult result =
                _original->Call(a_stack, a_logger, a_vm, a_arg4);

            // Update my registry after the original has run.
            if (extracted) {
                auto& reg = KeyRegistry::GetSingleton();
                switch (_type) {
                case HookType::kRegister:
                    reg.Register(keyCode, callerFormID);
                    break;
                case HookType::kUnregister:
                    reg.Unregister(keyCode, callerFormID);
                    break;
                case HookType::kUnregisterAll:
                    reg.UnregisterAll(callerFormID);
                    break;
                }
            }

            return result;
        }

    private:
        RE::BSTSmartPointer<RE::BSScript::IFunction> _original;
        HookType                                      _type;
    };

    // --------------------------------------------------------
    //  HookTypeInfo
    //
    //  Iterates the MemberFuncInfo array of a single
    //  ObjectTypeInfo and replaces the three target functions.
    //  Returns the number of hooks installed.
    // --------------------------------------------------------

    int HookTypeInfo(RE::BSScript::ObjectTypeInfo* a_typeInfo)
    {
        if (!a_typeInfo || !a_typeInfo->IsLinked()) {
            logger::warn("[HCM] Type '{}' is not linked – skipping.",
                         a_typeInfo ? a_typeInfo->GetName() : "(null)");
            return 0;
        }

        const std::uint32_t count = a_typeInfo->GetNumMemberFuncs();
        auto*               funcs = a_typeInfo->GetMemberFuncIter();
        int                 hooked = 0;

        for (std::uint32_t i = 0; i < count; ++i) {
            auto& entry = funcs[i];
            if (!entry.func) continue;

            const RE::BSFixedString& name = entry.func->GetName();

            HookFunction::HookType hookType;

            if      (name == "RegisterForKey")      hookType = HookFunction::HookType::kRegister;
            else if (name == "UnregisterForKey")    hookType = HookFunction::HookType::kUnregister;
            else if (name == "UnregisterForAllKeys")hookType = HookFunction::HookType::kUnregisterAll;
            else continue;

            // Replace the smart pointer.  HookFunction's constructor
            // takes the original by value, incrementing its refcount,
            // so the original stays alive as long as HookFunction does.
            entry.func = RE::BSTSmartPointer<RE::BSScript::IFunction>(
                new HookFunction(entry.func, hookType));

            logger::info("[HCM] Hooked {}.{}",
                         a_typeInfo->GetName(), name.c_str());
            ++hooked;
        }

        return hooked;
    }

    // --------------------------------------------------------
    //  HookScriptType
    //
    //  Looks up a named script type in the VM and hooks it.
    // --------------------------------------------------------

    void HookScriptType(RE::BSScript::Internal::VirtualMachine* a_vm,
                        const RE::BSFixedString&                 a_typeName)
    {
        RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;

        // GetScriptObjectType fills typeInfo if the type exists.
        if (!a_vm->GetScriptObjectType(a_typeName, typeInfo) || !typeInfo) {
            logger::warn("[HCM] Script type '{}' not found in VM.",
                         a_typeName.c_str());
            return;
        }

        const int n = HookTypeInfo(typeInfo.get());
        if (n == 0)
            logger::warn("[HCM] No target functions found on type '{}'.",
                         a_typeName.c_str());
    }

}   // anonymous namespace

// ============================================================
//  Hooks::Install
// ============================================================

void Hooks::Install()
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) {
        logger::error("[HCM] Papyrus VM not available – hooks not installed.");
        return;
    }

    // SKSE adds RegisterForKey to these three Papyrus script types.
    // We hook all three so we catch registrations from quests,
    // references, aliases, and magic effects.
    // "Form" covers quests, object references, and most mod scripts.
    // "Alias" is the base type SKSE uses for both ReferenceAlias and
    // LocationAlias - they do not have RegisterForKey independently.
    // "ActiveMagicEffect" covers magic effect scripts.
    static const RE::BSFixedString targets[] = {
        "Form",
        "Alias",
        "ActiveMagicEffect"
    };

    for (const auto& typeName : targets)
        HookScriptType(vm, typeName);

    logger::info("[HCM] Hook installation complete.");
}
