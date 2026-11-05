#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "LatentDelegate.h"
#include "LuaFunctionAccelerator.h"
#include "LuaOverriderSuper.h"
#include "LuaState.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    NS_SLUA::lua_State* ErrorState = nullptr;
    int32 InvocationCount = 0;

    void TestFunction(UObject*, FFrame& Stack, RESULT_DECL)
    {
        ++InvocationCount;
        if (ErrorState) luaL_error(ErrorState, "convergence invocation failure");
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaReflectedCallTest,
    "Slua.Runtime.ReflectedCalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaReflectedCallTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("ReflectedCalls", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init())) return false;
    lua_State* L = State.getLuaState();
    TStrongObjectPtr<UFunction> Function(NewObject<UFunction>(UActorComponent::StaticClass(),
        MakeUniqueObjectName(UActorComponent::StaticClass(), UFunction::StaticClass(), TEXT("SluaReflectedCall"))));
    Function->FunctionFlags = FUNC_Native;
    for (int i = 0; i < 64; ++i)
    {
        auto* Property = new FIntProperty(Function.Get(), FName(*FString::Printf(TEXT("P%d"), i)));
        Property->SetPropertyFlags(CPF_Parm);
        Function->AddCppProperty(Property);
    }
    Function->StaticLink(true);
    Function->SetNativeFunc(&TestFunction);
    TStrongObjectPtr<UActorComponent> Receiver(NewObject<USceneComponent>());
    LuaObject::push(L, Receiver.Get());
    lua_setglobal(L, "receiver");
    lua_pushnil(L);
    lua_pushliteral(L, "method");
    LuaObject::push(L, Function.Get());
    lua_setglobal(L, "method");
    lua_settop(L, 0);
    auto* Accelerator = LuaFunctionAccelerator::findOrAdd(Function.Get());
    LuaObject::pushType(L, new LuaSuperCall(Receiver.Get()), "LuaSuperCall",
        LuaSuperCall::setupMetatable, LuaSuperCall::genericGC);
    lua_setglobal(L, "superReceiver");
    lua_pushlightuserdata(L, Accelerator);
    lua_pushcclosure(L, LuaSuperCall::__superCall, 1);
    lua_setglobal(L, "superMethod");

    auto Run = [&](const char* Script) {
        LuaVar Result = State.doString(Script);
        return Result.isBool() && Result.castTo<bool>();
    };
    InvocationCount = 0;
    TestTrue(TEXT("large argument list passes both ordinary and Super entries"), Run(R"lua(
        args = {}; for i = 1, 64 do args[i] = i end
        method(receiver, table.unpack(args))
        superMethod(superReceiver, table.unpack(args))
        return true
    )lua"));
    TestEqual(TEXT("both native calls executed"), InvocationCount, 2);
    TestTrue(TEXT("return reuse and ref/out values preserve their argument positions"), Run(R"lua(
        local math = import('KismetMathLibrary')
        local value = math.MakeVector(3, 0, 0)
        assert(rawequal(math.MakeVector(6, 0, 0, value), value))
        assert(rawequal(math.Vector_Normalize(value, 1.e-8), value))
        local x, y, z = math.BreakVector(value, nil, nil, nil)
        return x == 1 and y == 0 and z == 0
    )lua"));
    TestTrue(TEXT("native invocation can reenter Lua and another reflected call"), Run(R"lua(
        local calls = 0
        receiver.OnComponentActivated:Add(function(component, reset)
            assert(component == receiver and reset)
            assert(import('KismetMathLibrary').Add_IntInt(20, 22) == 42)
            calls = calls + 1
        end)
        receiver:Activate(true)
        receiver.OnComponentActivated:Clear()
        return calls == 1
    )lua"));

    if (FParse::Param(FCommandLine::Get(), TEXT("SluaFunctionCallPerf")))
    {
        struct FAllocations { lua_Alloc Original; void* User; uint64 Count = 0, Bytes = 0; } Allocations;
        Allocations.Original = lua_getallocf(L, &Allocations.User);
        lua_setallocf(L, [](void* User, void* Pointer, size_t OldSize, size_t NewSize) -> void*
        {
            auto& Stats = *static_cast<FAllocations*>(User);
            if (NewSize > 0 && !Pointer) { ++Stats.Count; Stats.Bytes += NewSize; }
            return Stats.Original(Stats.User, Pointer, OldSize, NewSize);
        }, &Allocations);
        ON_SCOPE_EXIT { lua_setallocf(L, Allocations.Original, Allocations.User); };
        const char* Workloads[] = {
            "local f=import('KismetMathLibrary').Add_IntInt; return function(n) local r; for i=1,n do r=f(20,22) end return r==42 end",
            "local f=import('KismetMathLibrary').MakeVector; local v=f(0,0,0); return function(n) local r; for i=1,n do r=f(3,4,5,v) end return rawequal(r,v) and v.X==3 end",
            "local f,object,values,unpack=method,receiver,args,table.unpack; return function(n) for i=1,n do f(object,unpack(values)) end return true end"
        };
        const TCHAR* Names[] = {TEXT("Scalar"), TEXT("StructReuse"), TEXT("Arguments64")};
        constexpr int32 Calls = 50000;
        for (int32 Workload = 0; Workload < UE_ARRAY_COUNT(Workloads); ++Workload)
        {
            LuaVar Driver = State.doString(Workloads[Workload]);
            if (!TestTrue(TEXT("workload compiles"), Driver.isFunction())) return false;
            for (int32 Round = 0; Round < 7; ++Round)
            {
                lua_gc(L, LUA_GCCOLLECT, 0);
                if (!TestTrue(TEXT("workload warms up"), Driver.call<bool>(2000))) return false;
                Allocations.Count = Allocations.Bytes = 0;
                const double Start = FPlatformTime::Seconds();
                const bool Success = Driver.call<bool>(Calls);
                const double Nanoseconds = (FPlatformTime::Seconds() - Start) * 1.e9 / Calls;
                if (!TestTrue(TEXT("workload result"), Success)) return false;
                AddInfo(FString::Printf(TEXT("SluaFunctionCallPerf,%s,%d,%d,%.2f,%llu,%llu"),
                    Names[Workload], Round, Calls, Nanoseconds, Allocations.Count, Allocations.Bytes));
            }
        }
    }
    ErrorState = L;
    TestTrue(TEXT("ordinary and Super errors propagate after native storage cleanup"), Run(R"lua(
        local a, e = pcall(method, receiver, table.unpack(args))
        local b, f = pcall(superMethod, superReceiver, table.unpack(args))
        return not a and not b and e:find('convergence invocation failure', 1, true)
            ~= nil and f:find('convergence invocation failure', 1, true) ~= nil
    )lua"));
    ErrorState = nullptr;
    TestTrue(TEXT("cached accelerator removed"), LuaFunctionAccelerator::remove(Function.Get()));
    TestFalse(TEXT("Accelerator validity set rejects removed pointer"), LuaFunctionAccelerator::IsValid(Accelerator));
    TestTrue(TEXT("retained closures reject invalid accelerator"), Run(R"lua(
        return not pcall(method, receiver, table.unpack(args))
            and not pcall(superMethod, superReceiver, table.unpack(args))
    )lua"));
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    LuaObject::push(L, World);
    lua_setglobal(L, "world");
    TestTrue(TEXT("reflected Delay rejects the main thread and registers a coroutine"), Run(R"lua(
        local delay = import('KismetSystemLibrary').Delay
        assert(not pcall(delay, world, 0.001))
        finished = false
        thread = coroutine.create(function()
            delay(world, 0.001)
            delay(world, 0.001)
            finished = true
        end)
        assert(coroutine.resume(thread))
        return coroutine.status(thread) == 'suspended'
    )lua"));
    TestEqual(TEXT("Delay holds one coroutine reference"), State.getThreadCountForTests(), 1);
    for (int i = 0; i < 3; ++i)
    {
        World->GetLatentActionManager().BeginFrame();
        World->GetLatentActionManager().ProcessLatentActions(State.getLatentDelegate(), 0.01f);
    }
    TestTrue(TEXT("consecutive Delay actions resume the original coroutine"), Run("return finished and coroutine.status(thread) == 'dead'"));
    TestEqual(TEXT("Delay completion releases its coroutine reference"), State.getThreadCountForTests(), 0);
    World->DestroyWorld(false);
    TestEqual(TEXT("stack restored"), lua_gettop(L), 0);
    State.close();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSluaLatentFailureLifetimeTest,
    "Slua.Runtime.LatentFailureLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSluaLatentFailureLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace NS_SLUA;
    LuaState State("LatentFailureLifetime", nullptr, false);
    if (!TestTrue(TEXT("state initializes"), State.init())) return false;
    lua_State* L = State.getLuaState();
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TStrongObjectPtr<UActorComponent> Receiver(NewObject<USceneComponent>());
    TStrongObjectPtr<UFunction> Function(NewObject<UFunction>(UActorComponent::StaticClass(),
        MakeUniqueObjectName(UActorComponent::StaticClass(), UFunction::StaticClass(), TEXT("SluaLatentFailure"))));
    Function->FunctionFlags = FUNC_Native;
    auto* LatentInfo = new FStructProperty(Function.Get(), TEXT("LatentInfo"));
    LatentInfo->Struct = FLatentActionInfo::StaticStruct();
    LatentInfo->SetPropertyFlags(CPF_Parm);
    Function->AddCppProperty(LatentInfo);
    Function->StaticLink(true);
    Function->SetNativeFunc(&TestFunction);
    TStrongObjectPtr<UFunction> OutputFunction(NewObject<UFunction>(UActorComponent::StaticClass(),
        MakeUniqueObjectName(UActorComponent::StaticClass(), UFunction::StaticClass(), TEXT("SluaLatentOutputFailure"))));
    OutputFunction->FunctionFlags = FUNC_Native;
    auto* Return = new FStructProperty(OutputFunction.Get(), TEXT("ReturnValue"));
    Return->Struct = FLatentActionInfo::StaticStruct();
    Return->SetPropertyFlags(CPF_Parm | CPF_ReturnParm);
    OutputFunction->AddCppProperty(Return);
    auto* OutputLatentInfo = new FStructProperty(OutputFunction.Get(), TEXT("LatentInfo"));
    OutputLatentInfo->Struct = FLatentActionInfo::StaticStruct();
    OutputLatentInfo->SetPropertyFlags(CPF_Parm);
    OutputFunction->AddCppProperty(OutputLatentInfo);
    OutputFunction->StaticLink(true);
    OutputFunction->SetNativeFunc([](UObject*, FFrame&, RESULT_DECL) {});
    ON_SCOPE_EXIT
    {
        ErrorState = nullptr;
        LuaFunctionAccelerator::remove(Function.Get());
        LuaFunctionAccelerator::remove(OutputFunction.Get());
        State.close();
        World->DestroyWorld(false);
    };

    LuaObject::push(L, World);
    lua_setglobal(L, "world");
    LuaObject::push(L, Receiver.Get());
    lua_setglobal(L, "receiver");
    lua_pushnil(L);
    lua_pushliteral(L, "latentFailure");
    LuaObject::push(L, Function.Get());
    lua_setglobal(L, "latentFailure");
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushliteral(L, "latentOutputFailure");
    LuaObject::push(L, OutputFunction.Get());
    lua_setglobal(L, "latentOutputFailure");
    lua_settop(L, 0);
    lua_pushcfunction(L, [](lua_State* Thread) -> int
    {
        ErrorState = Thread;
        return 0;
    });
    lua_setglobal(L, "captureThread");
    lua_pushcfunction(L, [](lua_State* Thread) -> int
    {
        lua_pushboolean(Thread, LuaState::get(Thread)->findThread(Thread) != LUA_REFNIL);
        return 1;
    });
    lua_setglobal(L, "isRegistered");
    auto Run = [&](const char* Script)
    {
        LuaVar Result = State.doString(Script);
        return Result.isBool() && Result.castTo<bool>();
    };

    TestTrue(TEXT("new failed registrations roll back before the first Delay"), Run(R"lua(
        local delay = import('KismetSystemLibrary').Delay
        local unload = import('GameplayStatics').UnloadStreamLevel
        weakThread = setmetatable({}, { __mode = 'v' })
        callbackCount = 0
        finished = false
        receiver.OnComponentActivated:Add(function()
            assert(select(2, coroutine.running()), 'delegate must reenter the main state')
            collectgarbage('collect')
            assert(weakThread[1] ~= nil, 'running coroutine was collected')
            callbackCount = callbackCount + 1
        end)
        local thread = coroutine.create(function()
            captureThread()
            local ok, message = pcall(unload, world, 'UnusedLevel', nil)
            assert(not ok and message:find('boolean expected', 1, true))
            assert(not isRegistered(), 'parameter failure leaked a new registration')
            ok, message = pcall(latentFailure, receiver)
            assert(not ok and message:find('convergence invocation failure', 1, true))
            assert(not isRegistered(), 'invocation failure leaked a new registration')
            -- The extra return-reuse argument has the wrong type, so conversion fails after Invoke.
            assert(not pcall(latentOutputFailure, receiver, 1))
            assert(isRegistered(), 'output failure released a registration after Invoke completed')
            delay(world, 0.001)

            ok, message = pcall(unload, world, 'UnusedLevel', nil)
            assert(not ok and message:find('boolean expected', 1, true))
            -- Check ownership before GC so a regression fails without a use-after-free.
            assert(isRegistered(), 'parameter failure released the resumed coroutine')
            receiver.OnComponentActivated:Broadcast(receiver, false)
            ok, message = pcall(latentFailure, receiver)
            assert(not ok and message:find('convergence invocation failure', 1, true))
            assert(isRegistered(), 'invocation failure released the resumed coroutine')
            receiver.OnComponentActivated:Broadcast(receiver, false)
            assert(not pcall(latentOutputFailure, receiver, 1))
            assert(isRegistered(), 'output failure released the resumed coroutine')
            delay(world, 0.001)
            finished = true
        end)
        weakThread[1] = thread
        assert(coroutine.resume(thread))
        return coroutine.status(thread) == 'suspended'
    )lua"));
    TestEqual(TEXT("first Delay retains one registration"), State.getThreadCountForTests(), 1);
    World->GetLatentActionManager().BeginFrame();
    World->GetLatentActionManager().ProcessLatentActions(State.getLatentDelegate(), 0.01f);
    TestTrue(TEXT("both caught failures survive delegate reentry and GC"), Run("return callbackCount == 2 and not finished"));
    TestEqual(TEXT("second Delay reuses the live registration"), State.getThreadCountForTests(), 1);
    World->GetLatentActionManager().BeginFrame();
    World->GetLatentActionManager().ProcessLatentActions(State.getLatentDelegate(), 0.01f);
    TestTrue(TEXT("coroutine completes and can be collected"), Run(R"lua(
        receiver.OnComponentActivated:Clear()
        collectgarbage('collect')
        return finished and weakThread[1] == nil
    )lua"));
    TestEqual(TEXT("completion releases the registration"), State.getThreadCountForTests(), 0);
    TestEqual(TEXT("stack restored"), lua_gettop(L), 0);
    return true;
}

#endif
