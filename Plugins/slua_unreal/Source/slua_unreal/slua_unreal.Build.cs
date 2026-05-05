// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

using UnrealBuildTool;
using System.IO;

public class slua_unreal : ModuleRules
{
    public slua_unreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        // enable exception
        bEnableExceptions = true;
#if UE_5_2_OR_LATER
        IWYUSupport = IWYUSupport.None;
#else
        bEnforceIWYU = false;
#endif
#if UE_5_7_OR_LATER
        CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
#else
        bEnableUndefinedIdentifierWarnings = false;
#endif

        var externalSource = Path.Combine(PluginDirectory, "External");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
#if UE_4_21_OR_LATER
            // PublicDefinitions / PrivateDefinitions only exist on UE 4.21+.
            // Older engines must fall back to the unified Definitions list.
            PublicDefinitions.Add("LUA_BUILD_AS_DLL=1");
            PrivateDefinitions.Add("LUA_CORE=1");
#else
            Definitions.Add("LUA_BUILD_AS_DLL=1");
            Definitions.Add("LUA_CORE=1");
#endif
        }

        PublicIncludePaths.AddRange(
            new string[] {
                externalSource,
                Path.Combine(externalSource, "lua"),
                // ... add public include paths required here ...
            }
            );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                // ... add other public dependencies that you statically link with here ...
            }
            );

        if (Target.Type == TargetRules.TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "UMG",
                "InputCore",
                "NetCore",
                // Keep lua.lib private so game modules import Lua through slua_unreal.dll.
                "slua_lua",
                // ... add private dependencies that you statically link with here ...
            }
            );

#if UE_4_21_OR_LATER
        PublicDefinitions.Add("ENABLE_PROFILER");
        PublicDefinitions.Add("NS_SLUA=slua");
#else
        Definitions.Add("ENABLE_PROFILER");
        Definitions.Add("NS_SLUA=slua");
#endif
    }
}
