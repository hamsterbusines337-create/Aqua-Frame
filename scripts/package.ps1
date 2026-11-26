$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $dist = Join-Path $root 'dist'
    $runtime = Join-Path $dist 'runtime'
    & cmake --install build --config Release --prefix $runtime
    if ($LASTEXITCODE -ne 0) { throw 'Staging failed.' }
    foreach ($file in 'AquaFrame.exe', 'AquaFrameReset.exe', 'libcef.dll', 'icudtl.dat', 'resources.pak', 'locales\en-US.pak') {
        if (!(Test-Path (Join-Path $runtime $file))) { throw "Missing runtime file: $file" }
    }
    $zip = Join-Path $dist 'AquaFrame-Portable.zip'
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip }
    [IO.Compression.ZipFile]::CreateFromDirectory($runtime, $zip, 'Fastest', $false)

    $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    if (!(Test-Path $iscc)) { throw 'Inno Setup 6 is required for packaging.' }
    & $iscc installer/AquaFrame.iss
    if ($LASTEXITCODE -ne 0) { throw 'Installer creation failed.' }
    Copy-Item third_party/cef/.aqua-package.json (Join-Path $dist 'cef-package.json') -Force
    Get-FileHash -Algorithm SHA256 -Path @($zip, (Join-Path $dist 'AquaFrame-Setup.exe')) |
        ForEach-Object { '{0} *{1}' -f $_.Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) } |
        Set-Content (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
} finally {
    Pop-Location
}
