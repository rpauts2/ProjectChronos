#!/usr/bin/env pwsh
# Fetch latest CS2 offsets from cs2-dumper and generate offets.json
$base = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output"
$outDir = "resources"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

Write-Host "Fetching CS2 offsets..."

$files = @(
    "client_dll.json",
    "offsets.json",
    "buttons.json"
)

foreach ($f in $files) {
    $url = "$base/$f"
    $out = "$outDir/$f"
    Write-Host "  $f..."
    curl.exe -sL -o $out $url 2>&1
    if ($LASTEXITCODE -eq 0 -and (Get-Item $out).Length -gt 100) {
        Write-Host "    OK ($((Get-Item $out).Length) bytes)"
    } else {
        Write-Host "    FAILED"
    }
}

Write-Host "Done. Offsets saved to $outDir"