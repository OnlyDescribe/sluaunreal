#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "LuaObject.h"
#include "LuaNetType.h"
#include "LuaState.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"

namespace
{
    bool RunLifetimeScript(NS_SLUA::LuaState& State, const char* Script)
    {
        auto Result = State.doString(Script);
        return Result.isBool() && Result.castTo<bool>();
    }

    class FCountedStringProperty : public FStrProperty
    {
    public:
        FCountedStringProperty(FFieldVariant Owner, FName Name) : FStrProperty(Owner, Name)
        {
            FArchive Archive;
            LinkWithoutChangingOffset(Archive);
        }

        mutable int32 Destroyed = 0;

        virtual void DestroyValueInternal(void* Value) const override
        {
            ++Destroyed;
            FStrProperty::DestroyValueInternal(Value);
        }
    };


}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBorrowedContainerDestructionTest,
    "Slua.Runtime.BorrowedContainerDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBorrowedContainerDestructionTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("BorrowedContainerDestruction", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init()))
        return false;
    lua_State* L = State.getLuaState();
    lua_pushinteger(L, static_cast<int>(EPropertyClass::Str));
    lua_setglobal(L, "StringType");
    TUniquePtr<FCountedStringProperty> ElementOwner(new FCountedStringProperty(GetTransientPackage(), TEXT("Element")));
    auto& Element = *ElementOwner;
    TUniquePtr<FMapProperty> MapProperty(new FMapProperty(GetTransientPackage(), TEXT("Map")));
    auto& Key = *new FCountedStringProperty(MapProperty.Get(), TEXT("Key"));
    auto& Value = *new FCountedStringProperty(MapProperty.Get(), TEXT("Value"));
    MapProperty->KeyProp = &Key;
    MapProperty->ValueProp = &Value;
    FArchive Archive;
    MapProperty->LinkWithoutChangingOffset(Archive);

    TArray<FString> Array{TEXT("first"), TEXT("second")};
    TMap<FString, FString> Map{{TEXT("first"), TEXT("one")}, {TEXT("second"), TEXT("two")}};
    TSet<FString> Set{TEXT("first"), TEXT("second")};
    LuaObject::addLink(L, &Array);
    LuaArray::push(L, new LuaArray(&Element, reinterpret_cast<FScriptArray*>(&Array), true, false));
    LuaObject::linkProp(L, &Array, lua_touserdata(L, -1));
    lua_setglobal(L, "array");
    LuaObject::addLink(L, &Map);
    LuaMap::push(L, new LuaMap(MapProperty.Get(), reinterpret_cast<FScriptMap*>(&Map), true, nullptr, InvalidReplicatedIndex));
    LuaObject::linkProp(L, &Map, lua_touserdata(L, -1));
    lua_setglobal(L, "map");
    LuaMap::push(L, Map);
    lua_setglobal(L, "copy");
    LuaObject::addLink(L, &Set);
    LuaSet::push(L, new LuaSet(&Element, reinterpret_cast<FScriptSet*>(&Set), true, false));
    LuaObject::linkProp(L, &Set, lua_touserdata(L, -1));
    lua_setglobal(L, "set");

    TestTrue(TEXT("borrowed containers mutate their real storage"), RunLifetimeScript(State, R"lua(
        map:Remove('first')
        map:Clear()
        assert(copy:Get('first') == 'one' and copy:Get('second') == 'two')
        copy:Clear()
        array:Remove(0)
        array:Clear()
        set:Remove('first')
        set:Clear()
        local owned = slua.Map(StringType, StringType)
        owned:Add('key', 'value')
        assert(owned:Get('key') == 'value')
        owned:Clear()
        return true
    )lua"));
    TestEqual(TEXT("array and set destroy all four removed values plus temporary set key"), Element.Destroyed, 5);
    TestEqual(TEXT("map destroys both removed keys plus temporary lookup key"), Key.Destroyed, 3);
    TestEqual(TEXT("map destroys both removed values"), Value.Destroyed, 2);
    TestTrue(TEXT("all native containers are empty"), Array.IsEmpty() && Map.IsEmpty() && Set.IsEmpty());
    State.close();
    TestEqual(TEXT("borrowed wrappers do not destroy elements a second time"), Element.Destroyed, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaStructReferenceTest,
    "Slua.Runtime.StructNativeReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaStructReferenceTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("StructNativeReferences", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init()))
        return false;
    lua_State* L = State.getLuaState();

    TStrongObjectPtr<UScriptStruct> InnerType(NewObject<UScriptStruct>());
    auto* ObjectProperty = new FObjectProperty(InnerType.Get(), TEXT("Object"));
    ObjectProperty->PropertyClass = UActorComponent::StaticClass();
    InnerType->AddCppProperty(ObjectProperty);
    InnerType->StaticLink(true);
    TWeakObjectPtr<UScriptStruct> WeakType(InnerType.Get());
    UActorComponent* Object = NewObject<USceneComponent>();
    TWeakObjectPtr<UActorComponent> WeakObject(Object);

    // FInstancedStruct stores its type and heap allocation outside reflected
    // properties; retaining the inner UObject therefore requires native ARO.
    FInstancedStruct Instance;
    Instance.InitializeAs(InnerType.Get());
    ObjectProperty->SetObjectPropertyValue_InContainer(Instance.GetMutableMemory(), Object);
    TUniquePtr<FProperty> Property(PropertyProto::createProperty(
        PropertyProto(EPropertyClass::Struct, FInstancedStruct::StaticStruct())));
    LuaObject::push(L, Property.Get(), reinterpret_cast<uint8*>(&Instance), nullptr);
    lua_setglobal(L, "instance");
    Instance.Reset();
    InnerType.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Lua-owned struct reports native ARO type"), WeakType.IsValid());
    TestTrue(TEXT("Lua-owned struct reports nested native ARO values"), WeakObject.IsValid());
    State.close();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("value is released with Lua owner"), WeakObject.IsValid());
    TestFalse(TEXT("dynamic native ARO type is released with Lua owner"), WeakType.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaStructSkippedSerializationReferenceTest,
    "Slua.Runtime.StructSkippedSerializationReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaStructSkippedSerializationReferenceTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("StructSkippedSerializationReferences", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init()))
        return false;
    lua_State* L = State.getLuaState();

    TStrongObjectPtr<UScriptStruct> Type(NewObject<UScriptStruct>());
    auto* OrdinaryProperty = new FObjectProperty(Type.Get(), TEXT("OrdinaryObject"));
    OrdinaryProperty->PropertyClass = UActorComponent::StaticClass();
    Type->AddCppProperty(OrdinaryProperty);
    auto* SkippedProperty = new FObjectProperty(Type.Get(), TEXT("SkippedObject"));
    SkippedProperty->PropertyClass = UActorComponent::StaticClass();
    SkippedProperty->SetPropertyFlags(CPF_SkipSerialization);
    Type->AddCppProperty(SkippedProperty);
    Type->StaticLink(true);

    TWeakObjectPtr<UActorComponent> OrdinaryObject(NewObject<USceneComponent>());
    TWeakObjectPtr<UActorComponent> SkippedObject(NewObject<USceneComponent>());
    {
        FStructOnScope Instance(Type.Get());
        OrdinaryProperty->SetObjectPropertyValue_InContainer(Instance.GetStructMemory(), OrdinaryObject.Get());
        SkippedProperty->SetObjectPropertyValue_InContainer(Instance.GetStructMemory(), SkippedObject.Get());
        TUniquePtr<FProperty> Property(PropertyProto::createProperty(PropertyProto(EPropertyClass::Struct, Type.Get())));
        LuaObject::push(L, Property.Get(), Instance.GetStructMemory(), nullptr);
        lua_setglobal(L, "instance");
    }
    Type.Reset();

    // Only the Lua-owned copy retains the objects during collection.
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("ordinary struct field retains its strong reference"), OrdinaryObject.IsValid());
    TestTrue(TEXT("SkipSerialization struct field retains its strong reference"), SkippedObject.IsValid());
    State.close();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("ordinary reference is released with Lua owner"), OrdinaryObject.IsValid());
    TestFalse(TEXT("SkipSerialization reference is released with Lua owner"), SkippedObject.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaEmptyContainerMetadataTest,
    "Slua.Runtime.EmptyContainerMetadata",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaEmptyContainerMetadataTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("EmptyContainerMetadata", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init()))
        return false;
    lua_State* L = State.getLuaState();
    TStrongObjectPtr<UScriptStruct> Type(NewObject<UScriptStruct>());
    Type->AddCppProperty(new FStrProperty(Type.Get(), TEXT("Text")));
    Type->StaticLink(true);
    TWeakObjectPtr<UScriptStruct> WeakType(Type.Get());
    FScriptArray EmptyArray;
    FProperty* Inner = PropertyProto::createProperty(PropertyProto(EPropertyClass::Struct, Type.Get()));
    LuaArray::push(L, Inner, &EmptyArray, true);
    lua_setglobal(L, "emptyArray");
    Type.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("empty owned container retains its artificial property metadata"), WeakType.IsValid());
    TestTrue(TEXT("empty container remains usable after GC"), RunLifetimeScript(State, "return emptyArray:Num() == 0"));
    State.close();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("metadata is released with container"), WeakType.IsValid());
    return true;
}


#endif
