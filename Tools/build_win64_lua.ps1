[CmdletBinding()]
param(
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Configuration = "RelWithDebInfo"
$PluginRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\Plugins\slua_unreal")).Path
$BuildRoot = Join-Path $PluginRoot "build_win64"
$BuildOutput = Join-Path $BuildRoot $Configuration
$LibraryRoot = Join-Path $PluginRoot "Library\Win64"
$ManifestPath = Join-Path $LibraryRoot "BUILD-MANIFEST.json"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if ($Clean -and (Test-Path -LiteralPath $BuildRoot))
{
    $BuildDirectory = Get-Item -LiteralPath $BuildRoot
    if ($BuildDirectory.FullName -ne [System.IO.Path]::GetFullPath((Join-Path $PluginRoot "build_win64")) -or
        ($BuildDirectory.Attributes -band [System.IO.FileAttributes]::ReparsePoint))
    {
        throw "Refusing to clean an unexpected or redirected build directory: $BuildRoot"
    }
    Remove-Item -LiteralPath $BuildRoot -Recurse -Force
}

# Let CMake select the installed Visual Studio, or honor CMAKE_GENERATOR.
& cmake -S $PluginRoot -B $BuildRoot -A x64
if ($LASTEXITCODE -ne 0)
{
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}
& cmake --build $BuildRoot --config $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "CMake build failed with exit code $LASTEXITCODE"
}

# Validate the complete build before replacing any published artifact.
$BuiltFiles = [ordered]@{
    "lua.lib" = "lua.lib"
    "lua.pdb" = "lua.pdb"
    "lua.exe" = "lua.exe"
    "lua_cli.pdb" = "lua_cli.pdb"
}
foreach ($Entry in $BuiltFiles.GetEnumerator())
{
    $Source = Join-Path $BuildOutput $Entry.Key
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf))
    {
        throw "Expected CMake output is missing: $Source"
    }
}

$LuaExe = Join-Path $BuildOutput "lua.exe"
$SmokeOutput = (& $LuaExe -e 'assert(_VERSION == "Lua 5.3"); io.write(_VERSION)') 2>&1
if ($LASTEXITCODE -ne 0 -or ($SmokeOutput -join "") -ne "Lua 5.3")
{
    throw "Standalone Lua CLI smoke test failed: $($SmokeOutput -join [Environment]::NewLine)"
}

$CompilerPath = $null
$CmakeGenerator = $null
$CmakeCache = Join-Path $BuildRoot "CMakeCache.txt"
foreach ($Line in Get-Content -LiteralPath $CmakeCache)
{
    if ($Line -match '^CMAKE_CXX_COMPILER:FILEPATH=(.+)$')
    {
        $CompilerPath = $Matches[1]
    }
    if ($Line -match '^CMAKE_GENERATOR:INTERNAL=(.+)$')
    {
        $CmakeGenerator = $Matches[1]
    }
}
if (-not $CompilerPath)
{
    $CompilerConfig = Get-ChildItem -Path (Join-Path $BuildRoot "CMakeFiles\*\CMakeCXXCompiler.cmake") -File |
        Select-Object -First 1
    if ($CompilerConfig)
    {
        foreach ($Line in Get-Content -LiteralPath $CompilerConfig.FullName)
        {
            if ($Line -match '^set\(CMAKE_CXX_COMPILER "([^"]+)"\)')
            {
                $CompilerPath = $Matches[1]
                break
            }
        }
    }
}
$CompilerVersion = if ($CompilerPath -and (Test-Path -LiteralPath $CompilerPath))
{
    (Get-Item -LiteralPath $CompilerPath).VersionInfo.FileVersion
}
else
{
    "$CmakeGenerator (compiler path unavailable in CMake metadata)"
}

$Manifest = [ordered]@{
    schemaVersion = 1
    luaVersion = "5.3.4"
    generator = $CmakeGenerator
    architecture = "x64"
    configuration = $Configuration
    compilerVersion = $CompilerVersion
    compileDefinitions = @("LUA_BUILD_AS_DLL", "LUA_STATIC_LINK")
    cmakeSource = "Plugins/slua_unreal/CMakeLists.txt"
    library = "Plugins/slua_unreal/Library/Win64/lua.lib"
    librarySha256 = (Get-FileHash -LiteralPath (Join-Path $BuildOutput "lua.lib") -Algorithm SHA256).Hash
    cli = "Plugins/slua_unreal/Library/Win64/lua.exe"
    cliSha256 = (Get-FileHash -LiteralPath $LuaExe -Algorithm SHA256).Hash
}
$ManifestJson = $Manifest | ConvertTo-Json -Depth 10

[void](New-Item -ItemType Directory -Path $LibraryRoot -Force)
foreach ($Entry in $BuiltFiles.GetEnumerator())
{
    Copy-Item -LiteralPath (Join-Path $BuildOutput $Entry.Key) -Destination (Join-Path $LibraryRoot $Entry.Value) -Force
}
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson, $Utf8NoBom)

Write-Host "Win64 Lua library SHA256: $($Manifest.librarySha256)"
Write-Host "Standalone CLI smoke test: $SmokeOutput"
