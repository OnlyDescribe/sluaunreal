// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#include "LuaFunctionAccelerator.h"

#include "LatentDelegate.h"
#include "LuaOverrider.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    NS_SLUA::lua_State* GFunctionLocalLifetimeProbeState = nullptr;

    void FunctionLocalLifetimeProbe(UObject* Context, FFrame& Stack, RESULT_DECL)
    {
        if (GFunctionLocalLifetimeProbeState)
        {
            luaL_error(GFunctionLocalLifetimeProbeState, "forced local lifetime probe failure");
        }
    }
}
#endif

namespace NS_SLUA
{
    TMap<UFunction*, LuaFunctionAccelerator*> LuaFunctionAccelerator::cache;

    inline bool isLatentProperty(FProperty* prop)
    {
        FStructProperty* structProp = CastField<FStructProperty>(prop);
        if (structProp && (structProp->Struct == FLatentActionInfo::StaticStruct()))
        {
	        return true;
        }
	    return false;
    }

    LuaFunctionAccelerator::LuaFunctionAccelerator(UFunction* inFunc)
        : func(inFunc)
        , bLuaOverride(ULuaOverrider::isUFunctionHooked(inFunc))
    {
        auto funcFlag = func->FunctionFlags;
        bNativeFunc = (funcFlag & EFunctionFlags::FUNC_Native) != 0;

        int propIndex = 0;
        for (TFieldIterator<FProperty> it(func); it && (it->PropertyFlags & CPF_Parm); ++it, ++propIndex)
        {
            FProperty* prop = *it;
            uint64 propflag = prop->GetPropertyFlags();

            if (prop->HasAnyPropertyFlags(CPF_OutParm))
            {
                outParmRecProps.Add(prop);
            }

            FCheckerInfo checkerInfo = {false, false, false, false, propIndex, prop->GetOffset_ForInternal(), prop};
            FCheckerInfo* checkerRef = &checkerInfo;
            if (!prop->HasAnyPropertyFlags(CPF_NoDestructor))
            {
                checkerInfo.bInit = true;
                checkerInfo.bReference = IsReferenceParam(prop->PropertyFlags, func) && LuaObject::getReferencer(prop);
                paramsChecker.Add(checkerInfo);
                
                checkerRef = &paramsChecker.Top();
            }

            if (bNativeFunc)
            {
                if ((propflag & CPF_ReturnParm))
                    continue;
            }
            else if (IsRealOutParam(propflag))
                continue;
            
            if (isLatentProperty(prop))
            {
                checkerRef->bLatent = true;
            }
            else
            {
                auto checker = LuaObject::getChecker(prop);
                checkerRef->bCheck = true;
                checkerRef->checker = checker;
            }

            if (checkerRef == &checkerInfo)
            {
                paramsChecker.Add(checkerInfo);
            }
        }

        bHasReturnParam = func->ReturnValueOffset != MAX_uint16;
        if (bHasReturnParam)
        {
            auto returnProperty = func->GetReturnProperty();
            auto pusher = LuaObject::getPusher(returnProperty);

            returnPusherInfo.bReference = false;
            returnPusherInfo.offset = returnProperty->GetOffset_ForInternal();
            returnPusherInfo.prop = returnProperty;
            returnPusherInfo.pusher = pusher;
        }

        int i = 0;
        for (TFieldIterator<FProperty> it(func); it && (it->PropertyFlags & CPF_Parm); ++it)
        {
            FProperty* prop = *it;
            auto propflag = prop->PropertyFlags;
            // skip return param
            if(propflag&CPF_ReturnParm)
                continue;
                
            if (!isLatentProperty(prop))
            {
                if (IsRealOutParam(propflag))
                {
                    if (IsReferenceParam(propflag, func))
                    {
                        const auto referencer = LuaObject::getReferencer(prop);
                        if (referencer)
                        {
                            FPusherInfo pusherInfo = {true, i, prop->GetOffset_ForInternal(), prop, referencer, LuaObject::getPusher(prop)};
                            outPropsPusher.Add(pusherInfo);
                        }
                        else
                        {
                            FPusherInfo pusherInfo = {false, i, prop->GetOffset_ForInternal(), prop, nullptr, LuaObject::getPusher(prop)};
                            outPropsPusher.Add(pusherInfo);
                        }
                    }
                    else
                    {
                        FPusherInfo pusherInfo = {false, i, prop->GetOffset_ForInternal(), prop, nullptr, LuaObject::getPusher(prop)};
                        outPropsPusher.Add(pusherInfo);
                    }

                    if (bNativeFunc)
                    {
                        i++;
                    }
                }
                else
                {
                    i++;
                }
            }
        }
    }

    LuaFunctionAccelerator* LuaFunctionAccelerator::findOrAdd(UFunction* inFunc)
    {
        auto ret = cache.Find(inFunc);
        if (ret)
        {
            return *ret;
        }
        
        auto value = new LuaFunctionAccelerator(inFunc);
        cache.Emplace(inFunc, value);
        return value;
    }

    bool LuaFunctionAccelerator::remove(UFunction* inFunc)
    {
        auto funcPtr = cache.Find(inFunc);
        if (funcPtr)
        {
            delete *funcPtr;
            cache.Remove(inFunc);
            return true;
        }

        return false;
    }

    void LuaFunctionAccelerator::clear()
    {
        for (auto iter : cache)
        {
            delete iter.Value;
        }

        cache.Empty();
    }

    struct LuaFunctionAccelerator::FProtectedCallContext
    {
        LuaFunctionAccelerator* accelerator;
        UObject* object;
        NewObjectRecorder* objectRecorder;
        uint8* params;
        FProperty** nextParamProperty;
        PTRINT* outParams;
        FFrame* stack;
        bool* isLatentFunction;
        int argumentCount;
        int nextArgumentIndex;
    };

    int LuaFunctionAccelerator::protectedFillParams(lua_State* L)
    {
        FProtectedCallContext* context = static_cast<FProtectedCallContext*>(
            lua_touserdata(L, lua_upvalueindex(1)));
        check(context && context->accelerator);

        int argumentIndex = 1;
        for (FCheckerInfo& checkerInfo : context->accelerator->paramsChecker)
        {
            FProperty* prop = checkerInfo.prop;
            if (checkerInfo.bInit && !(checkerInfo.bReference && lua_type(L, argumentIndex) == LUA_TUSERDATA))
            {
                if (!prop->HasAnyPropertyFlags(CPF_ZeroConstructor))
                {
                    prop->InitializeValue_InContainer(context->params);
                }
                *context->nextParamProperty = prop;
                ++context->nextParamProperty;
            }

            if (checkerInfo.bLatent)
            {
                lua_State* mainThread = L->l_G->mainthread;
                ULatentDelegate* latentObject = LuaObject::getLatentDelegate(mainThread);
                const int threadRef = latentObject ? latentObject->getThreadRef(L) : LUA_REFNIL;
                if (threadRef == LUA_REFNIL || threadRef == LUA_NOREF)
                {
                    return luaL_error(L, "latent UFunction must be called from a live coroutine");
                }

                FLatentActionInfo latentActionInfo(threadRef, GetTypeHash(FGuid::NewGuid()),
                    *ULatentDelegate::NAME_LatentCallback, latentObject);
                prop->CopySingleValue(prop->ContainerPtrToValuePtr<void>(context->params), &latentActionInfo);
                *context->isLatentFunction = true;
            }
            else if (checkerInfo.bCheck)
            {
                PTRINT* pointer = context->outParams + checkerInfo.index;
                *pointer = PTRINT(0);
                const uint64 propFlags = prop->GetPropertyFlags();
                if ((propFlags & CPF_OutParm) && lua_isnil(L, argumentIndex))
                {
                    ++argumentIndex;
                    continue;
                }

                *pointer = PTRINT(checkerInfo.checker(
                    L, prop, context->params + checkerInfo.offset, argumentIndex, false));
                ++argumentIndex;
            }
        }

        context->nextArgumentIndex = argumentIndex;
        return 0;
    }

    int LuaFunctionAccelerator::protectedInvoke(lua_State* L)
    {
        FProtectedCallContext* context = static_cast<FProtectedCallContext*>(
            lua_touserdata(L, lua_upvalueindex(1)));
        check(context && context->accelerator && context->stack);

        LuaFunctionAccelerator* accelerator = context->accelerator;
        UFunction* function = accelerator->func;
        const EFunctionFlags functionFlags = function->FunctionFlags;
        uint8* returnValueAddress = accelerator->bHasReturnParam
            ? context->params + function->ReturnValueOffset
            : nullptr;

        if (functionFlags & FUNC_Net)
        {
#if (ENGINE_MINOR_VERSION<25) && (ENGINE_MAJOR_VERSION==4)
            const int32 functionCallspace = context->object->GetFunctionCallspace(
                function, context->params, context->stack);
#else
            const int32 functionCallspace = context->object->GetFunctionCallspace(function, context->stack);
#endif
            uint8* savedCode = nullptr;
            if (functionCallspace & FunctionCallspace::Remote)
            {
                savedCode = context->stack->Code;
                context->object->CallRemoteFunction(
                    function, context->params, context->stack->OutParms, context->stack);
            }

            if (functionCallspace & FunctionCallspace::Local)
            {
                if (savedCode)
                {
                    context->stack->Code = savedCode;
                }
#if ENGINE_MINOR_VERSION >= 23 && (PLATFORM_MAC || PLATFORM_IOS)
                FFrame* frame = context->stack;
                function->Invoke(context->object, *frame, returnValueAddress);
#else
                function->Invoke(context->object, *context->stack, returnValueAddress);
#endif
            }
        }
        else
        {
            function->Invoke(context->object, *context->stack, returnValueAddress);
        }

        return 0;
    }

    int LuaFunctionAccelerator::protectedPushResults(lua_State* L)
    {
        FProtectedCallContext* context = static_cast<FProtectedCallContext*>(
            lua_touserdata(L, lua_upvalueindex(1)));
        check(context && context->accelerator);

        LuaFunctionAccelerator* accelerator = context->accelerator;
        int32 returnCount = 0;
        if (accelerator->bHasReturnParam)
        {
            const FPusherInfo& returnInfo = accelerator->returnPusherInfo;
            const int reusableArgument = context->nextArgumentIndex <= context->argumentCount
                ? context->nextArgumentIndex
                : 0;
            returnCount += returnInfo.pusher(L, returnInfo.prop,
                context->params + returnInfo.offset, reusableArgument, context->objectRecorder);
        }

        for (const FPusherInfo& pusherInfo : accelerator->outPropsPusher)
        {
            FProperty* prop = pusherInfo.prop;
            uint8* source = context->params + pusherInfo.offset;
            const int32 index = pusherInfo.index;
            if (pusherInfo.bReference && context->outParams[index])
            {
                pusherInfo.referencePusher(
                    L, prop, source, reinterpret_cast<void*>(context->outParams[index]));
                lua_pushvalue(L, 1 + index);
                ++returnCount;
            }
            else
            {
                returnCount += pusherInfo.pusher(L, prop, source, 0, context->objectRecorder);
            }
        }

        return returnCount;
    }

    int LuaFunctionAccelerator::call(lua_State* L, int offset, UObject* obj, bool& isLatentFunction, NewObjectRecorder* objRecorder)
    {
        isLatentFunction = false;
        auto funcFlag = func->FunctionFlags;
        if (!(funcFlag & FUNC_Net) && !bNativeFunc && func->Script.Num() == 0)
        {
            return 0;
        }

        int i = offset;
        uint16 propertiesSize = func->PropertiesSize;
        uint8* params = (uint8*)FMemory_Alloca(propertiesSize);
        uint16 paramsPointerSize = func->NumParms * sizeof(void*);
        FProperty** propertyList = (FProperty**)FMemory_Alloca(paramsPointerSize);
        PTRINT* outParams = (PTRINT*)FMemory_Alloca(paramsPointerSize);

        if (propertiesSize)
            FMemory::Memzero(params, propertiesSize);
        if (paramsPointerSize)
        {
            FMemory::Memzero(propertyList, paramsPointerSize);
            FMemory::Memzero(outParams, paramsPointerSize);
        }

        FFrame newStack(obj, func, params, nullptr,
#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION > 4
            func->ChildProperties
#else
            func->Children
#endif
        );

        checkSlow(newStack.Locals || func->ParmsSize == 0);
        FOutParmRec** lastOut = &newStack.OutParms;
        AutoDestructor autoDestructor(propertyList, params, func->NumParms);

        for (auto prop : outParmRecProps)
        {
            CA_SUPPRESS(6263)
            auto out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
            out->Property = prop;
            out->PropAddr = prop->ContainerPtrToValuePtr<uint8>(params);

            if (*lastOut)
            {
                (*lastOut)->NextOutParm = out;
                lastOut = &(*lastOut)->NextOutParm;
            }
            else
            {
                *lastOut = out;
            }
        }

        if (*lastOut)
        {
            (*lastOut)->NextOutParm = NULL;
        }

        const int originalArgumentCount = lua_gettop(L);
        const int protectedArgumentCount = FMath::Max(originalArgumentCount - offset + 1, 0);
        FProtectedCallContext context = {
            this,
            obj,
            objRecorder,
            params,
            propertyList,
            outParams,
            &newStack,
            &isLatentFunction,
            protectedArgumentCount,
            1
        };

        auto callProtected = [&](lua_CFunction protectedFunction, bool bCopyArguments, int resultCount)
        {
            lua_pushlightuserdata(L, &context);
            lua_pushcclosure(L, protectedFunction, 1);
            int copiedArgumentCount = 0;
            if (bCopyArguments)
            {
                for (int argumentIndex = offset; argumentIndex <= originalArgumentCount; ++argumentIndex)
                {
                    lua_pushvalue(L, argumentIndex);
                    ++copiedArgumentCount;
                }
            }
            return lua_pcall(L, copiedArgumentCount, resultCount, 0);
        };

        if (callProtected(&LuaFunctionAccelerator::protectedFillParams, true, 0) != LUA_OK)
        {
            return -1;
        }

        AutoLocalDestructor localDestructor(func, params);
        localDestructor.Initialize();
#if WITH_DEV_AUTOMATION_TESTS
        lastLocalInitializedCount = localDestructor.initializedCount;
#endif
        const int invokeStatus = callProtected(&LuaFunctionAccelerator::protectedInvoke, false, 0);
        localDestructor.Destroy();
#if WITH_DEV_AUTOMATION_TESTS
        lastLocalDestroyedCount = localDestructor.destroyedCount;
#endif
        if (invokeStatus != LUA_OK)
        {
            return -1;
        }

        const int stackTopBeforeResults = lua_gettop(L);
        if (callProtected(&LuaFunctionAccelerator::protectedPushResults, true, LUA_MULTRET) != LUA_OK)
        {
            return -1;
        }

        return lua_gettop(L) - stackTopBeforeResults;
    }

#if WITH_DEV_AUTOMATION_TESTS
    bool LuaFunctionAccelerator::runFunctionLocalLifetimeProbeForTests(
        lua_State* L, bool bForceLuaError, int32& outInitializedCount, int32& outDestroyedCount)
    {
        outInitializedCount = 0;
        outDestroyedCount = 0;
        if (!L)
        {
            return false;
        }

        const FName FunctionName = MakeUniqueObjectName(
            UObject::StaticClass(), UFunction::StaticClass(), TEXT("SluaFunctionLocalLifetimeProbe"));
        UFunction* function = NewObject<UFunction>(UObject::StaticClass(), FunctionName, RF_Transient);
        function->FunctionFlags = FUNC_Native | FUNC_HasDefaults;

        FStructProperty* structLocal = new FStructProperty(function, TEXT("StructLocal"), RF_Public);
        structLocal->Struct = TBaseStructure<FSoftObjectPath>::Get();
        function->AddCppProperty(structLocal);

        FArrayProperty* arrayLocal = new FArrayProperty(function, TEXT("ArrayLocal"), RF_Public);
        arrayLocal->AddCppProperty(new FStrProperty(arrayLocal, TEXT("Inner"), RF_Public));
        function->AddCppProperty(arrayLocal);

        FStrProperty* stringLocal = new FStrProperty(function, TEXT("StringLocal"), RF_Public);
        function->AddCppProperty(stringLocal);
        function->StaticLink(true);
        function->SetNativeFunc(&FunctionLocalLifetimeProbe);

        // A native-class-owned synthetic UFunction does not receive the Blueprint-generated
        // local chains. Build the same chains explicitly so the probe exercises the runtime
        // contract consumed by LuaFunctionAccelerator::call().
        function->FirstPropertyToInit = stringLocal;
        stringLocal->PostConstructLinkNext = arrayLocal;
        arrayLocal->PostConstructLinkNext = structLocal;
        structLocal->PostConstructLinkNext = nullptr;
        function->DestructorLink = stringLocal;
        stringLocal->DestructorLinkNext = arrayLocal;
        arrayLocal->DestructorLinkNext = structLocal;
        structLocal->DestructorLinkNext = nullptr;

        LuaFunctionAccelerator accelerator(function);
        bool bLatentFunction = false;
        const int32 stackTop = lua_gettop(L);
        GFunctionLocalLifetimeProbeState = bForceLuaError ? L : nullptr;
        const int32 result = accelerator.call(L, 1, GetTransientPackage(), bLatentFunction, nullptr);
        GFunctionLocalLifetimeProbeState = nullptr;

        outInitializedCount = accelerator.lastLocalInitializedCount;
        outDestroyedCount = accelerator.lastLocalDestroyedCount;
        const bool bExpectedResult = bForceLuaError ? result < 0 : result == 0;
        if (result < 0 && lua_gettop(L) > stackTop)
        {
            lua_settop(L, stackTop);
        }
        return bExpectedResult && !bLatentFunction;
    }
#endif

    void LuaFunctionAccelerator::fillParam(lua_State* L,int i, NewObjectRecorder* objRecorder,const PostFillParamCallback& callback ,bool &isLatentFunction) {
        uint16 paramsPointerSize = func->NumParms * sizeof(void*);
        uint16 parmsSize = func->ParmsSize;
        uint8* params = (uint8*)FMemory_Alloca(parmsSize);
        FProperty** propertyList = (FProperty**)FMemory_Alloca(paramsPointerSize);
        PTRINT* outParams = (PTRINT*)FMemory_Alloca(paramsPointerSize);
        if (parmsSize)
            FMemory::Memzero(params, func->ParmsSize);
        if (paramsPointerSize)
            FMemory::Memzero(propertyList, paramsPointerSize);

        AutoDestructor autoDestructor(propertyList, params, func->NumParms);
        isLatentFunction = false;
        for (auto& checkerInfo : paramsChecker)
        {
            auto prop = checkerInfo.prop;
            if (checkerInfo.bInit && !(checkerInfo.bReference && (lua_type(L, i) == LUA_TUSERDATA)))
            {
                if (!prop->HasAnyPropertyFlags(CPF_ZeroConstructor))
                {
                    prop->InitializeValue_InContainer(params);
                }
                *propertyList = prop;
                ++propertyList;
            }

            if (checkerInfo.bLatent)
            {
                // bind a callback to the latent function
                lua_State* mainThread = L->l_G->mainthread;

                ULatentDelegate* latentObj = LuaObject::getLatentDelegate(mainThread);
                int threadRef = latentObj->getThreadRef(L);
                FLatentActionInfo LatentActionInfo(threadRef, GetTypeHash(FGuid::NewGuid()),
                                                   *ULatentDelegate::NAME_LatentCallback, latentObj);

                prop->CopySingleValue(prop->ContainerPtrToValuePtr<void>(params), &LatentActionInfo);
                isLatentFunction = true;
            }
            else if (checkerInfo.bCheck)
            {
                PTRINT* pointer = outParams + checkerInfo.index;
                *pointer = PTRINT(0);
                // if is out param, can accept nil
                uint64 propflag = prop->GetPropertyFlags();
                if ((propflag & CPF_OutParm) && lua_isnil(L, i))
                {
                    i++;
                    continue;
                }

                auto checker = checkerInfo.checker;
                *pointer = PTRINT(checker(L, prop, params + checkerInfo.offset, i, false));
                i++;
            }
        }

        callback(params, outParams, objRecorder);
    }

    int LuaFunctionAccelerator::returnValue(lua_State* L, int i, uint8* params, PTRINT* outParams, NewObjectRecorder* objRecorder)
    {
        int32 ret = 0;
        if (bHasReturnParam)
        {
            auto returnProperty = returnPusherInfo.prop;
            ret += returnPusherInfo.pusher(L, returnProperty, params + returnPusherInfo.offset, 0, objRecorder);
        }

        for (auto &pusherInfo : outPropsPusher)
        {
            auto prop = pusherInfo.prop;
            auto propflag = prop->PropertyFlags;
            
            uint8* src = params+pusherInfo.offset;
            int32 index = pusherInfo.index;
            if (pusherInfo.bReference && *(outParams + index))
            {
                pusherInfo.referencePusher(L, prop, src, reinterpret_cast<void*>(*(outParams + index)));
                lua_pushvalue(L, i + index);
                ret++;
            }
            else
            {
                ret += pusherInfo.pusher(L, prop, src, 0, objRecorder);
            }
        }
        
        return ret;
    }
}
