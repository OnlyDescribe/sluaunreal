# Vendored Lua changes

The runtime is Lua 5.3.4 compiled as C++ inside the `NS_SLUA` namespace.
Keep the upstream Lua and LuaSocket notices in the repository's `LICENSE.TXT`.
The source archive, module declarations and CLI must use the same ABI settings.

## Win64 API ownership

`luaconf.h` gives `LUA_STATIC_LINK` precedence over `LUA_BUILD_AS_DLL`.
The CMake archive and CLI define both, so their Lua C API symbols use plain
`extern "C"` declarations. In a modular UE Win64 target, `slua_unreal` defines
`LUA_BUILD_AS_DLL` publicly and `LUA_CORE` privately; consumers import the API
from the plugin DLL. `Private/LuaExports.cpp` explicitly exports the symbols
listed in `Tools/Win64LuaExports.txt`. Monolithic Win64 targets instead define
`LUA_STATIC_LINK` and link the archive into the executable.

The `slua_lua` external module is a private dependency of `slua_unreal`.
Making it a public dependency would allow consumers to acquire another copy
of the Lua implementation. `LUA_CORE` must also remain private.

## C++ error unwinding

Public Lua APIs which can propagate a Lua error or yield use `LUA_THROWS`
(`noexcept(false)` in C++, empty in C). Declarations and definitions carry
the same specification. This keeps the Win64 C export names while preventing
MSVC `/EHsc` from assuming that these C-linkage calls cannot unwind native
objects. The C++ VM uses the existing exception path in `ldo.cpp`; keep the
UE module's exception support enabled. Non-raising APIs retain their original
declarations rather than receiving a blanket annotation.

## Standalone CLI

`lua.cpp` is the upstream REPL entry point adapted to the namespaced APIs.
Only the CMake `lua_cli` target compiles it; the UE runtime module does not.
The CLI executable is generated locally and excluded from Git.

## Rebuilding and validation

Use `Tools/build_win64_lua.ps1` and review `Library/Win64/BUILD-MANIFEST.json`
with the archive. The manifest records the actual configuration, definitions,
compiler version and archive/CLI hashes. `Tools/verify_win64_artifacts.ps1`
checks those hashes, DLL exports and the CLI smoke test after a host build.
Build redistributed artifacts in a standalone plugin checkout so debug records
do not embed a consuming game's source path. The checked-in archive does not
make other platform libraries or native debugger attachment verified.
