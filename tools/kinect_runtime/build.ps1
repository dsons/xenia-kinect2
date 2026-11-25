# Builds the Kinect runtime that Xenia loads from <xenia>\kinect\:
#   xenia_kinect.dll    this folder's C API
#   freenect.dll        libfreenect (Kinect for Xbox 360)
#   freenect2.dll       libfreenect2 (Kinect for Xbox One; patches\ applied)
#   libusb-1.0.dll      libusb 1.0.30
#   turbojpeg.dll       libjpeg-turbo 3.2.0
#   vcomp140.dll and the Visual C++ runtime DLLs
#
# Requires Visual Studio 2022 or later (or its Build Tools) with the C++
# workload, and Git. Downloads are checked against the SHA-256 hashes below; libfreenect is
# cloned at a pinned commit, which Git verifies.
#
#   powershell -ExecutionPolicy Bypass -File tools\kinect_runtime\build.ps1

param(
  [string]$OutDir,
  [string]$WorkDir
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $here '..\..\build\kinect' }
if (-not $WorkDir) { $WorkDir = Join-Path $here '..\..\build\kinect_runtime' }
$ProgressPreference = 'SilentlyContinue'

$libfreenectUrl = 'https://github.com/OpenKinect/libfreenect.git'
$libfreenectCommit = '09a1f098040d00e6070c18174904547ec31d2774'

$deps = @(
  @{ Name = 'libfreenect2.zip'
     Url = 'https://github.com/OpenKinect/libfreenect2/archive/fd64c5d9b214df6f6a55b4419357e51083f15d93.zip'
     Sha256 = '2D65122C3A73BFEC3523E16F8D9B49F82ECF618E3364DF4EC293DE0528BB4A68' },
  @{ Name = 'libusb-1.0.30.7z'
     Url = 'https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.7z'
     Sha256 = '7FB1DFEC805B97983763D7D0AE244320DA12ADD1003D4249C96CC4D586398C79' },
  @{ Name = 'libusb-1.0.30.tar.bz2'
     Url = 'https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2'
     Sha256 = 'FEA36F34F9156400209595E300840767AB1A385EDE1DC7EE893015AEA9C6DBAF' },
  @{ Name = 'libjpeg-turbo-3.2.0-vc-x64.exe'
     Url = 'https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0-vc-x64.exe'
     Sha256 = '662761D8BA8DAE04AEC74023EBAECEB856C2B56B9B59CFD180759D26300DDA42' }
)

function Step($message) { Write-Host "== $message" }

function Invoke-Checked {
  param([string]$Exe, [string[]]$Arguments)
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
}

New-Item -ItemType Directory -Force $WorkDir, $OutDir | Out-Null
$WorkDir = (Resolve-Path $WorkDir).Path
$OutDir = (Resolve-Path $OutDir).Path
$downloads = Join-Path $WorkDir 'downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null

# Toolchain.
$vswhere = Join-Path $here '..\vswhere\vswhere.exe'
if (-not (Test-Path $vswhere)) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
}
$vs = & $vswhere -latest -products * -version '[17,)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 or later with the C++ workload was not found' }
$vsMajor = [int](& $vswhere -path $vs -property installationVersion).Split('.')[0]
$vsYears = @{ 17 = '2022'; 18 = '2026' }
$vsYear = $vsYears[$vsMajor]
if (-not $vsYear) { throw "Visual Studio $vsMajor is not supported by this script yet" }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$git = (Get-Command git -ErrorAction Stop).Source
# CMAKE_POLICY_VERSION_MINIMUM: CMake 4 refuses libfreenect's and libfreenect2's
# old cmake_minimum_required otherwise. Older CMake ignores it.
$generator = @('-G', "Visual Studio $vsMajor $vsYear", '-A', 'x64',
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5')

# Downloads.
foreach ($dep in $deps) {
  $path = Join-Path $downloads $dep.Name
  if (-not (Test-Path $path)) {
    Step "download $($dep.Name)"
    Invoke-WebRequest -Uri $dep.Url -OutFile "$path.part" -UseBasicParsing
    Move-Item "$path.part" $path
  }
  $hash = (Get-FileHash -Algorithm SHA256 $path).Hash
  if ($hash -ne $dep.Sha256) {
    Remove-Item $path
    throw "$($dep.Name): SHA-256 $hash does not match $($dep.Sha256)"
  }
}

# libusb.
$libusb = Join-Path $WorkDir 'libusb'
if (-not (Test-Path $libusb)) {
  Step 'extract libusb'
  New-Item -ItemType Directory $libusb | Out-Null
  Invoke-Checked tar @('-xf', (Join-Path $downloads 'libusb-1.0.30.7z'), '-C', $libusb)
}
$libusbHeader = Get-ChildItem $libusb -Recurse -Filter libusb.h | Select-Object -First 1
$libusbDir = Join-Path $libusb 'VS2022\MS64\dll'
if (-not $libusbHeader -or -not (Test-Path "$libusbDir\libusb-1.0.lib")) {
  throw 'libusb package layout not recognised'
}

# libjpeg-turbo ships as an NSIS installer. 7-Zip unpacks it without
# administrator rights; otherwise the installer runs silently (it may ask for
# them). /D must be the installer's last argument.
$jpeg = Join-Path $WorkDir 'libjpeg-turbo'
if (-not (Test-Path "$jpeg\lib\turbojpeg.lib")) {
  $installer = Join-Path $downloads 'libjpeg-turbo-3.2.0-vc-x64.exe'
  $sevenZip = (Get-Command 7z -ErrorAction SilentlyContinue).Source
  if (-not $sevenZip -and (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) {
    $sevenZip = "$env:ProgramFiles\7-Zip\7z.exe"
  }
  if ($sevenZip) {
    Step 'unpack libjpeg-turbo'
    Invoke-Checked $sevenZip @('x', '-y', "-o$jpeg", $installer) | Out-Null
  } else {
    Step 'install libjpeg-turbo into the work folder'
    Start-Process -Wait -FilePath $installer -ArgumentList '/S', "/D=$jpeg"
  }
  if (-not (Test-Path "$jpeg\lib\turbojpeg.lib")) { throw 'libjpeg-turbo was not unpacked' }
}

# libfreenect2 source with patches.
$src = Join-Path $WorkDir 'libfreenect2'
if (-not (Test-Path $src)) {
  Step 'extract and patch libfreenect2'
  $tmp = Join-Path $WorkDir 'libfreenect2.tmp'
  Expand-Archive (Join-Path $downloads 'libfreenect2.zip') $tmp
  Move-Item (Get-ChildItem $tmp | Select-Object -First 1).FullName $src
  Remove-Item $tmp
  Push-Location $src
  try {
    Invoke-Checked $git @('init', '-q')
    foreach ($patch in Get-ChildItem (Join-Path $here 'patches') -Filter *.patch | Sort-Object Name) {
      Invoke-Checked $git @('apply', '--whitespace=nowarn', $patch.FullName)
    }
  } finally {
    Pop-Location
  }
}

# CMake wants forward slashes in paths it writes into its own scripts.
function CMakePath([string]$path) { $path -replace '\\', '/' }

# libfreenect2 (CPU depth pipeline with OpenMP; no OpenCL, CUDA or OpenGL).
$prefix = Join-Path $WorkDir 'libfreenect2-install'
if (-not (Test-Path "$prefix\lib\freenect2.lib")) {
  Step 'build libfreenect2'
  $build = Join-Path $WorkDir 'libfreenect2-build'
  Invoke-Checked $cmake (@('-S', $src, '-B', $build) + $generator + @(
    '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /openmp',
    '-DENABLE_OPENCL=OFF', '-DENABLE_CUDA=OFF', '-DENABLE_OPENGL=OFF',
    '-DBUILD_EXAMPLES=OFF', '-DBUILD_OPENNI2_DRIVER=OFF',
    "-DLibUSB_INCLUDE_DIRS=$(CMakePath $libusbHeader.DirectoryName)",
    "-DLibUSB_LIBRARIES=$(CMakePath "$libusbDir\libusb-1.0.lib")",
    "-DLibUSB_DLL=$(CMakePath "$libusbDir\libusb-1.0.dll")",
    "-DTurboJPEG_INCLUDE_DIRS=$(CMakePath "$jpeg\include")",
    "-DTurboJPEG_LIBRARIES=$(CMakePath "$jpeg\lib\turbojpeg.lib")",
    "-DTurboJPEG_DLL=$(CMakePath "$jpeg\bin\turbojpeg.dll")",
    "-DCMAKE_INSTALL_PREFIX=$(CMakePath $prefix)"))
  Invoke-Checked $cmake @('--build', $build, '--config', 'Release', '--parallel')
  Invoke-Checked $cmake @('--install', $build, '--config', 'Release')
}

# libfreenect (Kinect for Xbox 360). Only the shared library is built: with
# MSVC its static library has the same file name as the DLL's import library.
$src1 = Join-Path $WorkDir 'libfreenect'
if (-not (Test-Path "$src1\.git")) {
  Step 'clone libfreenect'
  Invoke-Checked $git @('clone', '-q', $libfreenectUrl, $src1)
}
Invoke-Checked $git @('-C', $src1, 'checkout', '-q', '--detach', $libfreenectCommit)
if ((& $git -C $src1 rev-parse HEAD) -ne $libfreenectCommit) { throw 'libfreenect is not at the pinned commit' }
$prefix1 = Join-Path $WorkDir 'libfreenect-install'
if (-not (Test-Path "$prefix1\bin\freenect.dll")) {
  Step 'build libfreenect'
  $build1 = Join-Path $WorkDir 'libfreenect-build'
  Invoke-Checked $cmake (@('-S', $src1, '-B', $build1) + $generator + @(
    '-DBUILD_EXAMPLES=OFF', '-DBUILD_FAKENECT=OFF', '-DBUILD_C_SYNC=OFF',
    '-DBUILD_CPP=OFF', '-DBUILD_REDIST_PACKAGE=ON',
    "-DLIBUSB_1_INCLUDE_DIR=$(CMakePath $libusbHeader.DirectoryName)",
    "-DLIBUSB_1_LIBRARY=$(CMakePath "$libusbDir\libusb-1.0.lib")",
    "-DCMAKE_INSTALL_PREFIX=$(CMakePath $prefix1)"))
  Invoke-Checked $cmake @('--build', $build1, '--config', 'Release', '--target', 'freenect', '--parallel')
  $dll = Get-ChildItem $build1 -Recurse -Filter freenect.dll | Select-Object -First 1
  $lib = Get-ChildItem $build1 -Recurse -Filter freenect.lib | Select-Object -First 1
  if (-not $dll -or -not $lib) { throw 'libfreenect did not build freenect.dll' }
  New-Item -ItemType Directory -Force "$prefix1\bin", "$prefix1\lib", "$prefix1\include\libfreenect" | Out-Null
  Copy-Item -Force $dll.FullName "$prefix1\bin"
  Copy-Item -Force $lib.FullName "$prefix1\lib"
  Copy-Item -Force "$src1\include\*.h" "$prefix1\include\libfreenect"
}

# xenia_kinect.dll.
Step 'build xenia_kinect'
$runtimeBuild = Join-Path $WorkDir 'xenia_kinect-build'
Invoke-Checked $cmake (@('-S', $here, '-B', $runtimeBuild) + $generator + @(
  "-DFREENECT_ROOT=$(CMakePath $prefix1)",
  "-DFREENECT2_ROOT=$(CMakePath $prefix)",
  "-DLIBUSB_INCLUDE_DIR=$(CMakePath $libusbHeader.DirectoryName)",
  "-DLIBUSB_LIBRARY=$(CMakePath "$libusbDir\libusb-1.0.lib")"))
Invoke-Checked $cmake @('--build', $runtimeBuild, '--config', 'Release', '--parallel')

# Collect.
Step "collect into $OutDir"
Copy-Item -Force "$runtimeBuild\Release\xenia_kinect.dll" $OutDir
Copy-Item -Force "$prefix1\bin\freenect.dll" $OutDir
Copy-Item -Force "$prefix\bin\freenect2.dll" $OutDir
Copy-Item -Force "$libusbDir\libusb-1.0.dll" $OutDir
Copy-Item -Force "$jpeg\bin\turbojpeg.dll" $OutDir
$redist = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory |
  Where-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
  Sort-Object Name | Select-Object -Last 1
if (-not $redist) { throw 'Visual C++ redistributable files were not found' }
$runtimeDirs = Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory |
  Where-Object { $_.Name -match '^Microsoft\.VC14\d\.(CRT|OpenMP)$' }
foreach ($dir in $runtimeDirs) {
  Get-ChildItem $dir.FullName -Filter *.dll |
    Where-Object { $_.Name -match '^(msvcp140|vcruntime140|vcruntime140_1|vcomp140)\.dll$' } |
    Copy-Item -Destination $OutDir -Force
}

$licenses = Join-Path $OutDir 'licenses'
if (Test-Path $licenses) { Remove-Item -Recurse -Force $licenses }
New-Item -ItemType Directory $licenses | Out-Null
Copy-Item -Force "$src1\APACHE20" "$licenses\libfreenect-APACHE20.txt"
Copy-Item -Force "$src1\CONTRIB" "$licenses\libfreenect-CONTRIB.txt"
Copy-Item -Force "$src\APACHE20" "$licenses\libfreenect2-APACHE20.txt"
Copy-Item -Force "$src\CONTRIB" "$licenses\libfreenect2-CONTRIB.txt"
Copy-Item -Force (Join-Path $here 'patches') (Join-Path $licenses 'libfreenect2-patches') -Recurse
Get-ChildItem $jpeg -Recurse -Include LICENSE.md, README.ijg | ForEach-Object {
  Copy-Item -Force $_.FullName (Join-Path $licenses "libjpeg-turbo-$($_.Name).txt")
}
# libusb is LGPL: ship its license and its source.
$libusbSource = Join-Path $downloads 'libusb-1.0.30.tar.bz2'
$libusbSourceDir = Join-Path $WorkDir 'libusb-source'
New-Item -ItemType Directory -Force $libusbSourceDir | Out-Null
Invoke-Checked tar @('-xjf', $libusbSource, '-C', $libusbSourceDir, 'libusb-1.0.30/COPYING')
Copy-Item -Force (Join-Path $libusbSourceDir 'libusb-1.0.30\COPYING') "$licenses\libusb-COPYING.txt"
Copy-Item -Force $libusbSource $licenses

Get-ChildItem $OutDir -File | ForEach-Object { '{0,-24} {1,10:N0}' -f $_.Name, $_.Length }
