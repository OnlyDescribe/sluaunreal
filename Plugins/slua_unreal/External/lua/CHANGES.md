# Local modifications to vendored Lua sources

These files were copied from upstream Lua 5.3 and modified locally to fit the
KeyGame slua_unreal integration. This file documents the diffs from upstream so
future upgrades can re-apply or audit them.

## `luaconf.h`

- **Commit:** `a8bfec2` (initial), updated for LUA_STATIC_LINK ordering fix.
- **Change:** Added a `LUA_STATIC_LINK` branch and reordered the top-level
  `LUA_API` macro chain so that `LUA_STATIC_LINK` is matched **before**
  `LUA_BUILD_AS_DLL`. Final order:
  1. `LUA_STATIC_LINK`  → plain `extern "C"` / `extern` (no dll attributes).
  2. `LUA_BUILD_AS_DLL` + (`LUA_CORE` | `LUA_LIB`) → `__declspec(dllexport)`.
  3. `LUA_BUILD_AS_DLL` (consumer)                 → `__declspec(dllimport)`.
  4. (none)                                        → `extern`.
- **Why the ordering matters:** The CMake build defines **both**
  `LUA_BUILD_AS_DLL` and `LUA_STATIC_LINK` on the `lua` and `lua_cli` targets
  (see `Plugins/slua_unreal/CMakeLists.txt`). With the old ordering the
  inner `LUA_BUILD_AS_DLL` / `LUA_CORE` branch hit first and produced a
  `lua.lib` whose Lua C API symbols carried `__declspec(dllexport)` and a
  `lua_cli.exe` whose references resolved as `__declspec(dllimport)` — the
  latter could not link standalone. Putting `LUA_STATIC_LINK` first makes
  the archive a "normal" static library (plain extern symbols), which
  satisfies both `lua_cli` (resolves locally) and slua_unreal (re-exports
  via `LuaExports.cpp`, see below).
- **slua_unreal module build:** Compiled with `LUA_BUILD_AS_DLL` (public) and
  `LUA_CORE` (private). `LUA_STATIC_LINK` is **not** defined here, so the
  declarations slua_unreal sees in its own TUs are `dllexport` (LUA_CORE
  branch). The actual symbol *definitions* come from lua.lib (plain
  extern), so MSVC will not auto-emit them onto the slua_unreal.dll export
  table; that is what `Source/slua_unreal/Private/LuaExports.cpp` solves
  with `/EXPORT:` linker pragmas (required for EmmyLua attach-to-process).
- **Downstream consumers:** Game modules link only `LUA_BUILD_AS_DLL` (no
  LUA_CORE), so their declarations are `dllimport` and resolve against the
  symbols slua_unreal.dll exports.
- **Coupled definitions** (see `slua_unreal.Build.cs`):
  - Wrapped in `#if UE_4_21_OR_LATER` to use
    `PublicDefinitions.Add("LUA_BUILD_AS_DLL=1")` /
    `PrivateDefinitions.Add("LUA_CORE=1")`; older engines (UE 4.18–4.20)
    fall back to `Definitions.Add(...)`.
  - `LUA_CORE` is kept *private* so that downstream modules importing
    slua_unreal do not redefine it and accidentally flip `LUA_API` to
    `dllexport` on the consumer side.
- **EmmyLua context:** Required to make the EmmyLua VS Code debugger work
  via the attach-to-process workflow, which expects Lua C API symbols to
  be exported from the host module (slua_unreal.dll).

## `lua.cpp`

- **Commit:** `7f173e8`
- **Change:** Verbatim copy of the upstream Lua 5.3 `lua.c` REPL entry point,
  renamed to `lua.cpp` and wrapped with `using namespace NS_SLUA;` so the
  `lua_State` / API symbols resolve to slua's namespaced declarations.
- **Reason:** Needed only by the standalone `lua_cli` executable produced by
  the CMake project under `Plugins/slua_unreal/CMakeLists.txt` for offline
  scripting / wrapper-generation tooling.
- **Scope:** This file is **not** part of the UE module build
  (`slua_unreal.Build.cs` does not pull it in). It only ships through the
  CMake target.

## Maintenance notes

- When pulling a new upstream Lua release, re-apply the `LUA_API` shim in
  `luaconf.h` (with `LUA_STATIC_LINK` listed **first**) and re-vendor
  `lua.cpp` from the new `lua.c`.
- Keep `LUA_CORE` strictly in `PrivateDefinitions` to avoid leaking core-build
  semantics into modules that link against `slua_unreal`.
- The current `Library/Win64/lua.lib` was rebuilt with the CMake target's
  `LUA_BUILD_AS_DLL + LUA_STATIC_LINK` definitions. Under the reordered
  `luaconf.h` chain that produces an archive with **plain extern** symbols,
  which is the desired state. No rebuild required after the reordering fix
  (the binary already matches the new semantic).
- If the `LUA_STATIC_LINK` define is ever removed from CMake, lua.lib will
  go back to `__declspec(dllexport)` symbols and `LuaExports.cpp` becomes
  redundant (but remains harmless).
