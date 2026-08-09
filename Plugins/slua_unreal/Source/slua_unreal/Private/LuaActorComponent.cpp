#include "LuaActorComponent.h"
#include "LuaState.h"
#include "Net/UnrealNetwork.h"

ULuaActorComponent::ULuaActorComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , EnableLuaTick(false)
{
    bWantsInitializeComponent = true;
}

void ULuaActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    if (EnableLuaTick)
    {
        UnRegistLuaTick();
    }
}

FString ULuaActorComponent::GetLuaFilePath_Implementation() const
{
    return LuaFilePath;
}

void ULuaActorComponent::RegistLuaTick(float TickInterval)
{
    EnableLuaTick = true;
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->registLuaTick(this, TickInterval);
    }
}

void ULuaActorComponent::UnRegistLuaTick()
{
    UWorld* world = GetWorld();
    if (auto* state = NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr))
    {
        state->unRegistLuaTick(this);
    }
}

void ULuaActorComponent::InitializeComponent()
{
    Super::InitializeComponent();

    UWorld* world = GetWorld();
    TryHook(NS_SLUA::LuaState::get(world ? world->GetGameInstance() : nullptr));
}

void ULuaActorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    {
        DOREPLIFETIME_CONDITION(ULuaActorComponent, LuaNetSerialization, COND_None);
    }
}
