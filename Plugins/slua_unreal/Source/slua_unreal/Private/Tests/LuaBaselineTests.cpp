#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "LuaFunctionAccelerator.h"
#include "LuaOverriderInterface.h"
#include "LuaState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBaselineArrayTest,
    "Slua.Baseline.ArraySemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBaselineArrayTest::RunTest(const FString& Parameters)
{
    NS_SLUA::LuaState State("BaselineArray", nullptr, false);
    if (!TestTrue(TEXT("reference VM initializes"), State.init())) return false;
    auto Result = State.doString(R"lua(
        local Math = import("KismetMathLibrary")
        local function maximum(values)
            return Math.MaxOfIntArray(values, nil, nil)
        end
        local values = {}
        values[3] = 30; values[1] = 10; values[2] = 20
        local index, value = maximum(values)
        assert(index == 2 and value == 30, "numeric order changed")
        index, value = maximum({10, 20, label = 999})
        assert(index == 1 and value == 20, "The baseline sequence reader must ignore record fields")
        assert(pcall(maximum, {[1] = 10, [3] = 30}), "The baseline does not reject sparse shape")
        assert(pcall(maximum, {}), "empty input must remain accepted")
        return true
    )lua");
    TestTrue(TEXT("Integer-order conversion accepts sparse tables and ignores record keys"),
        Result.isBool() && Result.castTo<bool>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBaselineStructTest,
    "Slua.Baseline.StructAndOutParameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBaselineStructTest::RunTest(const FString& Parameters)
{
    NS_SLUA::LuaState State("BaselineStructs", nullptr, false);
    if (!TestTrue(TEXT("reference VM initializes"), State.init())) return false;
    auto Result = State.doString(R"lua(
        local Math = import("KismetMathLibrary")
        local value = Math.MakeTransform({X=3, Y=4, Z=5},
            {Pitch=0, Yaw=0, Roll=0}, {X=1, Y=2, Z=3})
        local location, rotation, scale = Math.BreakTransform(value, nil, nil, nil)
        assert(location.X == 3 and location.Y == 4 and location.Z == 5)
        assert(rotation.Pitch == 0 and rotation.Yaw == 0 and rotation.Roll == 0)
        assert(scale.X == 1 and scale.Y == 2 and scale.Z == 3)
        local vector = Math.MakeVector(3, 0, 0)
        assert(rawequal(Math.MakeVector(6, 0, 0, vector), vector))
        assert(rawequal(Math.Vector_Normalize(vector, 1.e-8), vector))
        local x, y, z = Math.BreakVector(vector, nil, nil, nil)
        assert(x == 1 and y == 0 and z == 0)
        return true
    )lua");
    TestTrue(TEXT("Engine structs, reusable returns and output order survive conversion"),
        Result.isBool() && Result.castTo<bool>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBaselineMethodLookupTest,
    "Slua.Baseline.MethodLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBaselineMethodLookupTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("BaselineLookup", nullptr, false);
    if (!TestTrue(TEXT("VM initializes"), State.init())) return false;
    LuaVar Self = State.doString(R"lua(
        return setmetatable({}, {__index=function(self, key, luaFirst)
            assert(luaFirst)
            if key == "Fail" then error("lookup fixture failure") end
            if key == "Invalid" then return 42 end
            if key == "Present" then return function() return 42 end end
        end})
    )lua");
    lua_State* L = State.getLuaState();
    const int Top = lua_gettop(L);
    LuaVar Function;
    FString Error;
    TestEqual(TEXT("present method resolves"),
        ILuaOverriderInterface::LookupLuaFunction(L, Self, TEXT("Present"), Function, Error), LUA_OK);
    TestTrue(TEXT("resolved method is callable"), Function.isFunction() && Function.call().castTo<int>() == 42);
    TestEqual(TEXT("absent method is not an error"),
        ILuaOverriderInterface::LookupLuaFunction(L, Self, TEXT("Missing"), Function, Error), LUA_OK);
    TestTrue(TEXT("absent method is nil"), Function.isNil() && Error.IsEmpty());
    TestEqual(TEXT("lookup errors are protected"),
        ILuaOverriderInterface::LookupLuaFunction(L, Self, TEXT("Fail"), Function, Error), LUA_ERRRUN);
    TestTrue(TEXT("lookup error retains its cause"), Error.Contains(TEXT("lookup fixture failure")));
    TestEqual(TEXT("non-callable values fail"),
        ILuaOverriderInterface::LookupLuaFunction(L, Self, TEXT("Invalid"), Function, Error), LUA_ERRRUN);
    TestEqual(TEXT("lookup restores the stack"), lua_gettop(L), Top);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBaselineLocalLifetimeTest,
    "Slua.Baseline.FunctionLocalLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBaselineLocalLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("BaselineLocals", nullptr, false);
    if (!TestTrue(TEXT("VM initializes"), State.init())) return false;
    for (bool ForceError : {false, true})
    {
        int32 Initialized = 0;
        int32 Destroyed = 0;
        TestTrue(TEXT("native invocation returns the expected status"),
            LuaFunctionAccelerator::runFunctionLocalLifetimeProbeForTests(
                State.getLuaState(), ForceError, Initialized, Destroyed));
        TestEqual(TEXT("all non-trivial locals initialize"), Initialized, 3);
        TestEqual(TEXT("success and Lua errors both destroy locals"), Destroyed, Initialized);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaBaselineStateIsolationTest,
    "Slua.Baseline.StateIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaBaselineStateIsolationTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    const int32 InitialStates = LuaState::getRegisteredStateCountForTests();
    LuaState First("BaselineFirst", nullptr, false);
    LuaState Second("BaselineSecond", nullptr, false);
    if (!TestTrue(TEXT("both VMs initialize"), First.init() && Second.init())) return false;
    LuaVar FirstFunction = First.doString("return function() return 1 end");
    LuaVar SecondFunction = Second.doString("return import('KismetMathLibrary').Add_IntInt");
    First.close();
    TestFalse(TEXT("closed VM values become invalid"), FirstFunction.isValid());
    TestEqual(TEXT("only the closed VM is unregistered"),
        LuaState::getRegisteredStateCountForTests(), InitialStates + 1);
    TestEqual(TEXT("surviving VM retains its reflected function"), SecondFunction.call(20, 22).castTo<int>(), 42);
    First.close();
    Second.close();
    TestEqual(TEXT("both VMs leave the registry"), LuaState::getRegisteredStateCountForTests(), InitialStates);
    return true;
}

#endif
