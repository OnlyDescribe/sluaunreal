#pragma once
#include "LuaVar.h"
#include "LuaCppBindingPost.h"
#include "UObject/Interface.h"
#include "LuaObject.h"
#include "LuaState.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaOverriderInterface.generated.h"

UINTERFACE()
class SLUA_UNREAL_API ULuaOverriderInterface : public UInterface
{
    GENERATED_UINTERFACE_BODY()
};

class SLUA_UNREAL_API ILuaOverriderInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent)
        FString GetLuaFilePath() const;

    NS_SLUA::LuaVar GetSelfTable(NS_SLUA::LuaState* L = nullptr) const;

    virtual void PostLuaHook();

    void TryHook();
    bool TryHook(NS_SLUA::LuaState* LuaState);

    // LUA_OK with a nil function means genuinely absent; other statuses carry a lookup error.
    int ResolveLuaFunction(NS_SLUA::lua_State* L, const NS_SLUA::LuaVar& SelfTable,
        const FString& FunctionName, NS_SLUA::LuaVar& Function, FString& Error);

    static int LookupLuaFunction(NS_SLUA::lua_State* L, const NS_SLUA::LuaVar& SelfTable,
        const FString& FunctionName, NS_SLUA::LuaVar& Function, FString& Error);

    // Lua operations; the caller supplies the protection boundary.
    static void PushLuaFunction(NS_SLUA::lua_State* L, const NS_SLUA::LuaVar& Self, const FString& Name);
    void PushLuaFunctionCached(NS_SLUA::lua_State* L, const NS_SLUA::LuaVar& Self, const FString& Name);

    bool IsLuaFunctionExist(const FString& FunctionName);

    template<class RET, class ...ARGS>
    RET CallLuaFunction(const FString& FunctionName, ARGS&& ...Args) {
        return Call<RET>(false, nullptr, FunctionName, std::forward<ARGS>(Args)...);
    }

    template<class ...ARGS>
    void CallLuaFunction(const FString& FunctionName, ARGS&& ...Args) {
        Call<void>(false, nullptr, FunctionName, std::forward<ARGS>(Args)...);
    }

    template<class RET, class ...ARGS>
    RET CallLuaFunctionIfExist(const FString& FunctionName, ARGS&& ...Args) {
        return Call<RET>(true, nullptr, FunctionName, std::forward<ARGS>(Args)...);
    }
    
    template<class ...ARGS>
    void CallLuaFunctionIfExist(const FString& FunctionName, ARGS&& ...Args) {
        Call<void>(true, nullptr, FunctionName, std::forward<ARGS>(Args)...);
    }

    template<class RET, class ...ARGS>
    RET CallLuaFunctionWithContext(const UWorld* World, const FString& FunctionName, ARGS&& ...Args) {
        return Call<RET>(false, World, FunctionName, std::forward<ARGS>(Args)...);
    }

    template<class ...ARGS>
    void CallLuaFunctionWithContext(const UWorld* World, const FString& FunctionName, ARGS&& ...Args) {
        Call<void>(false, World, FunctionName, std::forward<ARGS>(Args)...);
    }

    template<class RET, class ...ARGS>
    RET CallLuaFunctionIfExistWithContext(const UWorld* World, const FString& FunctionName, ARGS&& ...Args) {
        return Call<RET>(true, World, FunctionName, std::forward<ARGS>(Args)...);
    }
    
    template<class ...ARGS>
    void CallLuaFunctionIfExistWithContext(const UWorld* World, const FString& FunctionName, ARGS&& ...Args) {
        Call<void>(true, World, FunctionName, std::forward<ARGS>(Args)...);
    }

private:
    template<class RET, class ...ARGS>
    RET Call(bool checkExist, const UWorld* World, const FString& FunctionName, ARGS&& ...Args) {
        auto* LS = World ? NS_SLUA::LuaState::get(World->GetGameInstance()) : nullptr;
        const auto Self = GetSelfTable(LS);
        if (!Self.isValid() || !Self.isTable()) {
            if (!checkExist) {
                NS_SLUA::Log::Error("Lua module not assigned to UObject[%s]", TCHAR_TO_UTF8(*Cast<UObject>(this)->GetName()));
            }
            return RET();
        }
        NS_SLUA::LuaVar Function;
        FString Error;
        if (ResolveLuaFunction(LS ? LS->getLuaState() : nullptr, Self, FunctionName, Function, Error) != LUA_OK) {
            NS_SLUA::Log::Error("%s.%s: %s", TCHAR_TO_UTF8(*Cast<UObject>(this)->GetName()),
                TCHAR_TO_UTF8(*FunctionName), TCHAR_TO_UTF8(*Error));
            return RET();
        }
        if (Function.isNil()) {
            if (!checkExist) {
                NS_SLUA::Log::Error("%s: required Lua function %s is missing",
                    TCHAR_TO_UTF8(*Cast<UObject>(this)->GetName()), TCHAR_TO_UTF8(*FunctionName));
            }
            return RET();
        }
        auto Result = Function.call(Self, std::forward<ARGS>(Args)...);
        return Result.isValid() ? Result.template castTo<RET>() : RET();
    }

public:
    TMap<FString, NS_SLUA::LuaVar> FuncMap;
};
