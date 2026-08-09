[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "RelWithDebInfo",

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$PluginRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\Plugins\slua_unreal")).Path
$BuildRoot = Join-Path $PluginRoot "build_win64"
$LibraryRoot = Join-Path $PluginRoot "Library\Win64"
$ManifestPath = Join-Path $LibraryRoot "BUILD-MANIFEST.json"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if ($Clean -and (Test-Path -LiteralPath $BuildRoot))
{
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}

& cmake -S $PluginRoot -B $BuildRoot -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0)
{
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}
& cmake --build $BuildRoot --config $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "CMake build failed with exit code $LASTEXITCODE"
}

[void](New-Item -ItemType Directory -Path $LibraryRoot -Force)
$BuiltFiles = [ordered]@{
    "lua.lib" = "lua.lib"
    "lua.pdb" = "lua.pdb"
    "lua.exe" = "lua.exe"
    "lua_cli.pdb" = "lua_cli.pdb"
}
foreach ($Entry in $BuiltFiles.GetEnumerator())
{
    $Source = Join-Path (Join-Path $BuildRoot $Configuration) $Entry.Key
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf))
    {
        throw "Expected CMake output is missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $LibraryRoot $Entry.Value) -Force
}

$LuaExe = Join-Path $LibraryRoot "lua.exe"
$SmokeOutput = (& $LuaExe -e 'assert(_VERSION == "Lua 5.3"); io.write(_VERSION)') 2>&1
if ($LASTEXITCODE -ne 0 -or ($SmokeOutput -join "") -ne "Lua 5.3")
{
    throw "Standalone Lua CLI smoke test failed: $($SmokeOutput -join [Environment]::NewLine)"
}

$CompilerPath = $null
$CmakeCache = Join-Path $BuildRoot "CMakeCache.txt"
foreach ($Line in Get-Content -LiteralPath $CmakeCache)
{
    if ($Line -match '^CMAKE_CXX_COMPILER:FILEPATH=(.+)$')
    {
        $CompilerPath = $Matches[1]
        break
    }
}
$CompilerVersion = if ($CompilerPath -and (Test-Path -LiteralPath $CompilerPath))
{
    (Get-Item -LiteralPath $CompilerPath).VersionInfo.FileVersion
}
else
{
    "Visual Studio 17 2022 (compiler path unavailable in CMake cache)"
}

$Manifest = [ordered]@{
    schemaVersion = 1
    luaVersion = "5.3.4"
    generator = "Visual Studio 17 2022"
    architecture = "x64"
    configuration = $Configuration
    compilerVersion = $CompilerVersion
    compileDefinitions = @("LUA_BUILD_AS_DLL", "LUA_STATIC_LINK")
    cmakeSource = "Plugins/slua_unreal/CMakeLists.txt"
    library = "Plugins/slua_unreal/Library/Win64/lua.lib"
    librarySha256 = (Get-FileHash -LiteralPath (Join-Path $LibraryRoot "lua.lib") -Algorithm SHA256).Hash
    cli = "Plugins/slua_unreal/Library/Win64/lua.exe"
    cliSha256 = (Get-FileHash -LiteralPath $LuaExe -Algorithm SHA256).Hash
}
[System.IO.File]::WriteAllText($ManifestPath, ($Manifest | ConvertTo-Json -Depth 10), $Utf8NoBom)

Write-Host "Win64 Lua library SHA256: $($Manifest.librarySha256)"
Write-Host "Standalone CLI smoke test: $SmokeOutput"
