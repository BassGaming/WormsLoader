# Copies the built loader + mods into the game folder. Run build.bat first.
#   .\install.ps1                 install
#   .\install.ps1 -Uninstall      remove dinput8.dll (mods folder and settings are kept)
param(
    [string]$GameDir = 'H:\Games\SteamLibrary\steamapps\common\Worms Clan Wars',
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'build\out'
$dll = Join-Path $GameDir 'dinput8.dll'

if ($Uninstall) {
    if (Test-Path $dll) { Remove-Item $dll; "Removed $dll" }
    return
}
if (-not (Test-Path (Join-Path $GameDir 'WormsClanWars.exe'))) { throw "WormsClanWars.exe not found in $GameDir" }
if (-not (Test-Path (Join-Path $out 'dinput8.dll'))) { throw "Nothing built yet: run build.bat" }

# A dinput8.dll that is not ours (another mod) gets a one-time backup.
if ((Test-Path $dll) -and -not (Select-String -Path $dll -Pattern 'WormsLoader' -SimpleMatch -Quiet)) {
    Copy-Item $dll "$dll.bak" -Force
    "Backed up existing dinput8.dll to dinput8.dll.bak"
}
Copy-Item (Join-Path $out 'dinput8.dll') $dll -Force
Get-ChildItem (Join-Path $out 'Mods') -Directory | ForEach-Object {
    $dst = Join-Path $GameDir "Mods\$($_.Name)"
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item (Join-Path $_.FullName '*') $dst -Recurse -Force # the build has no settings.ini, so the user's is kept
    "Installed mod $($_.Name)"
}
"Installed WormsLoader to $GameDir"
