// Tencent is pleased to support the open source community by making sluaunreal available.

#include "CoreMinimal.h"

// -----------------------------------------------------------------------------
// Why this file exists
// -----------------------------------------------------------------------------
// The vendored Lua static library (Library/Win64/lua.lib) is built by the
// project's CMake target with LUA_STATIC_LINK defined, which (per
// External/lua/luaconf.h) compiles every Lua C API symbol with a plain
// `extern "C"` linkage -- no __declspec(dllexport) baked in. That keeps the
// .lib/.a portable and lets the standalone lua_cli executable link it.
//
// slua_unreal.dll links lua.lib privately (through the slua_lua external
// module) and is itself a UE module DLL. Without dllexport on the archive
// symbols, MSVC will NOT auto-export Lua's C API from slua_unreal.dll.
// Downstream modules (e.g. slua_profile, KeyGame Lua glue) and external
// debuggers that attach to the running process (notably EmmyLua's VS Code
// "attach to process" flow) need those symbols visible on the DLL's export
// table.
//
// We force-export them at link time with /EXPORT linker pragmas. This is
// emitted into slua_unreal.dll's command line, so it works for both the UE
// 4.x and UE 5.x toolchains (always link.exe on Win64 / MSVC).
//
// Notes:
//   * Guarded on PLATFORM_WINDOWS + _MSC_VER + LUA_BUILD_AS_DLL so non-Windows
//     and non-MSVC builds simply skip this block; they either don't need
//     export tables (static link on iOS/Android/Linux/Mac via slua_lua) or
//     handle visibility through compiler flags.
//   * If lua.lib is ever rebuilt without LUA_STATIC_LINK so its symbols carry
//     dllexport again, these pragmas become redundant but remain harmless.
// -----------------------------------------------------------------------------

#if PLATFORM_WINDOWS && defined(_MSC_VER) && defined(LUA_BUILD_AS_DLL)
// UBT builds the module import library before linking private static libraries.
// Keep Lua's C API visible to dependent modules while lua.lib stays private here.
#define SLUA_EXPORT_LUA_SYMBOL(Symbol) __pragma(comment(linker, "/EXPORT:" #Symbol))

SLUA_EXPORT_LUA_SYMBOL(lua_absindex)
SLUA_EXPORT_LUA_SYMBOL(lua_arith)
SLUA_EXPORT_LUA_SYMBOL(lua_atpanic)
SLUA_EXPORT_LUA_SYMBOL(lua_callk)
SLUA_EXPORT_LUA_SYMBOL(lua_checkstack)
SLUA_EXPORT_LUA_SYMBOL(lua_close)
SLUA_EXPORT_LUA_SYMBOL(lua_compare)
SLUA_EXPORT_LUA_SYMBOL(lua_concat)
SLUA_EXPORT_LUA_SYMBOL(lua_copy)
SLUA_EXPORT_LUA_SYMBOL(lua_createtable)
SLUA_EXPORT_LUA_SYMBOL(lua_error)
SLUA_EXPORT_LUA_SYMBOL(lua_gc)
SLUA_EXPORT_LUA_SYMBOL(lua_getallocf)
SLUA_EXPORT_LUA_SYMBOL(lua_getfield)
SLUA_EXPORT_LUA_SYMBOL(lua_getglobal)
SLUA_EXPORT_LUA_SYMBOL(lua_gethook)
SLUA_EXPORT_LUA_SYMBOL(lua_gethookcount)
SLUA_EXPORT_LUA_SYMBOL(lua_gethookmask)
SLUA_EXPORT_LUA_SYMBOL(lua_geti)
SLUA_EXPORT_LUA_SYMBOL(lua_getinfo)
SLUA_EXPORT_LUA_SYMBOL(lua_getlocal)
SLUA_EXPORT_LUA_SYMBOL(lua_getmetatable)
SLUA_EXPORT_LUA_SYMBOL(lua_getstack)
SLUA_EXPORT_LUA_SYMBOL(lua_gettable)
SLUA_EXPORT_LUA_SYMBOL(lua_gettop)
SLUA_EXPORT_LUA_SYMBOL(lua_getupvalue)
SLUA_EXPORT_LUA_SYMBOL(lua_getuservalue)
SLUA_EXPORT_LUA_SYMBOL(lua_iscfunction)
SLUA_EXPORT_LUA_SYMBOL(lua_isinteger)
SLUA_EXPORT_LUA_SYMBOL(lua_isnumber)
SLUA_EXPORT_LUA_SYMBOL(lua_isstring)
SLUA_EXPORT_LUA_SYMBOL(lua_isuserdata)
SLUA_EXPORT_LUA_SYMBOL(lua_isyieldable)
SLUA_EXPORT_LUA_SYMBOL(lua_len)
SLUA_EXPORT_LUA_SYMBOL(lua_load)
SLUA_EXPORT_LUA_SYMBOL(lua_newstate)
SLUA_EXPORT_LUA_SYMBOL(lua_newthread)
SLUA_EXPORT_LUA_SYMBOL(lua_newuserdata)
SLUA_EXPORT_LUA_SYMBOL(lua_next)
SLUA_EXPORT_LUA_SYMBOL(lua_pcallk)
SLUA_EXPORT_LUA_SYMBOL(lua_pushboolean)
SLUA_EXPORT_LUA_SYMBOL(lua_pushcclosure)
SLUA_EXPORT_LUA_SYMBOL(lua_pushfstring)
SLUA_EXPORT_LUA_SYMBOL(lua_pushinteger)
SLUA_EXPORT_LUA_SYMBOL(lua_pushlightuserdata)
SLUA_EXPORT_LUA_SYMBOL(lua_pushlstring)
SLUA_EXPORT_LUA_SYMBOL(lua_pushnil)
SLUA_EXPORT_LUA_SYMBOL(lua_pushnumber)
SLUA_EXPORT_LUA_SYMBOL(lua_pushstring)
SLUA_EXPORT_LUA_SYMBOL(lua_pushthread)
SLUA_EXPORT_LUA_SYMBOL(lua_pushvalue)
SLUA_EXPORT_LUA_SYMBOL(lua_pushvfstring)
SLUA_EXPORT_LUA_SYMBOL(lua_rawequal)
SLUA_EXPORT_LUA_SYMBOL(lua_rawget)
SLUA_EXPORT_LUA_SYMBOL(lua_rawgeti)
SLUA_EXPORT_LUA_SYMBOL(lua_rawgetp)
SLUA_EXPORT_LUA_SYMBOL(lua_rawlen)
SLUA_EXPORT_LUA_SYMBOL(lua_rawset)
SLUA_EXPORT_LUA_SYMBOL(lua_rawseti)
SLUA_EXPORT_LUA_SYMBOL(lua_rawsetp)
SLUA_EXPORT_LUA_SYMBOL(lua_resume)
SLUA_EXPORT_LUA_SYMBOL(lua_rotate)
SLUA_EXPORT_LUA_SYMBOL(lua_setallocf)
SLUA_EXPORT_LUA_SYMBOL(lua_setfield)
SLUA_EXPORT_LUA_SYMBOL(lua_setglobal)
SLUA_EXPORT_LUA_SYMBOL(lua_sethook)
SLUA_EXPORT_LUA_SYMBOL(lua_seti)
SLUA_EXPORT_LUA_SYMBOL(lua_setlocal)
SLUA_EXPORT_LUA_SYMBOL(lua_setmetatable)
SLUA_EXPORT_LUA_SYMBOL(lua_setonlyluac)
SLUA_EXPORT_LUA_SYMBOL(lua_settable)
SLUA_EXPORT_LUA_SYMBOL(lua_settop)
SLUA_EXPORT_LUA_SYMBOL(lua_setupvalue)
SLUA_EXPORT_LUA_SYMBOL(lua_setuservalue)
SLUA_EXPORT_LUA_SYMBOL(lua_status)
SLUA_EXPORT_LUA_SYMBOL(lua_stringtonumber)
SLUA_EXPORT_LUA_SYMBOL(lua_toboolean)
SLUA_EXPORT_LUA_SYMBOL(lua_tocfunction)
SLUA_EXPORT_LUA_SYMBOL(lua_tointegerx)
SLUA_EXPORT_LUA_SYMBOL(lua_tolstring)
SLUA_EXPORT_LUA_SYMBOL(lua_tonumberx)
SLUA_EXPORT_LUA_SYMBOL(lua_topointer)
SLUA_EXPORT_LUA_SYMBOL(lua_tothread)
SLUA_EXPORT_LUA_SYMBOL(lua_touserdata)
SLUA_EXPORT_LUA_SYMBOL(lua_type)
SLUA_EXPORT_LUA_SYMBOL(lua_typename)
SLUA_EXPORT_LUA_SYMBOL(lua_upvalueid)
SLUA_EXPORT_LUA_SYMBOL(lua_upvaluejoin)
SLUA_EXPORT_LUA_SYMBOL(lua_version)
SLUA_EXPORT_LUA_SYMBOL(lua_xmove)
SLUA_EXPORT_LUA_SYMBOL(lua_yieldk)
SLUA_EXPORT_LUA_SYMBOL(luaL_addlstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_addstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_addvalue)
SLUA_EXPORT_LUA_SYMBOL(luaL_argerror)
SLUA_EXPORT_LUA_SYMBOL(luaL_buffinit)
SLUA_EXPORT_LUA_SYMBOL(luaL_buffinitsize)
SLUA_EXPORT_LUA_SYMBOL(luaL_callmeta)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkany)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkinteger)
SLUA_EXPORT_LUA_SYMBOL(luaL_checklstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_checknumber)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkoption)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkstack)
SLUA_EXPORT_LUA_SYMBOL(luaL_checktype)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkudata)
SLUA_EXPORT_LUA_SYMBOL(luaL_checkversion_)
SLUA_EXPORT_LUA_SYMBOL(luaL_error)
SLUA_EXPORT_LUA_SYMBOL(luaL_execresult)
SLUA_EXPORT_LUA_SYMBOL(luaL_fileresult)
SLUA_EXPORT_LUA_SYMBOL(luaL_getmetafield)
SLUA_EXPORT_LUA_SYMBOL(luaL_getsubtable)
SLUA_EXPORT_LUA_SYMBOL(luaL_gsub)
SLUA_EXPORT_LUA_SYMBOL(luaL_len)
SLUA_EXPORT_LUA_SYMBOL(luaL_loadbufferx)
SLUA_EXPORT_LUA_SYMBOL(luaL_loadfilex)
SLUA_EXPORT_LUA_SYMBOL(luaL_loadstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_newmetatable)
SLUA_EXPORT_LUA_SYMBOL(luaL_newstate)
SLUA_EXPORT_LUA_SYMBOL(luaL_openlibs)
SLUA_EXPORT_LUA_SYMBOL(luaL_optinteger)
SLUA_EXPORT_LUA_SYMBOL(luaL_optlstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_optnumber)
SLUA_EXPORT_LUA_SYMBOL(luaL_prepbuffsize)
SLUA_EXPORT_LUA_SYMBOL(luaL_pushresult)
SLUA_EXPORT_LUA_SYMBOL(luaL_pushresultsize)
SLUA_EXPORT_LUA_SYMBOL(luaL_ref)
SLUA_EXPORT_LUA_SYMBOL(luaL_requiref)
SLUA_EXPORT_LUA_SYMBOL(luaL_setfuncs)
SLUA_EXPORT_LUA_SYMBOL(luaL_setmetatable)
SLUA_EXPORT_LUA_SYMBOL(luaL_testudata)
SLUA_EXPORT_LUA_SYMBOL(luaL_tolstring)
SLUA_EXPORT_LUA_SYMBOL(luaL_traceback)
SLUA_EXPORT_LUA_SYMBOL(luaL_unref)
SLUA_EXPORT_LUA_SYMBOL(luaL_where)
SLUA_EXPORT_LUA_SYMBOL(luaopen_base)
SLUA_EXPORT_LUA_SYMBOL(luaopen_bit32)
SLUA_EXPORT_LUA_SYMBOL(luaopen_coroutine)
SLUA_EXPORT_LUA_SYMBOL(luaopen_debug)
SLUA_EXPORT_LUA_SYMBOL(luaopen_io)
SLUA_EXPORT_LUA_SYMBOL(luaopen_math)
SLUA_EXPORT_LUA_SYMBOL(luaopen_os)
SLUA_EXPORT_LUA_SYMBOL(luaopen_package)
SLUA_EXPORT_LUA_SYMBOL(luaopen_string)
SLUA_EXPORT_LUA_SYMBOL(luaopen_table)
SLUA_EXPORT_LUA_SYMBOL(luaopen_utf8)

#undef SLUA_EXPORT_LUA_SYMBOL
#endif
