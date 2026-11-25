@echo off
rem Builds Xenia with Kinect from its source code.
rem Put this file in an empty folder and double-click it. The source goes to
rem C:\xenia-kinect2-build and the finished zip is copied next to this file.
rem Run from a copy of the source, it builds that copy instead.
setlocal
set "XN_SELF=%~f0"
set "XN_HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s = [IO.File]::ReadAllText($env:XN_SELF); Invoke-Expression $s.Substring($s.IndexOf('#' + 'BEGIN-POWERSHELL'))"
set "XN_EXIT=%ERRORLEVEL%"
echo.
if not "%XN_NOPAUSE%"=="1" pause
exit /b %XN_EXIT%

#BEGIN-POWERSHELL
$ErrorActionPreference = 'Stop'
$here = $env:XN_HERE.TrimEnd('\')
$repo = 'https://github.com/dsons/xenia-kinect2.git'
try {
  Write-Host 'Xenia with Kinect: build from source' -ForegroundColor Cyan
  $script = Join-Path $here 'tools\kinect_release\build-all.ps1'
  $inPlace = Test-Path $script
  if ($inPlace) {
    $src = $here
  } else {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
      [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
      Write-Host 'Git is needed to download the source.'
      if ((Read-Host 'Install Git now? (Y/N)') -notmatch '^[Yy]') { throw 'Git was not installed.' }
      & winget install --id Git.Git --exact --source winget --accept-package-agreements --accept-source-agreements
      $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
        [Environment]::GetEnvironmentVariable('Path', 'User')
      if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git could not be installed.' }
    }
    # A short path without spaces: some build tools do not handle spaces.
    $src = Join-Path $env:SystemDrive 'xenia-kinect2-build'
    if (Test-Path (Join-Path $src '.git')) {
      Write-Host "Updating the source in $src"
      & git -C $src pull --ff-only
    } else {
      Write-Host "Downloading the source to $src"
      & git clone $repo $src
    }
    if ($LASTEXITCODE -ne 0) { throw 'Downloading the source failed.' }
    $script = Join-Path $src 'tools\kinect_release\build-all.ps1'
  }
  & powershell -NoProfile -ExecutionPolicy Bypass -File $script
  if ($LASTEXITCODE -ne 0) { throw 'The build failed. The messages above say why.' }
  if (-not $inPlace) {
    $zip = Get-ChildItem (Join-Path $src 'build\release') -Filter 'xenia-kinect2-*.zip' |
      Sort-Object LastWriteTime | Select-Object -Last 1
    Copy-Item $zip.FullName $here -Force
    Write-Host ''
    Write-Host "Copied $($zip.Name) to $here" -ForegroundColor Green
  }
  exit 0
} catch {
  Write-Host ''
  Write-Host $_.Exception.Message -ForegroundColor Red
  exit 1
}
