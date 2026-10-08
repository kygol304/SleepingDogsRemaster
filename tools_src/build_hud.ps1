# Construit les écrans Flash modifiés et les injecte dans Sleeping Dogs (UI.big / UI.bix).
#   - HUD façon GTA VI : script principal du HUD + ui\hud_gta.as
#   - garde-robe avec la ligne ARMES : script principal de wardrobe.bin + ui\wardrobe_gta.as
# Les deux sont injectés ensemble : ui_patch repart des archives d'origine à chaque application.
param([switch]$Launch)
$ErrorActionPreference = "Stop"
$R    = Split-Path $PSScriptRoot   # racine du projet (dossier parent de tools_src)
$Java = "$R\tools\jdk-21.0.12.1+1\bin\java.exe"
$Ffdec = "$R\tools\jpexs\ffdec-cli.jar"
$Game = "C:\Program Files (x86)\Steam\steamapps\common\SleepingDogsDefinitiveEdition"
$Out  = "C:\skbuild\ui_build"
$env:TMP = "C:\skbuild\tmp"; $env:TEMP = "C:\skbuild\tmp"

if (Get-Process sdhdship -ErrorAction SilentlyContinue) { throw "Sleeping Dogs est ouvert : ferme-le avant d'injecter les ecrans." }

# Un écran : script d'origine + ajout, recompilé par JPEXS, puis remis dans son en-tête .bin.
function Build-Screen($name, $uid, $scriptRel, $addition) {
    $work = "$Out\$name-" + (Get-Date -Format "yyyyMMdd-HHmmss")
    New-Item -ItemType Directory -Force (Split-Path "$work\scripts\$scriptRel") | Out-Null
    $orig = Get-Content "C:\skbuild\ui_src\$name\scripts\$scriptRel" -Raw
    $add  = Get-Content "$R\ui\$addition" -Raw
    Set-Content "$work\scripts\$scriptRel" ($orig + "`r`n" + $add) -Encoding UTF8
    & $Java -jar $Ffdec -importScript "C:\skbuild\ui_gfx\0x$uid.gfx" "$work\$name.gfx" "$work\scripts" 2>&1 | Select-Object -Last 3 | Out-Host
    if (-not (Test-Path "$work\$name.gfx")) { throw "JPEXS n'a pas produit $name (erreur de compilation ?)" }
    python "$R\tools_src\ui_wrap.py" "C:\skbuild\sd_extract\UI\_Unknown_\0x$uid.bin" "$work\$name.gfx" "$work\$name.bin" | Out-Host
    return "$work\$name.bin"
}

$hud = Build-Screen "Hud" "4FAA1890" "frame_1\DoAction.as" "hud_gta.as"
$wardrobe = Build-Screen "Wardrobe" "53ACE131" "frame_1\DoAction_2.as" "wardrobe_gta.as"
python "$R\tools_src\ui_patch.py" apply $Game UI "4FAA1890=$hud" "53ACE131=$wardrobe"
if ($LASTEXITCODE -ne 0) { throw "injection echouee" }

if ($Launch) { Start-Process "steam://rungameid/307690"; "jeu lance" }
