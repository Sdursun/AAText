# Runs tests/amiga/gui-test on an Amiga through AmigaBridge (amigactl):
# copies the four GUI programs and the scripts to RAM:aaguitest/, runs
# the test, prints its log and removes the directory again.
#
#   .\tools\run-guitests.ps1                  (WinUAE)
#   .\tools\run-guitests.ps1 -AmigaHost 10.20.10.81 -Loops 5
#
# Build first: .\build.ps1 gui manager gui_mui manager_mui
param(
    [string]$AmigaHost = "",
    [int]$Loops = 20
)

$ErrorActionPreference = "Stop"
$ctl = Join-Path $env:LOCALAPPDATA "AmigaBridge\bin\amigactl.exe"
$hostArgs = @()
if ($AmigaHost) { $hostArgs = @("--host", $AmigaHost) }
$dir = "RAM:aaguitest/"

function Amiga { & $ctl @hostArgs @args; if ($LASTEXITCODE) { throw "amigactl $args failed" } }

Amiga shell "If NOT EXISTS RAM:aaguitest`nMakeDir RAM:aaguitest`nEndIf" | Out-Null
foreach ($p in "AATextPrefs", "AATextManager", "AATextPrefs_MUI", "AATextManager_MUI") {
    Amiga put (Join-Path $PSScriptRoot "..\build\68020\$p") $dir | Out-Null
}
# the scripts in ISO-8859-9 ("türkçe"), the Amiga's character set
$tmp = New-Item -ItemType Directory -Force (Join-Path $env:TEMP "aaguitest")
foreach ($s in "gui-test", "gui-run", "gui-loop") {
    $text = Get-Content -Raw -Encoding utf8 (Join-Path $PSScriptRoot "..\tests\amiga\$s")
    [IO.File]::WriteAllText((Join-Path $tmp $s), $text.Replace("`r`n", "`n"),
                            [Text.Encoding]::GetEncoding("iso-8859-9"))
    Amiga put (Join-Path $tmp $s) $dir | Out-Null
}

# each program runs about 8 s; 8 starts plus 2 per loop
$timeout = 60 + (8 + 2 * $Loops) * 10
Amiga shell --timeout $timeout "Execute ${dir}gui-test $dir $Loops"
Amiga shell "Delete $dir ALL QUIET" | Out-Null
