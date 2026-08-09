[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EngineRoot,

    [string]$ProjectRoot = (Join-Path $PSScriptRoot "..\..\.."),

    [switch]$VerifyDeterministic
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ExistingDirectory([string]$Path, [string]$Label)
{
    $Resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $Resolved -or -not (Test-Path -LiteralPath $Resolved.Path -PathType Container))
    {
        throw "$Label directory does not exist: $Path"
    }
    return $Resolved.Path
}

function Convert-ToToolPath([string]$Path)
{
    return $Path.Replace("\", "/")
}

function Get-WindowsToolchainIncludes
{
    $Includes = [System.Collections.Generic.List[string]]::new()
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $VsWhere)
    {
        $VsRoot = (& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
        if ($LASTEXITCODE -eq 0 -and $VsRoot)
        {
            $MsvcRoot = Join-Path $VsRoot "VC\Tools\MSVC"
            $MsvcVersion = Get-ChildItem -LiteralPath $MsvcRoot -Directory -ErrorAction SilentlyContinue |
                Sort-Object { [version]$_.Name } -Descending |
                Select-Object -First 1
            if ($MsvcVersion)
            {
                foreach ($RelativePath in @("include", "atlmfc\include"))
                {
                    $Candidate = Join-Path $MsvcVersion.FullName $RelativePath
                    if (Test-Path -LiteralPath $Candidate)
                    {
                        $Includes.Add((Convert-ToToolPath $Candidate))
                    }
                }
            }
        }
    }

    $WindowsKitRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
    $WindowsKitVersion = Get-ChildItem -LiteralPath $WindowsKitRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if ($WindowsKitVersion)
    {
        foreach ($RelativePath in @("ucrt", "shared", "um", "winrt"))
        {
            $Candidate = Join-Path $WindowsKitVersion.FullName $RelativePath
            if (Test-Path -LiteralPath $Candidate)
            {
                $Includes.Add((Convert-ToToolPath $Candidate))
            }
        }
    }

    if ($Includes.Count -eq 0)
    {
        throw "No MSVC or Windows SDK include directories were discovered."
    }
    return $Includes
}

$EngineRoot = Resolve-ExistingDirectory $EngineRoot "Engine root"
$ProjectRoot = Resolve-ExistingDirectory $ProjectRoot "Project root"
$SubmoduleRoot = Resolve-ExistingDirectory (Join-Path $PSScriptRoot "..") "slua submodule root"
$ProjectFile = Join-Path $ProjectRoot "Key.uproject"
$ProjectVcxproj = Join-Path $ProjectRoot "Intermediate\ProjectFiles\UE5.vcxproj"
if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf))
{
    throw "Key.uproject was not found under project root: $ProjectRoot"
}
if (-not (Test-Path -LiteralPath $ProjectVcxproj -PathType Leaf))
{
    throw "UE5.vcxproj is missing. Generate project files before running the wrapper generator: $ProjectVcxproj"
}
if (-not (Test-Path -LiteralPath (Join-Path $EngineRoot "Engine\Source") -PathType Container))
{
    throw "The supplied engine root has no Engine/Source directory: $EngineRoot"
}

$ConfigTemplatePath = Join-Path $PSScriptRoot "config.json"
$WrapperExe = Join-Path $PSScriptRoot "lua-wrapper.exe"
$PrivateOutput = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Source\slua_unreal\Private"
$PublicOutput = Join-Path $SubmoduleRoot "Plugins\slua_unreal\Source\slua_unreal\Public"
$RawInc = Join-Path $PrivateOutput "LuaWrapper.inc"
$RawHead = Join-Path $PublicOutput "LuaWrapperHead.inc"
$VersionedInc = Join-Path $PrivateOutput "LuaWrapper5.7.inc"
$VersionedHead = Join-Path $PublicOutput "LuaWrapper5.7Head.inc"
$ManifestPath = Join-Path $PSScriptRoot "WrapperGenerationManifest.json"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($RequiredFile in @($ConfigTemplatePath, $WrapperExe))
{
    if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf))
    {
        throw "Required wrapper tool file is missing: $RequiredFile"
    }
}

$ToolchainIncludes = (Get-WindowsToolchainIncludes) -join ";"
$LastPostprocessRemovedBytes = 0

function Invoke-WrapperGeneration
{
    $TemporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("slua-wrapper-" + [guid]::NewGuid().ToString("N"))
    [void](New-Item -ItemType Directory -Path $TemporaryDirectory)
    try
    {
        $Config = Get-Content -LiteralPath $ConfigTemplatePath -Raw | ConvertFrom-Json
        $Config.win.solution_dir = Convert-ToToolPath $SubmoduleRoot
        $Config.win.ue4_dir = Convert-ToToolPath $EngineRoot
        $Config.win.ue_vcproj = Convert-ToToolPath $ProjectVcxproj
        $Config.win.include_path = $Config.win.include_path.Replace("{toolchain_includes}", $ToolchainIncludes)
        $TemporaryConfig = Join-Path $TemporaryDirectory "config.json"
        [System.IO.File]::WriteAllText($TemporaryConfig, ($Config | ConvertTo-Json -Depth 100), $Utf8NoBom)

        # lua-wrapper resolves config.json beside its executable rather than
        # from the process working directory, so isolate the legacy binary and
        # its runtime dependencies in the temporary directory.
        foreach ($ToolFile in @("lua-wrapper.exe", "dot-clang.dll", "libclang.dll", "Newtonsoft.Json.dll"))
        {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $ToolFile) -Destination $TemporaryDirectory
        }
        $TemporaryWrapperExe = Join-Path $TemporaryDirectory "lua-wrapper.exe"

        # These are generator intermediates, not checked-in build inputs.
        Remove-Item -LiteralPath $RawInc, $RawHead -Force -ErrorAction SilentlyContinue
        Push-Location $TemporaryDirectory
        try
        {
            & $TemporaryWrapperExe
            if ($LASTEXITCODE -ne 0)
            {
                throw "lua-wrapper.exe failed with exit code $LASTEXITCODE"
            }
        }
        finally
        {
            Pop-Location
        }

        if (-not (Test-Path -LiteralPath $RawInc -PathType Leaf) -or -not (Test-Path -LiteralPath $RawHead -PathType Leaf))
        {
            throw "lua-wrapper.exe did not produce LuaWrapper.inc and LuaWrapperHead.inc."
        }

        $Content = [System.IO.File]::ReadAllText($RawInc)
        $BeforeLength = $Content.Length
        $Content = [regex]::Replace(
            $Content,
            "\r?\n        static int get_CURRENT_FILE_ID_\d+_GENERATED_BODY\(lua_State\* L\) \{[^}]+\}\r?\n",
            "`n")
        $Content = [regex]::Replace(
            $Content,
            "\r?\n        static int set_CURRENT_FILE_ID_\d+_GENERATED_BODY\(lua_State\* L\) \{[^}]+\}\r?\n",
            "`n")
        $Content = [regex]::Replace(
            $Content,
            "\r?\n\s+LuaObject::addField\(L, `"CURRENT_FILE_ID_\d+_GENERATED_BODY`"[^\r\n]+\r?\n",
            "`n")

        [System.IO.File]::WriteAllText($VersionedInc, $Content, $Utf8NoBom)
        [System.IO.File]::WriteAllBytes($VersionedHead, [System.IO.File]::ReadAllBytes($RawHead))
        $script:LastPostprocessRemovedBytes = $BeforeLength - $Content.Length

        Remove-Item -LiteralPath $RawInc, $RawHead -Force
    }
    finally
    {
        Remove-Item -LiteralPath $TemporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Invoke-WrapperGeneration
$FirstIncHash = (Get-FileHash -LiteralPath $VersionedInc -Algorithm SHA256).Hash
$FirstHeadHash = (Get-FileHash -LiteralPath $VersionedHead -Algorithm SHA256).Hash

if ($VerifyDeterministic)
{
    Invoke-WrapperGeneration
    $SecondIncHash = (Get-FileHash -LiteralPath $VersionedInc -Algorithm SHA256).Hash
    $SecondHeadHash = (Get-FileHash -LiteralPath $VersionedHead -Algorithm SHA256).Hash
    if ($FirstIncHash -ne $SecondIncHash -or $FirstHeadHash -ne $SecondHeadHash)
    {
        throw "Consecutive wrapper generations produced different output hashes."
    }
}

$EngineBuildVersion = Get-Content -LiteralPath (Join-Path $EngineRoot "Engine\Build\Build.version") -Raw | ConvertFrom-Json
$Manifest = [ordered]@{
    schemaVersion = 1
    engineVersion = "$($EngineBuildVersion.MajorVersion).$($EngineBuildVersion.MinorVersion).$($EngineBuildVersion.PatchVersion)"
    generator = "Tools/lua-wrapper.exe"
    generatorSha256 = (Get-FileHash -LiteralPath $WrapperExe -Algorithm SHA256).Hash
    configTemplate = "Tools/config.json"
    configTemplateSha256 = (Get-FileHash -LiteralPath $ConfigTemplatePath -Algorithm SHA256).Hash
    postprocess = "remove CURRENT_FILE_ID_*_GENERATED_BODY pseudo-fields"
    postprocessRemovedBytes = $LastPostprocessRemovedBytes
    outputs = [ordered]@{
        private = "Plugins/slua_unreal/Source/slua_unreal/Private/LuaWrapper5.7.inc"
        privateSha256 = (Get-FileHash -LiteralPath $VersionedInc -Algorithm SHA256).Hash
        public = "Plugins/slua_unreal/Source/slua_unreal/Public/LuaWrapper5.7Head.inc"
        publicSha256 = (Get-FileHash -LiteralPath $VersionedHead -Algorithm SHA256).Hash
    }
}
[System.IO.File]::WriteAllText($ManifestPath, ($Manifest | ConvertTo-Json -Depth 10), $Utf8NoBom)

Write-Host "Generated UE $($Manifest.engineVersion) wrapper."
Write-Host "LuaWrapper5.7.inc SHA256: $($Manifest.outputs.privateSha256)"
Write-Host "LuaWrapper5.7Head.inc SHA256: $($Manifest.outputs.publicSha256)"
if ($VerifyDeterministic)
{
    Write-Host "Determinism check: two consecutive generations matched."
}
