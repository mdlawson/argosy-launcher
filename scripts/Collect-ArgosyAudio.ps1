param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("legacy", "streaming")]
    [string] $Mode,

    [ValidatePattern("^[A-Za-z0-9_-]+$")]
    [string] $Core = "snes9x",

    [switch] $Capture
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
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "rm", "-f", "files/audio-diagnostics.txt", "files/audio-capture")
$otherMode = if ($Mode -eq "legacy") { "streaming" } else { "legacy" }
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "rm", "-f", "files/audio-$otherMode")
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "touch", "files/audio-$Mode")
Invoke-CheckedAdb -AdbArguments @("shell", "am", "start", "-n", "$debugPackage/com.nendo.argosy.MainActivity")

Write-Host "Selected $Mode. Exit any running game, then launch Super Metroid with $Core in Argosy DBG."
if ($Capture) {
    Write-Host "Reach the test scene at normal 1x speed."
    $null = Read-Host "Press Enter when the scene is playing to start the 20-second capture"
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "rm", "-f", "files/audio-core.wav", "files/audio-output.wav", "files/audio-capture.tar")
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "touch", "files/audio-capture")
    Write-Host "Recording. Keep the game running without opening menus or changing settings."
    Start-Sleep -Seconds 24
} else {
    Write-Host "Reach the test scene and listen for at least 30 seconds."
    $null = Read-Host "Press Enter after that run to retrieve the report"
}

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
if ($Capture) {
    for ($captureRetry = 0; $captureRetry -lt 10 -and -not ($report -match "AudioDiag capture saved"); $captureRetry++) {
        Start-Sleep -Seconds 1
        $report = @(& $adbExecutable shell run-as $debugPackage cat files/audio-diagnostics.txt)
        if ($LASTEXITCODE -ne 0) {
            throw "ADB could not retrieve the capture status."
        }
    }
    if (-not ($report -match "AudioDiag capture saved success=1")) {
        throw "The capture did not complete successfully. Keep the game running during capture and relaunch it for each recording."
    }
}
$reportPath = Join-Path (Get-Location) "$Core-$Mode-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
$report | Set-Content -LiteralPath $reportPath -Encoding UTF8 -ErrorAction Stop
Write-Host "Saved diagnostic report: $reportPath"
if ($Capture) {
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "cp", "files/audio-diagnostics.txt", "files/audio-capture-report.txt")
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "tar", "-cf", "files/audio-capture.tar", "files/audio-core.wav", "files/audio-output.wav", "files/audio-capture-report.txt")
    $archivePath = Join-Path (Get-Location) "$Core-$Mode-$(Get-Date -Format yyyyMMdd-HHmmss)-capture.tar"
    $transfer = New-Object System.Diagnostics.Process
    $transfer.StartInfo.FileName = $adbExecutable
    $transfer.StartInfo.Arguments = "exec-out run-as $debugPackage cat files/audio-capture.tar"
    $transfer.StartInfo.UseShellExecute = $false
    $transfer.StartInfo.RedirectStandardOutput = $true
    $transfer.StartInfo.RedirectStandardError = $true
    $archiveFile = $null
    try {
        if (-not $transfer.Start()) { throw "ADB transfer did not start." }
        $transferError = $transfer.StandardError.ReadToEndAsync()
        $archiveFile = [System.IO.File]::Create($archivePath)
        $transfer.StandardOutput.BaseStream.CopyTo($archiveFile)
        $archiveFile.Dispose()
        $archiveFile = $null
        $transfer.WaitForExit()
        if ($transfer.ExitCode -ne 0) { throw "ADB transfer failed: $($transferError.Result)" }
    } finally {
        if ($null -ne $archiveFile) { $archiveFile.Dispose() }
        $transfer.Dispose()
    }
    $archiveEntries = @(& tar -tf $archivePath)
    if ($LASTEXITCODE -ne 0 -or -not ($archiveEntries -contains "files/audio-core.wav") -or
        -not ($archiveEntries -contains "files/audio-output.wav")) {
        throw "The downloaded archive could not be verified."
    }
    Write-Host "Saved capture archive: $archivePath"
    Write-Host "Send the capture archive and whether the crackle was audible during the recording."
} else {
    Write-Host "Send this file and whether the crackle sounded improved, unchanged, or worse."
}
