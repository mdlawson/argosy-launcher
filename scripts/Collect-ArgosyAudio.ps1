param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("legacy", "streaming")]
    [string] $Mode,

    [ValidatePattern("^[A-Za-z0-9_-]+$")]
    [string] $Core = "snes9x"
)

$adbExecutable = (Get-Command adb -ErrorAction Stop).Source
$debugPackage = "com.nendo.argosy.debug"

function Invoke-CheckedAdb {
    param([string[]] $AdbArguments)
    & $adbExecutable @AdbArguments
    if ($LASTEXITCODE -ne 0) {
        throw "ADB failed. Check the connected device and that Argosy DBG is installed."
    }
}

Invoke-CheckedAdb -AdbArguments @("get-state")
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "mkdir", "-p", "files")
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "rm", "-f", "files/audio-diagnostics.txt")
$otherMode = if ($Mode -eq "legacy") { "streaming" } else { "legacy" }
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "rm", "-f", "files/audio-$otherMode")
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "touch", "files/audio-$Mode")
Invoke-CheckedAdb -AdbArguments @("shell", "am", "start", "-n", "$debugPackage/com.nendo.argosy.MainActivity")

Write-Host "Selected $Mode. Exit any running game, then launch Super Metroid with $Core in Argosy DBG."
Write-Host "Reach the test scene and listen for at least 30 seconds."
$null = Read-Host "Press Enter after that run to retrieve the report"

$report = @(& $adbExecutable shell run-as $debugPackage cat files/audio-diagnostics.txt)
if ($LASTEXITCODE -ne 0) {
    throw "No diagnostic report was found. Check that the updated APK is installed and launch the game in Argosy DBG."
}
if (-not ($report -match "AudioDiag config resampler=$Mode")) {
    throw "The report does not show $Mode. Exit and relaunch the game after selecting the mode."
}
if (-not ($report -match "AudioDiag stats")) {
    throw "The report has no audio statistics yet. Let the game play for at least 30 seconds and try again."
}
$reportPath = Join-Path (Get-Location) "$Core-$Mode-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
$report | Set-Content -LiteralPath $reportPath -Encoding UTF8 -ErrorAction Stop
Write-Host "Saved diagnostic report: $reportPath"
Write-Host "Send this file and whether the crackle sounded improved, unchanged, or worse."
