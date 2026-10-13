param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("smoothed", "original")]
    [string] $Mode
)

$adbExecutable = (Get-Command adb -ErrorAction Stop).Source
$debugPackage = "com.nendo.argosy.debug"
$corePath = "files/libretro/cores/snes9x_libretro_android.so"
$backupPath = "$corePath.pre-volume-smoothing"
$backupHashPath = "$backupPath.sha256"
$stagedPath = "$corePath.staged"
$transferPath = "/data/local/tmp/argosy-snes-volume-smooth.so"

function Invoke-CheckedAdb {
    param([string[]] $AdbArguments)
    & $adbExecutable @AdbArguments
    if ($LASTEXITCODE -ne 0) { throw "ADB failed. Keep the original-core backup and send the error." }
}

function Get-CoreHash {
    param([string] $DevicePath)
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "test", "-s", $DevicePath)
    $result = @(& $adbExecutable shell run-as $debugPackage sha256sum $DevicePath)
    if ($LASTEXITCODE -ne 0 -or $result.Count -ne 1 -or $result[0] -notmatch "^([0-9a-fA-F]{64})\s") {
        throw "Could not verify the device core checksum."
    }
    return $Matches[1].ToLower()
}

Invoke-CheckedAdb -AdbArguments @("get-state")
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "test", "-d", "files/libretro/cores")
if ($Mode -eq "smoothed") {
    $abis = @(& $adbExecutable shell getprop ro.product.cpu.abilist)
    if ($LASTEXITCODE -ne 0 -or ($abis -join ",") -notmatch "(^|,)arm64-v8a(,|$)") {
        throw "This test core requires an arm64 Android device."
    }
    $localCore = Join-Path $PSScriptRoot "snes9x_libretro_android.so"
    $checksumFile = Join-Path $PSScriptRoot "SHA256SUMS"
    $expectedLine = @(Get-Content -LiteralPath $checksumFile -ErrorAction Stop |
        Where-Object { $_ -match "^[0-9a-fA-F]{64}\s+\*?snes9x_libretro_android\.so$" })
    if ($expectedLine.Count -ne 1) { throw "Missing test-core checksum." }
    $expectedHash = ($expectedLine[0] -split "\s+")[0].ToLower()
    if ((Get-FileHash -LiteralPath $localCore -Algorithm SHA256 -ErrorAction Stop).Hash.ToLower() -ne $expectedHash) {
        throw "The downloaded test core does not match its checksum."
    }
}

Write-Host "Save your game and exit it through Argosy DBG's menu before continuing."
$null = Read-Host "Press Enter once the game is closed"
Invoke-CheckedAdb -AdbArguments @("shell", "am", "force-stop", $debugPackage)

& $adbExecutable shell run-as $debugPackage test -f $backupPath
$backupExists = $LASTEXITCODE -eq 0
if ($Mode -eq "smoothed") {
    $originalHash = Get-CoreHash -DevicePath $corePath
    if (-not $backupExists) {
        if ($originalHash -eq $expectedHash) { throw "The smoothed core is installed but its original backup is missing." }
        Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "cp", $corePath, "$backupPath.staged")
        if ((Get-CoreHash -DevicePath "$backupPath.staged") -ne $originalHash) { throw "The original-core backup could not be verified." }
        Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "mv", "$backupPath.staged", $backupPath)
    }
}
if (-not $backupExists -and $Mode -eq "original") { throw "No original-core backup exists. The installed core was not changed." }
$backupHash = Get-CoreHash -DevicePath $backupPath
& $adbExecutable shell run-as $debugPackage test -f $backupHashPath
if ($LASTEXITCODE -ne 0) {
    if ((Get-CoreHash -DevicePath $corePath) -ne $backupHash) { throw "The backup checksum is missing and cannot be reconstructed from the installed core." }
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "sh", "-c", "'echo $backupHash > $backupHashPath'")
}
$savedBackupHash = @(& $adbExecutable shell run-as $debugPackage cat $backupHashPath)
if ($LASTEXITCODE -ne 0 -or $savedBackupHash.Count -ne 1 -or $savedBackupHash[0].Trim().ToLower() -ne $backupHash) {
    throw "The original-core backup does not match its saved checksum. The installed core was not changed."
}
if ($Mode -eq "smoothed") {
    Invoke-CheckedAdb -AdbArguments @("push", $localCore, $transferPath)
    try {
        Invoke-CheckedAdb -AdbArguments @("shell", "chmod", "644", $transferPath)
        Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "cp", $transferPath, $stagedPath)
        if ((Get-CoreHash -DevicePath $stagedPath) -ne $expectedHash) { throw "The transferred test core could not be verified." }
    } finally {
        & $adbExecutable shell rm -f $transferPath
    }
} else {
    $expectedHash = $backupHash
    Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "cp", $backupPath, $stagedPath)
    if ((Get-CoreHash -DevicePath $stagedPath) -ne $expectedHash) { throw "The staged original core could not be verified." }
}
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "chmod", "700", $stagedPath)
Invoke-CheckedAdb -AdbArguments @("shell", "run-as", $debugPackage, "mv", $stagedPath, $corePath)
if ((Get-CoreHash -DevicePath $corePath) -ne $expectedHash) { throw "The installed core could not be verified." }
Invoke-CheckedAdb -AdbArguments @("shell", "am", "start", "-n", "$debugPackage/com.nendo.argosy.MainActivity")
Write-Host "Selected $Mode Snes9x in Argosy DBG. Launch Super Metroid and listen to Ceres."
Write-Host "The original core backup remains at $backupPath."
