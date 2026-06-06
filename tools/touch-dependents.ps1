# touch-dependents.ps1
# Run this after modifying any .h file to ensure Ninja rebuilds all stale .cpp
# objects.  Ninja's .d dependency files track headers, but can miss changes when
# the header was modified after the last compilation of a dependent file.
#
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File tools\touch-dependents.ps1
#
# Or with specific headers:
#   powershell -File tools\touch-dependents.ps1 -Headers "DataLayer.h","PingRow.h"

param(
    [string[]]$Headers = @()
)

Set-Location (Split-Path $PSScriptRoot)

if ($Headers.Count -eq 0) {
    # Default: all headers that differ from HEAD
    $Headers = git diff --name-only HEAD -- "*.h" 2>$null |
               ForEach-Object { [System.IO.Path]::GetFileName($_) } |
               Where-Object { $_ -ne "" }

    if ($Headers.Count -eq 0) {
        Write-Host "No modified headers found — nothing to touch."
        exit 0
    }
}

Write-Host "Scanning for .cpp files that include: $($Headers -join ', ')"

$pattern = ($Headers | ForEach-Object { [regex]::Escape($_) }) -join "|"

$touched = 0
Get-ChildItem -Recurse -Path src -Include "*.cpp" | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content -and $content -match $pattern) {
        $_.LastWriteTime = Get-Date
        $touched++
    }
}

Write-Host "Touched $touched .cpp files — run cmake --build to recompile them."
