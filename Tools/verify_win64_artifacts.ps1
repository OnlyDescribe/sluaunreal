[CmdletBinding()]
param(
    [string]$SluaDll,
    [string]$LuaExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SubmoduleRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if (-not $SluaDll)
{
    $SluaDll = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Binaries\Win64\UnrealEditor-slua_unreal.dll"
}
if (-not $LuaExe)
{
    $LuaExe = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Library\Win64\lua.exe"
}
$LibraryManifestPath = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Library\Win64\BUILD-MANIFEST.json"
$ExportManifestPath = Join-Path $PSScriptRoot "Win64LuaExports.txt"
$LuaExportsSource = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Source\slua_unreal\Private\LuaExports.cpp"
$LuaLibrary = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Library\Win64\lua.lib"

foreach ($RequiredFile in @($SluaDll, $LuaExe, $LibraryManifestPath, $ExportManifestPath, $LuaExportsSource, $LuaLibrary))
{
    if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf))
    {
        throw "Required artifact is missing: $RequiredFile"
    }
}

$LibraryManifest = Get-Content -LiteralPath $LibraryManifestPath -Raw | ConvertFrom-Json
$ActualLibraryHash = (Get-FileHash -LiteralPath $LuaLibrary -Algorithm SHA256).Hash
if ($ActualLibraryHash -ne $LibraryManifest.librarySha256)
{
    throw "lua.lib hash does not match BUILD-MANIFEST.json. Expected $($LibraryManifest.librarySha256), got $ActualLibraryHash"
}

$ExpectedExports = @(Get-Content -LiteralPath $ExportManifestPath |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith("#") } |
    Sort-Object -Unique)
$SourceExports = @(Get-Content -LiteralPath $LuaExportsSource |
    ForEach-Object { if ($_ -match '^SLUA_EXPORT_LUA_SYMBOL\(([^)]+)\)') { $Matches[1] } } |
    Sort-Object -Unique)
$SourceDelta = @(Compare-Object -ReferenceObject $ExpectedExports -DifferenceObject $SourceExports)
if ($SourceDelta.Count -ne 0)
{
    throw "Win64LuaExports.txt and LuaExports.cpp differ: $($SourceDelta | Out-String)"
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf))
{
    throw "vswhere.exe was not found: $VsWhere"
}
$VsRoot = (& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $VsRoot)
{
    throw "No Visual Studio C++ toolchain installation was found."
}
$MsvcVersion = Get-ChildItem -LiteralPath (Join-Path $VsRoot "VC\Tools\MSVC") -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
$Dumpbin = Join-Path $MsvcVersion.FullName "bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path -LiteralPath $Dumpbin -PathType Leaf))
{
    throw "dumpbin.exe was not found: $Dumpbin"
}

$ActualExports = @(& $Dumpbin /nologo /exports $SluaDll |
    ForEach-Object {
        if ($_ -match '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(lua(?:L|open)?_[A-Za-z0-9_]+)(?:\s|$)')
        {
            $Matches[1]
        }
    } |
    Sort-Object -Unique)
$ExportDelta = @(Compare-Object -ReferenceObject $ExpectedExports -DifferenceObject $ActualExports)
if ($ExportDelta.Count -ne 0)
{
    throw "DLL Lua exports do not match Win64LuaExports.txt: $($ExportDelta | Out-String)"
}

$SmokeOutput = (& $LuaExe -e 'assert(_VERSION == "Lua 5.3"); io.write(_VERSION)') 2>&1
if ($LASTEXITCODE -ne 0 -or ($SmokeOutput -join "") -ne "Lua 5.3")
{
    throw "Standalone Lua CLI smoke test failed: $($SmokeOutput -join [Environment]::NewLine)"
}

Write-Host "lua.lib manifest: verified ($ActualLibraryHash)"
Write-Host "slua_unreal.dll exports: verified ($($ActualExports.Count) Lua symbols)"
Write-Host "Standalone Lua CLI smoke test: $SmokeOutput"
