#include "LuaLevelScriptActor.h"
#include "LuaState.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

ALuaLevelScriptActor::ALuaLevelScriptActor(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        NS_SLUA::LuaState::onInitEvent.AddUObject(this, &ALuaLevelScriptActor::onLuaStateInit);
    }
    bReplicates = false;
}

FString ALuaLevelScriptActor::GetLuaFilePath_Implementation() const
{
    return LuaFilePath;
}

void ALuaLevelScriptActor::onLuaStateInit(NS_SLUA::lua_State* L)
{
    if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        NS_SLUA::LuaState* state = NS_SLUA::LuaState::get(L);
        UWorld* world = GetWorld();
        if (state && world && state->getGameInstance() == world->GetGameInstance())
        {
            TryHook(state);
        }
    }
}

void ALuaLevelScriptActor::RegistLuaTick(float TickInterval)
{
    EnableLuaTick = true;
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->registLuaTick(this, TickInterval);
    }
}

void ALuaLevelScriptActor::UnRegistLuaTick()
{
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->unRegistLuaTick(this);
    }
}

void ALuaLevelScriptActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    if (EnableLuaTick)
    {
        UnRegistLuaTick();
    }
}
