[CmdletBinding()]
param(
    [string]$Version = '158.0.6+g23f1736+chromium-158.0.8127.89',
    [string]$Sha256
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -and $env:PROCESSOR_ARCHITEW6432 -ne 'AMD64') {
    throw 'An x64 Windows installation is required.'
}
$null = Get-Command cmake, tar.exe
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere) -or !(& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
    throw 'Install Visual Studio 2022 with Desktop development with C++.'
}
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
if (!(Test-Path $sdk) -or !(Get-ChildItem $sdk -Directory | Where-Object {
    Test-Path (Join-Path $_.FullName 'um\Windows.h')
})) {
    throw 'Install the Windows 10 or Windows 11 SDK.'
}
$drive = (Get-Item $root).PSDrive
if ($drive.Free -lt 5GB) { throw 'At least 5 GiB of free space is required.' }
if (!$Sha256 -and $Version -eq '158.0.6+g23f1736+chromium-158.0.8127.89') {
    $Sha256 = '1da0b9fe0e915bd41448939f944003d848f8d9e4d15c7d85d7a17cce656b1a9a'
}
if (!$Sha256) { throw 'Specify the verified SHA-256 for the selected CEF version.' }
if ($Sha256 -and !$Version) { throw '-Sha256 requires an explicit -Version.' }

$base = 'https://cef-builds.spotifycdn.com/'
$index = Invoke-RestMethod -Uri "${base}index.json" -MaximumRedirection 0
$release = $index.windows64.versions | Where-Object {
    $_.channel -eq 'stable' -and (!$Version -or $_.cef_version -eq $Version)
} | Select-Object -First 1
$package = $release.files | Where-Object type -eq 'minimal' | Select-Object -First 1
if (!$package -or $package.name -cnotmatch '^cef_binary_[\w.+-]+_windows64_minimal\.tar\.bz2$' -or
    $package.sha1 -notmatch '^[a-fA-F0-9]{40}$') {
    throw 'No valid Windows x64 stable package was found in the official CEF index.'
}
$thirdParty = Join-Path $root 'third_party'
$destination = Join-Path $thirdParty 'cef'
$stamp = Join-Path $destination '.aqua-package.json'
if (Test-Path $stamp) {
    $existing = Get-Content $stamp -Raw | ConvertFrom-Json
    if ($existing.archive -eq $package.name -and $existing.sha1 -eq $package.sha1 -and
        (!$Sha256 -or $existing.sha256 -eq $Sha256) -and
        (Test-Path (Join-Path $destination 'Release\libcef.dll'))) {
        Write-Host "CEF $($release.cef_version) is already installed."
        return
    }
    throw 'A different CEF version is installed. Move third_party\cef aside before preparing a new version.'
}
if (Test-Path $destination) { throw 'Existing third_party\cef is unmanaged. Move it aside first.' }

$stage = Join-Path $thirdParty ('.cef-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $archive = Join-Path $stage $package.name
    Invoke-WebRequest -UseBasicParsing -Uri ($base + [Uri]::EscapeDataString($package.name)) -OutFile $archive -MaximumRedirection 0
    if ((Get-Item $archive).Length -ne $package.size -or
        (Get-FileHash $archive -Algorithm SHA1).Hash -ne $package.sha1) {
        throw 'The CEF archive does not match the official size and checksum.'
    }
    $digest = (Get-FileHash $archive -Algorithm SHA256).Hash
    if ($Sha256 -and $digest -ne $Sha256) { throw 'CEF archive SHA-256 mismatch.' }

    & tar.exe -xf $archive -C $stage
    if ($LASTEXITCODE -ne 0) { throw 'CEF extraction failed.' }
    $extracted = Join-Path $stage ($package.name -replace '\.tar\.bz2$', '')
    if (!(Test-Path (Join-Path $extracted 'Release\libcef.dll'))) { throw 'CEF extraction is incomplete.' }
    @{
        archive = $package.name
        cef = $release.cef_version
        chromium = $release.chromium_version
        sha1 = $package.sha1
        sha256 = $digest
    } | ConvertTo-Json | Set-Content (Join-Path $extracted '.aqua-package.json') -Encoding ascii
    Move-Item -LiteralPath $extracted -Destination $destination
    Write-Host "Prepared CEF $($release.cef_version), Chromium $($release.chromium_version)."
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
