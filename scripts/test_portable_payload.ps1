$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/release_payload.ps1"
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo ('bench_data/portable-audit-' + [guid]::NewGuid().ToString('N'))
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
foreach ($file in $PulseReleasePayload) {
    [IO.File]::WriteAllText((Join-Path $build $file), "isolated packaging fixture: $file")
}
foreach ($license in @('LumaText', 'PDFium')) {
    $folder = Join-Path $build "licenses/$license"
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    'license fixture' | Set-Content (Join-Path $folder 'LICENSE.txt')
}
$out = Join-Path $root 'out'
& "$PSScriptRoot/package_portable.ps1" -BuildDir $build -OutputDir $out | Out-Null
$zipFile = Get-ChildItem -LiteralPath $out -Filter '*.zip' | Select-Object -First 1
$zip = [IO.Compression.ZipFile]::OpenRead($zipFile.FullName)
try {
    foreach ($file in $PulseReleasePayload) {
        $entry = @($zip.Entries | Where-Object { $_.Name -eq $file })
        if ($entry.Count -ne 1) { throw "Final ZIP missing $file" }
        Write-Output "[PASS] final ZIP contains $file"
    }
} finally { $zip.Dispose() }
$helper = Join-Path $build 'pulse_elevated.exe'
Move-Item -LiteralPath $helper -Destination (Join-Path $root 'held-helper.exe')
$rejected = $false
try { & "$PSScriptRoot/package_portable.ps1" -BuildDir $build -OutputDir (Join-Path $root 'missing') | Out-Null }
catch { $rejected = $_.Exception.Message -like '*Missing portable dependency: pulse_elevated.exe*' }
if (-not $rejected) { throw 'Missing helper was accepted' }
Write-Output '[PASS] missing elevated helper refuses packaging'
[IO.File]::WriteAllBytes((Join-Path $build 'pulse.exe'), [Text.Encoding]::Unicode.GetBytes('PULSE_SELFTEST_CASE'))
$rejected = $false
try { & "$PSScriptRoot/check_release_payload.ps1" -BuildDir $build | Out-Null }
catch { $rejected = $_.Exception.Message -like '*embedded selftest*' }
if (-not $rejected) { throw 'Selftest payload was accepted' }
Write-Output '[PASS] embedded selftest marker refuses packaging'
$installer = Get-Content (Join-Path $repo 'tools/pack_installer/pack_installer.py') -Raw
foreach ($name in $PulseReleaseExecutables) {
    if ($installer -notmatch [regex]::Escape($name)) { throw "Installer payload drift: $name" }
}
Write-Output '[PASS] installer includes every shared production executable'
Write-Output "Fixtures retained: $root"