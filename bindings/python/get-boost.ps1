<#
.SYNOPSIS
    This script downloads and builds the Boost library and installs it to a specified directory.
    Based on dependency download script written by wcjohns@sandia.gov

.DESCRIPTION
    This script downloads, verifies, extracts, builds, and installs the Boost library.
    It is intended to be run from within the "x64 Native Tools Command Prompt for VS 2022"
    terminal (although b2 may invoke it for you).

    7-zip is required: https://7-zip.org/download.html

    Boost source is from https://archives.boost.io/release/

.PARAMETER buildPath
    The temporary directory where the Boost library will be built.
    Default: "c:\temp\boost-build"

.PARAMETER installPath
    The directory where the Boost library will be installed.
    Default: "c:\boost-install"

.PARAMETER boostUrl
    The URL from which the Boost library source archive will be downloaded.
    Default: "https://archives.boost.io/release/1.86.0/source/boost_1_86_0.7z"

.PARAMETER boostRequiredSha256
    The expected SHA256 hash of the downloaded Boost library source archive.
    Default: "413ee9d5754d0ac5994a3bf70c3b5606b10f33824fdd56cf04d425f2fc6bb8ce"

.PARAMETER sevenZipPath
    The path to the 7-Zip executable (7z.exe).
    Default: "C:\Program Files\7-Zip\7z.exe"    

.EXAMPLE
    .\get-boost.ps1 -buildPath "c:\temp\boost-build" -installPath "c:\boost-install"

.EXAMPLE
    .\get-boost.ps1 -buildPath "c:\temp\boost-build" -installPath "c:\boost-install" `
                    -boostUrl "https://example.com/boost_1_86_0.7z" `
                    -boostRequiredSha256 "your_sha256_hash_here"

.EXAMPLE
    .\get-boost.ps1 -buildPath "c:\temp\boost-build" -installPath "c:\boost-install" `
                     -boostUrl "https://example.com/boost_1_86_0.7z" `
                     -boostRequiredSha256 "your_sha256_hash_here" `
                     -sevenZipPath "C:\Path\To\7z.exe"
#>
param (
    [string]$buildPath = "c:\temp\boost-build-py313-try3",
    [string]$installPath = "c:\boost-install-py313",
    [string]$boostUrl = "https://archives.boost.io/release/1.86.0/source/boost_1_86_0.7z",
    [string]$boostRequiredSha256 = "413ee9d5754d0ac5994a3bf70c3b5606b10f33824fdd56cf04d425f2fc6bb8ce",
    [string]$sevenZipPath = "C:\Program Files\7-Zip\7z.exe",
    [string]$pythonVersion = "3.13",
    [string]$pythonIncludeDir = "C:\Users\hpbiven\AppData\Local\Programs\Python\Python313\include",
    [string]$pythonLibDir = "C:\Users\hpbiven\AppData\Local\Programs\Python\Python313\libs"
)

if (-not $buildPath -or -not $installPath) {
    Write-Host "Usage: .\get-boost.ps1 [build path] [install path] [boost URL] [boost SHA256]"
    exit 1
}

if (-not (Test-Path -Path $sevenZipPath)) {
    Write-Host "7-Zip executable not found at $sevenZipPath. You can download it here: https://7-zip.org/download.html"
    exit 1
}

$ErrorActionPreference = "Stop"

function Create-Directory {
    param (
        [string]$path
    )
    if (-not (Test-Path -Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
        Write-Host "Created directory $path"
    } else {
        Write-Host "Directory $path already exists."
    }
}

$origDir = Get-Location

Create-Directory -path $buildPath
$buildDir = Resolve-Path -Path $buildPath

Create-Directory -path $installPath
$installDir = Resolve-Path -Path $installPath

Write-Host "Will build in $buildDir"
Write-Host "Will install to $installDir"

pushd -Path $buildDir

$boostZipFile = [System.IO.Path]::GetFileName($boostUrl)
$boostDir = [System.IO.Path]::GetFileNameWithoutExtension($boostZipFile)
$boostBuildStatusFile = "built_$boostDir"

# Determine the user's Downloads folder path
$downloadsFolder = [System.IO.Path]::Combine([System.Environment]::GetFolderPath('UserProfile'), 'Downloads')
$boostZipFilePath = [System.IO.Path]::Combine($downloadsFolder, $boostZipFile)

if (-not (Test-Path -Path $boostBuildStatusFile)) {
    if (-not (Test-Path -Path $boostZipFilePath)) {
        Invoke-WebRequest -Uri $boostUrl -OutFile $boostZipFilePath
        Write-Host "Downloaded Boost to $boostZipFilePath"
    } else {
        Write-Host "$boostZipFile already downloaded in $downloadsFolder"
    }

    $boostSha256 = (Get-FileHash -Path $boostZipFilePath -Algorithm SHA256).Hash

    if ($boostSha256 -ne $boostRequiredSha256) {
        Write-Host "Invalid hash of boost. Expected $boostRequiredSha256 and got $boostSha256"
        Set-Location -Path $origDir
        exit 2
    }

    if (-not (Test-Path -Path $boostDir)) {
        & "$sevenZipPath" x $boostZipFilePath -o"$buildDir"
        Write-Host "Unzipped $boostZipFilePath"
    } else {
        Write-Host "Boost was already unzipped"
    }

    Set-Location -Path $boostDir

    Write-Host "Running boost bootstrap.bat"
    & cmd /c .\bootstrap.bat
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to run bootstrap.bat"
        popd
        exit 2
    }

    Write-Host "Building boost"
    & .\b2.exe --build-type=minimal --with-python runtime-link=static link=static threading=multi variant=release address-model=64 architecture=x86 --prefix=$installDir --build-dir=win_build -j8 install toolset=msvc-14.3 python=$pythonVersion 
    #& .\b2.exe --build-type=complete runtime-link=static link=static threading=multi address-model=64 architecture=x86 --prefix=$installDir --build-dir=win_build -j8 install
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to build boost"
        popd
        exit 3
    }
    #Remove-Item -Recurse -Force win_build

    Write-Host "Built boost!"

    Set-Location -Path $buildDir

    # Keep around in case we want to re-build with different options?
    #Remove-Item -Recurse -Force $boostDir
    #Write-Host "Removed $boostDir directory"

    New-Item -ItemType File -Path $boostBuildStatusFile | Out-Null
} else {
    Write-Host "Boost was already built ($boostBuildStatusFile existed)"
}

Write-Host "Completed Successfully"
popd

exit 0
