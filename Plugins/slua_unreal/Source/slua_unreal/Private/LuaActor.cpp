#include "LuaActor.h"
#include "LuaState.h"
#include "Net/UnrealNetwork.h"
#include "Runtime/Launch/Resources/Version.h"

ALuaActor::ALuaActor(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void ALuaActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    if (EnableLuaTick)
    {
        UnRegistLuaTick();
    }
}

FString ALuaActor::GetLuaFilePath_Implementation() const
{
    return LuaFilePath;
}

void ALuaActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 7))
    // Level-placed actors are deserialized before GameInstance::Init() creates LuaOverrider,
    // so NotifyUObjectCreated auto-hook misses them. TryHook() here is safe: LuaState is
    // guaranteed ready by PostInitializeComponents, and double-hook is guarded by isUFunctionHooked.
    if (UWorld* world = GetWorld())
    {
        TryHook(NS_SLUA::LuaState::get(world->GetGameInstance()));
    }
#endif
    ILuaOverriderInterface::PostLuaHook();
}

void ALuaActor::RegistLuaTick(float TickInterval)
{
    EnableLuaTick = true;
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->registLuaTick(this, TickInterval);
    }
}

void ALuaActor::UnRegistLuaTick()
{
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->unRegistLuaTick(this);
    }
}

void ALuaActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    {
        DOREPLIFETIME_CONDITION(ALuaActor, LuaNetSerialization, COND_None);
    }
}
