#include "LuaOverriderInterface.h"
#include "LuaOverrider.h"
#include "lauxlib.h"

void ILuaOverriderInterface::PushLuaFunction(NS_SLUA::lua_State* L,
    const NS_SLUA::LuaVar& Self, const FString& Name)
{
    using namespace NS_SLUA;
    if (!Self.isValid() || !Self.isTable() || LuaState::get(Self.getState()) != LuaState::get(L))
        luaL_error(L, "Lua self table is unavailable or belongs to another state");
    const int top = lua_gettop(L);
    luaL_checkstack(L, 5, "Lua method lookup");
    Self.push(L);
    if (!lua_getmetatable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return;
    }
    lua_pushliteral(L, "__index");
    lua_rawget(L, -2);
    lua_remove(L, -2); // metatable
    lua_remove(L, -2); // original self
    if (lua_isnil(L, -1)) return;
    Self.push(L);
    LuaObject::push(L, Name);
    lua_pushboolean(L, true); // slua's Lua-first override resolution
    lua_call(L, 3, 1);
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1))
        luaL_error(L, "Resolved Lua method must be a function or nil");
}

void ILuaOverriderInterface::PushLuaFunctionCached(NS_SLUA::lua_State* L,
    const NS_SLUA::LuaVar& Self, const FString& Name)
{
    using namespace NS_SLUA;
#if WITH_EDITOR
    if (Cast<UBlueprintFunctionLibrary>(this)) { PushLuaFunction(L, Self, Name); return; }
#endif
    if (const LuaVar* Cached = FuncMap.Find(Name);
        Cached && Cached->isValid() && LuaState::get(Cached->getState()) == LuaState::get(L))
    {
        if (!Self.isValid() || !Self.isTable() || LuaState::get(Self.getState()) != LuaState::get(L))
            luaL_error(L, "Lua self table is unavailable or belongs to another state");
        Cached->push(L);
        return;
    }
    PushLuaFunction(L, Self, Name);
    if (lua_isfunction(L, -1)) FuncMap.Add(Name, LuaVar(L, -1));
}

int ILuaOverriderInterface::LookupLuaFunction(NS_SLUA::lua_State* L,
    const NS_SLUA::LuaVar& SelfTable, const FString& FunctionName, NS_SLUA::LuaVar& Function, FString& Error)
{
    using namespace NS_SLUA;
    Function = LuaVar();
    Error.Reset();
    if (!L) { L = SelfTable.getState(); }
    if (!L || !SelfTable.isValid() || !SelfTable.isTable() ||
        LuaState::get(L) != LuaState::get(SelfTable.getState()))
    {
        Error = TEXT("Lua self table is unavailable or belongs to another state");
        return LUA_ERRRUN;
    }
    AutoStack Stack(L);
    if (!lua_checkstack(L, 3))
    {
        Error = TEXT("Cannot reserve Lua method lookup stack");
        return LUA_ERRMEM;
    }
    struct FLookupContext
    {
        const LuaVar& Self;
        const FString& Name;
        LuaVar& Function;
    } Context{SelfTable, FunctionName, Function};
    lua_pushcfunction(L, [](lua_State* Call) -> int
    {
        const char* Message = lua_type(Call, 1) == LUA_TSTRING ? lua_tostring(Call, 1) : "non-string Lua error";
        luaL_traceback(Call, Call, Message, 1);
        return 1;
    });
    const int Handler = lua_gettop(L);
    lua_pushcfunction(L, [](lua_State* Call) -> int
    {
        auto& Context = *static_cast<FLookupContext*>(lua_touserdata(Call, 1));
        PushLuaFunction(Call, Context.Self, Context.Name);
        if (lua_isnil(Call, -1)) { return 0; }
        Context.Function = LuaVar(Call, -1);
        return 0;
    });
    lua_pushlightuserdata(L, &Context);
    const int Status = lua_pcall(L, 1, 0, Handler);
    if (Status != LUA_OK)
    {
        Error = lua_type(L, -1) == LUA_TSTRING ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : TEXT("Lua method lookup failed");
    }
    return Status;
}

int ILuaOverriderInterface::ResolveLuaFunction(NS_SLUA::lua_State* L,
    const NS_SLUA::LuaVar& SelfTable, const FString& FunctionName, NS_SLUA::LuaVar& Function, FString& Error)
{
    using namespace NS_SLUA;
    if (!L) { L = SelfTable.getState(); }
#if WITH_EDITOR
    if (Cast<UBlueprintFunctionLibrary>(this)) { return LookupLuaFunction(L, SelfTable, FunctionName, Function, Error); }
#endif
    if (const auto* Cached = FuncMap.Find(FunctionName);
        Cached && Cached->isValid() && L && SelfTable.isValid() && SelfTable.isTable() &&
        LuaState::get(SelfTable.getState()) == LuaState::get(L) && LuaState::get(Cached->getState()) == LuaState::get(L))
    {
        Function = *Cached;
        Error.Reset();
        return LUA_OK;
    }
    const int Status = LookupLuaFunction(L, SelfTable, FunctionName, Function, Error);
    if (Status == LUA_OK && Function.isFunction()) { FuncMap.Add(FunctionName, Function); }
    return Status;
}

bool ILuaOverriderInterface::IsLuaFunctionExist(const FString& FunctionName)
{
    const auto Self = GetSelfTable();
    if (!Self.isValid() || !Self.isTable()) { return false; }
    NS_SLUA::LuaVar Function;
    FString Error;
    if (ResolveLuaFunction(nullptr, Self, FunctionName, Function, Error) != LUA_OK)
    {
        NS_SLUA::Log::Error("%s.%s: %s", TCHAR_TO_UTF8(*Cast<UObject>(this)->GetName()),
            TCHAR_TO_UTF8(*FunctionName), TCHAR_TO_UTF8(*Error));
    }
    return Function.isFunction();
}

ULuaOverriderInterface::ULuaOverriderInterface(const class FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

NS_SLUA::LuaVar ILuaOverriderInterface::GetSelfTable(NS_SLUA::LuaState* L/* = nullptr*/) const
{
    NS_SLUA::LuaVar* luaSelfTable = ULuaOverrider::getObjectLuaTable(Cast<UObject>(this), L ? L->getLuaState() : nullptr);
    if (luaSelfTable)
    {
        return *luaSelfTable;
    }
    else
    {
        return NS_SLUA::LuaVar();
    }
}

void ILuaOverriderInterface::PostLuaHook()
{
    static FString PostConstructFunction = TEXT("_PostConstruct");
    CallLuaFunctionIfExist(PostConstructFunction);
}

void ILuaOverriderInterface::TryHook()
{
    UObject* obj = Cast<UObject>(this);
    if (!obj)
    {
        return;
    }

    NS_SLUA::LuaState::hookObject(nullptr, obj, true);
}

bool ILuaOverriderInterface::TryHook(NS_SLUA::LuaState* LuaState)
{
    UObject* obj = Cast<UObject>(this);
    if (!obj || !LuaState || LuaState->isClosingOrClosed())
    {
        return false;
    }

    return NS_SLUA::LuaState::hookObject(LuaState, obj, true);
}
