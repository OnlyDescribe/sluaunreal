// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "lua_wrapperCommands.h"
#include "Runtime/Launch/Resources/Version.h"

#define LOCTEXT_NAMESPACE "Flua_wrapperModule"

void Flua_wrapperCommands::RegisterCommands()
{
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 7))
	UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputChord());
#else
	UI_COMMAND(OpenPluginWindow, "LuaWrapper", "Generate Lua Interface (Windows only)", EUserInterfaceActionType::Button, FInputGesture());
#endif
}

#undef LOCTEXT_NAMESPACE
