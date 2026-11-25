# Assembles the release folder and zip from a Release build of Xenia and the
# Kinect runtime (tools\kinect_runtime\build.ps1).
#
#   powershell -ExecutionPolicy Bypass -File tools\kinect_release\package.ps1 -Version 1.0.0

param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$XeniaBin,
  [string]$KinectDir,
  [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here '..\..')).Path
if (-not $XeniaBin) { $XeniaBin = Join-Path $root 'build\bin\Windows\Release' }
if (-not $KinectDir) { $KinectDir = Join-Path $root 'build\kinect' }
if (-not $OutDir) { $OutDir = Join-Path $root 'build\release' }
$name = "xenia-nui-$Version-windows-x64"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$dist = Join-Path $OutDir $name
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory $dist | Out-Null

$exe = Join-Path $XeniaBin 'xenia_canary.exe'
if (-not (Test-Path $exe)) { throw "xenia_canary.exe not found in $XeniaBin" }
foreach ($dll in 'xenia_kinect.dll', 'freenect.dll', 'freenect2.dll', 'libusb-1.0.dll', 'turbojpeg.dll', 'vcomp140.dll') {
  if (-not (Test-Path (Join-Path $KinectDir $dll))) { throw "$dll not found in $KinectDir" }
}

Copy-Item $exe $dist
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dist 'LICENSE.txt')
Copy-Item (Join-Path $here 'README.txt') $dist
Copy-Item (Join-Path $here 'config') (Join-Path $dist 'config') -Recurse
Copy-Item (Join-Path $here 'patches') (Join-Path $dist 'patches') -Recurse
# Keeps the configuration, profiles and saves next to the executable.
Set-Content -Path (Join-Path $dist 'portable.txt') -Value '' -NoNewline
Set-Content -Path (Join-Path $dist 'xenia-canary.config.toml') -Encoding ASCII -Value @(
  '[Profiles]',
  'default_profile_gamertag = "Player"'
)

$kinect = Join-Path $dist 'kinect'
Copy-Item $KinectDir $kinect -Recurse
Copy-Item (Join-Path $here 'Setup-Kinect.cmd'), (Join-Path $here 'Restore-Kinect.cmd'), (Join-Path $here 'Setup-Kinect.ps1') $kinect
Copy-Item (Join-Path $here 'SOURCES.txt') (Join-Path $kinect 'licenses')

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $dist -DestinationPath $zip
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash
Set-Content -Path "$zip.sha256" -Encoding ASCII -Value "$hash  $name.zip"
Get-ChildItem $dist -Recurse -File | ForEach-Object { $_.FullName.Substring($dist.Length + 1) }
"$zip"
"SHA-256 $hash"
