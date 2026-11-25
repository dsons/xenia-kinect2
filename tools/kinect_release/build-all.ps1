# Builds the whole Kinect release from this source folder: Xenia, the Kinect
# runtime and the zip. For-Developers-Build-From-Source.cmd runs it; so does the
# "Kinect release" GitHub Actions workflow.
#
#   powershell -ExecutionPolicy Bypass -File tools\kinect_release\build-all.ps1
#
# Missing tools (Git, Python, Visual Studio Build Tools with C++, Vulkan SDK)
# are installed with winget after asking, unless -NoInstall is given.

param(
  [string]$Version,
  [switch]$NoInstall,
  [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here '..\..')).Path
Set-Location $root

function Step($message) { Write-Host ''; Write-Host "== $message" -ForegroundColor Cyan }

function Update-Path {
  $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
  $user = [Environment]::GetEnvironmentVariable('Path', 'User')
  $env:Path = "$machine;$user"
  $vulkan = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
  if ($vulkan -and -not $env:VULKAN_SDK) { $env:VULKAN_SDK = $vulkan }
}

function Find-VisualStudio {
  $vswhere = Join-Path $root 'tools\vswhere\vswhere.exe'
  $path = & $vswhere -latest -products * -version '[17,)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
  return $path
}

function Test-Python {
  try {
    $out = & python -c "import sys; print(sys.version_info >= (3, 6) and sys.maxsize > 2**32)" 2>$null
    return $LASTEXITCODE -eq 0 -and $out -eq 'True'
  } catch { return $false }
}

function Test-Vulkan {
  if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK 'Bin\spirv-opt.exe'))) { return $true }
  $sdk = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($sdk -and (Test-Path (Join-Path $sdk.FullName 'Bin\spirv-opt.exe'))) {
    $env:VULKAN_SDK = $sdk.FullName
    return $true
  }
  return $false
}

function Install-Package($id, $name, [string[]]$extra) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "$name is missing and winget is not available to install it. Install $name, then run this again."
  }
  Write-Host "Installing $name with winget. Windows may ask for permission."
  & winget install --id $id --exact --source winget --accept-package-agreements --accept-source-agreements @extra
  if ($LASTEXITCODE -ne 0) {
    throw "Installing $name failed (winget exit code $LASTEXITCODE)."
  }
  Update-Path
}

# Prerequisites.
Step 'Checking the tools needed to build'
Update-Path
$missing = @()
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += 'Git' }
if (-not (Test-Python)) { $missing += 'Python 3' }
if (-not (Find-VisualStudio)) { $missing += 'Visual Studio Build Tools (C++)' }
if (-not (Test-Vulkan)) { $missing += 'Vulkan SDK' }

if ($missing.Count -gt 0) {
  Write-Host ('Missing: ' + ($missing -join ', '))
  if ($NoInstall) { throw 'Install the missing tools, then run this again.' }
  if (-not $Yes) {
    Write-Host ''
    Write-Host 'These are free tools from Microsoft, the Python and Git projects and'
    Write-Host 'Khronos (Vulkan). Downloading them can take a while: Visual Studio'
    Write-Host 'Build Tools is several gigabytes.'
    $answer = Read-Host 'Install them now? (Y/N)'
    if ($answer -notmatch '^[Yy]') { throw 'Nothing was installed.' }
  }
  if ($missing -contains 'Git') { Install-Package 'Git.Git' 'Git' }
  if ($missing -contains 'Python 3') { Install-Package 'Python.Python.3.12' 'Python 3' @('--scope', 'machine') }
  if ($missing -contains 'Visual Studio Build Tools (C++)') {
    Install-Package 'Microsoft.VisualStudio.2022.BuildTools' 'Visual Studio Build Tools' @(
      '--override', '--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
  }
  if ($missing -contains 'Vulkan SDK') { Install-Package 'KhronosGroup.VulkanSDK' 'Vulkan SDK' }

  $still = @()
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $still += 'Git' }
  if (-not (Test-Python)) { $still += 'Python 3' }
  if (-not (Find-VisualStudio)) { $still += 'Visual Studio Build Tools (C++)' }
  if (-not (Test-Vulkan)) { $still += 'Vulkan SDK' }
  if ($still.Count -gt 0) {
    throw ('Still missing after installing: ' + ($still -join ', ') + '. Restart the PC, then run this again.')
  }
}
$env:Path = "$(Join-Path $env:VULKAN_SDK 'Bin');$env:Path"
# With several MSVC toolsets installed, an older one can be picked up; use the
# newest.
if (-not $env:XE_VCVARS_VER) {
  $toolsets = @(Get-ChildItem (Join-Path (Find-VisualStudio) 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name })
  if ($toolsets.Count -gt 1) {
    $env:XE_VCVARS_VER = ($toolsets[-1].Name -split '\.')[0..1] -join '.'
    Write-Host "Using MSVC $($env:XE_VCVARS_VER)."
  }
}
Write-Host 'All tools found.'

if (-not (Test-Path (Join-Path $root '.git'))) {
  throw 'This folder is not a Git copy of the source. Run For-Developers-Build-From-Source.cmd from an empty folder instead: it downloads the source for you.'
}

if (-not $Version) {
  $ErrorActionPreference = 'Continue'
  $Version = (& git describe --tags --exact-match 2>$null)
  $described = $LASTEXITCODE -eq 0
  $ErrorActionPreference = 'Stop'
  if (-not $described -or -not $Version) {
    $Version = 'custom-' + (& git rev-parse --short HEAD)
  }
  $Version = $Version -replace '^v', ''
}

function Invoke-Step($exe, [string[]]$arguments) {
  & $exe @arguments
  if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

Step 'Downloading the source of the libraries Xenia uses'
Invoke-Step python @('xenia-build.py', 'setup')

Step 'Building Xenia (this takes a while)'
Invoke-Step python @('xenia-build.py', 'build', '--config=release', '--target=xenia-app')

Step 'Building the Kinect runtime'
Invoke-Step powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  (Join-Path $root 'tools\kinect_runtime\build.ps1'))

Step 'Packaging'
Invoke-Step powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  (Join-Path $here 'package.ps1'), '-Version', $Version)

$release = Join-Path $root 'build\release'
Write-Host ''
Write-Host "Done. Your build is in $release" -ForegroundColor Green
Write-Host "  xenia-kinect2-$Version-windows-x64\        ready to run"
Write-Host "  xenia-kinect2-$Version-windows-x64.zip     the same, zipped"
