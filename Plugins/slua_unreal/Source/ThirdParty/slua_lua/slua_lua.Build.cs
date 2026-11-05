// Tencent is pleased to support the open source community by making sluaunreal available.

using UnrealBuildTool;
using System.IO;

public class slua_lua : ModuleRules
{
    public slua_lua(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        var pluginDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../.."));
        var externalLib = Path.Combine(pluginDirectory, "Library");

        if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "iOS/liblua.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
#if UE_4_24_OR_LATER
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/armeabi-v7a/liblua.a"));
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/armeabi-arm64/liblua.a"));
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Android/x86/liblua.a"));
#else
            PublicLibraryPaths.Add(Path.Combine(externalLib, "Android/armeabi-arm64"));
            PublicLibraryPaths.Add(Path.Combine(externalLib, "Android/armeabi-v7a"));
            PublicLibraryPaths.Add(Path.Combine(externalLib, "Android/x86"));
            PublicAdditionalLibraries.Add("lua");
#endif
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Win64/lua.lib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Mac/liblua.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(externalLib, "Linux/liblua.a"));
        }
    }
}
