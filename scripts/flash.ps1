<#
.SYNOPSIS
    XX-39R Firmware Flasher for Windows
.DESCRIPTION
    Interactive flashing script for XX-39R replacement board firmware.
    Wraps avrdude with auto-detection, pretty-printed fuse reading,
    and safe fuse writing.
.NOTES
    Requires: avrdude with serialupdi support in PATH
    Run as:   PowerShell -ExecutionPolicy Bypass -File .\flash.ps1
#>

#Requires -Version 5.1

param(
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

$Avrdude = "avrdude"
$Mcu = "t414"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# Default to ../firmware/build (scripts/ inside release zip sits next to firmware/)
$BuildDir = Resolve-Path (Join-Path $ScriptDir ".." "firmware" "build") -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path
if (-not $BuildDir) {
    $BuildDir = Resolve-Path (Join-Path $ScriptDir ".." "firmware") | Select-Object -ExpandProperty Path
}
$HexDir = $BuildDir

# ---------------------------------------------------------------------------
# Discover available hex variants (so we only offer what exists)
# ---------------------------------------------------------------------------

$HasKs = $false
$HasWs = $false
$Has5m = $false
$Has10m = $false
$Has16m = $false
$Has20m = $false

$HexFiles = Get-ChildItem -Path $HexDir -Filter "*.hex" -ErrorAction SilentlyContinue
foreach ($f in $HexFiles) {
    $name = $f.Name
    if ($name -match "-ks")  { $HasKs  = $true }
    if ($name -match "-ws")  { $HasWs  = $true }
    if ($name -match "-5m")  { $Has5m  = $true }
    if ($name -match "-10m") { $Has10m = $true }
    if ($name -match "-16m") { $Has16m = $true }
    if ($name -notmatch "-(5m|10m|16m)\.hex$") { $Has20m = $true }
}

# ---------------------------------------------------------------------------
# ANSI colours (Windows Terminal / modern consoles)
# ---------------------------------------------------------------------------

$UseColor = $Host.UI.SupportsVirtualTerminal
function C($Code, $Text) {
    if ($UseColor) { "`e[$($Code)m$Text`e[0m" } else { $Text }
}
function Red($t)    { C "31;1", $t }
function Green($t)  { C "32;1", $t }
function Yellow($t) { C "33;1", $t }
function Blue($t)   { C "34;1", $t }
function Cyan($t)   { C "36;1", $t }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Banner {
    Write-Host ""
    Write-Host (Blue "========================================")
    Write-Host (Blue "  XX-39R Firmware Flasher")
    Write-Host (Blue "========================================")
    Write-Host ""
}

function Die($msg) {
    Write-Host (Red "ERROR: $msg") -ForegroundColor Red
    exit 1
}

function Warn($msg) {
    Write-Host (Yellow "WARNING: $msg")
}

function Info($msg) {
    Write-Host (Cyan $msg)
}

function Ok($msg) {
    Write-Host (Green $msg)
}

function Prompt($msg) {
    Write-Host (Cyan $msg) -NoNewline
}

# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------

function Detect-Port {
    $candidates = @()

    # Try WMI first (works on most Windows versions)
    try {
        $ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
            Where-Object { $_.PNPDeviceID -match "USB" -or $_.Description -match "USB" }
        foreach ($p in $ports) {
            $candidates += $p.DeviceID
        }
    } catch {}

    # Fallback: list COM devices from registry
    if ($candidates.Count -eq 0) {
        try {
            $reg = Get-ItemProperty "HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM" -ErrorAction SilentlyContinue
            if ($reg) {
                foreach ($v in $reg.PSObject.Properties) {
                    if ($v.Name -ne "PSPath" -and $v.Value -match "^COM\d+") {
                        $candidates += $v.Value
                    }
                }
            }
        } catch {}
    }

    # Deduplicate
    $candidates = $candidates | Select-Object -Unique

    if ($candidates.Count -eq 0) {
        return $null
    } elseif ($candidates.Count -eq 1) {
        return $candidates[0]
    } else {
        Write-Host ""
        Info "Multiple serial ports found:"
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            Write-Host "  $($i+1)) $($candidates[$i])"
        }
        Prompt "Select port (1-$($candidates.Count)): "
        $choice = Read-Host
        if ($choice -match '^\d+$') {
            $idx = [int]$choice - 1
            if ($idx -ge 0 -and $idx -lt $candidates.Count) {
                return $candidates[$idx]
            }
        }
        return $null
    }
}

# ---------------------------------------------------------------------------
# Fuse helpers
# ---------------------------------------------------------------------------

function Read-Fuses($port) {
    $cmd = "$Avrdude -c serialupdi -P `"$port`" -p $Mcu -U fuse0:r:-:h -U fuse1:r:-:h -U fuse2:r:-:h"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = "/c `"$cmd`""
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    $out = $proc.StandardOutput.ReadToEnd()
    $err = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()

    if ($proc.ExitCode -ne 0) {
        Warn "avrdude failed to read fuses"
        Write-Host $err
        return $null
    }

    $combined = $out + $err

    # Try multiple patterns
    $hexVals = [regex]::Matches($combined, '0x([0-9A-Fa-f]{2})') |
        ForEach-Object { $_.Groups[1].Value }

    if ($hexVals.Count -ge 3) {
        return "$($hexVals[0]) $($hexVals[1]) $($hexVals[2])"
    }
    return $null
}

function Parse-Bodcfg($val) {
    $n = [Convert]::ToInt32($val, 16)
    $activeBits = ($n -shr 3) -band 0x03
    $lvl = $n -band 0x07

    $activeText = switch ($activeBits) {
        0 { "DISABLED (unsafe for 3xAA!)" }
        1 { "enabled (continuous)" }
        2 { "enabled (sampled)" }
        3 { "enabled (sleep)" }
        default { "unknown" }
    }

    $voltText = switch ($lvl) {
        0 { "1.8V" }
        1 { "2.15V" }
        2 { "2.6V" }
        3 { "2.9V" }
        4 { "3.3V" }
        5 { "3.7V" }
        6 { "4.0V" }
        7 { "4.3V" }
        default { "unknown" }
    }

    return "BOD $activeText, threshold $voltText"
}

function Parse-Osccfg($val) {
    switch ($val) {
        "01" { return "16 MHz internal oscillator" }
        "02" { return "20 MHz internal oscillator" }
        default { return "unknown (0x$val)" }
    }
}

function Show-Fuses($port) {
    $fuses = Read-Fuses $port
    if (-not $fuses) {
        Warn "Could not read fuses."
        return
    }

    $parts = $fuses -split '\s+'
    $fuse0 = $parts[0]
    $fuse1 = $parts[1]
    $fuse2 = $parts[2]

    Write-Host ""
    Write-Host (Blue "Current fuse values:")
    Write-Host "  fuse0 (WDTCFG) = 0x$fuse0   -- WDT configuration fuse"
    Write-Host "  fuse1 (BODCFG) = 0x$fuse1   -- $(Parse-Bodcfg $fuse1)"
    Write-Host "  fuse2 (OSCCFG) = 0x$fuse2   -- $(Parse-Osccfg $fuse2)"
    Write-Host ""

    if ($fuse1 -eq "00") {
        Warn "Brown-out detection is DISABLED. This is unsafe for 3xAA operation."
        Warn "Consider running the fuse setup after flashing."
    }
}

# ---------------------------------------------------------------------------
# Firmware selection
# ---------------------------------------------------------------------------

function Select-Firmware {
    Write-Host ""
    Info "Select firmware category:"
    Write-Host "  1) Main firmware (for driving the boat)"
    Write-Host "  2) Test firmware (diagnostics / bench testing)"
    Write-Host "  q) Quit"
    Prompt "Choice: "
    $choice = Read-Host

    switch ($choice) {
        "1" { Select-MainFirmware }
        "2" { Select-TestFirmware }
        "q" { Ok "Goodbye!"; exit 0 }
        default { Warn "Invalid choice"; Select-Firmware }
    }
}

function Select-MainFirmware {
    Write-Host ""
    Info "Select main firmware:"
    Write-Host "  1) Generic firmware (no stall compensation)"
    Write-Host "  2) Spring Tide 40 (MOTOR_STALL_SPEED=140, kid-friendly)"
    Write-Host "  b) Back"
    Prompt "Choice: "
    $choice = Read-Host

    switch ($choice) {
        "1" {
            $features = Select-Features "firmware" "main"
            $hexFile = Join-Path $HexDir "firmware${features}.hex"
        }
        "2" {
            $features = Select-Features "firmware-springtide40" "springtide"
            $hexFile = Join-Path $HexDir "firmware-springtide40${features}.hex"
        }
        "b" { Select-Firmware; return }
        default { Warn "Invalid choice"; Select-MainFirmware; return }
    }

    Confirm-And-Flash $hexFile
}

function Select-TestFirmware {
    Write-Host ""
    Info "Select test firmware:"
    Write-Host "  1) Motor test via UART (firmware-test)"
    Write-Host "  2) GPIO toggle test (firmware-test-gpio)"
    Write-Host "  3) Port motor PWM sweep (firmware-test-pwm-port)"
    Write-Host "  4) Starboard motor PWM sweep (firmware-test-pwm-starboard)"
    Write-Host "  5) UART loopback test (firmware-test-uart)"
    Write-Host "  6) PB2 output test (firmware-test-pb2)"
    Write-Host "  7) Water sensor test (firmware-test-water)"
    Write-Host "  b) Back"
    Prompt "Choice: "
    $choice = Read-Host

    switch ($choice) {
        "1" {
            $features = Select-Features "firmware-test" "main"
            $hexFile = Join-Path $HexDir "firmware-test${features}.hex"
        }
        "2" {
            $features = Select-Features "firmware-test-gpio" "gpio"
            $hexFile = Join-Path $HexDir "firmware-test-gpio${features}.hex"
        }
        "3" {
            $features = Select-Features "firmware-test-pwm-port" "pwm"
            $hexFile = Join-Path $HexDir "firmware-test-pwm-port${features}.hex"
        }
        "4" {
            $features = Select-Features "firmware-test-pwm-starboard" "pwm"
            $hexFile = Join-Path $HexDir "firmware-test-pwm-starboard${features}.hex"
        }
        "5" {
            $features = Select-Features "firmware-test-uart" "uart"
            $hexFile = Join-Path $HexDir "firmware-test-uart${features}.hex"
        }
        "6" {
            $features = Select-Features "firmware-test-pb2" "pb2"
            $hexFile = Join-Path $HexDir "firmware-test-pb2${features}.hex"
        }
        "7" {
            $features = Select-Features "firmware-test-water" "water"
            $hexFile = Join-Path $HexDir "firmware-test-water${features}.hex"
        }
        "b" { Select-Firmware; return }
        default { Warn "Invalid choice"; Select-TestFirmware; return }
    }

    Confirm-And-Flash $hexFile
}

function Select-Features($baseName, $hint) {
    $freq = ""
    $ks = ""
    $ws = ""

    # Frequency — only offer variants that exist
    $availFreqs = @()
    if ($Has20m) { $availFreqs += "20 MHz (default)" }
    if ($Has10m) { $availFreqs += "10 MHz (/2 prescaler)" }
    if ($Has5m)  { $availFreqs += "5 MHz (/4 prescaler)" }
    if ($Has16m) { $availFreqs += "16 MHz (requires 16 MHz fuse)" }

    if ($availFreqs.Count -gt 1) {
        Write-Host ""
        Info "Select CPU frequency:"
        for ($i = 0; $i -lt $availFreqs.Count; $i++) {
            Write-Host "  $($i+1)) $($availFreqs[$i])"
        }
        Prompt "Choice: "
        $freqChoice = Read-Host
        $freqMap = @(""); $idx = 1
        if ($Has20m) { $freqMap += ""; $idx++ }
        if ($Has10m) { $freqMap += "-10m"; $idx++ }
        if ($Has5m)  { $freqMap += "-5m"; $idx++ }
        if ($Has16m) { $freqMap += "-16m"; $idx++ }
        if ($freqChoice -match '^\d+$') {
            $fIdx = [int]$freqChoice
            if ($fIdx -ge 1 -and $fIdx -lt $freqMap.Count) {
                $freq = $freqMap[$fIdx]
            }
        }
    }

    # Kickstart — only offer if ks variants exist
    if ($HasKs -and ($hint -eq "main" -or $hint -eq "pwm")) {
        Write-Host ""
        Info "Enable motor kickstart? (helps low-speed startup)"
        Write-Host "  1) No (default)"
        Write-Host "  2) Yes"
        Prompt "Choice: "
        $ksChoice = Read-Host
        if ($ksChoice -eq "2") { $ks = "-ks" }
    }

    # Water sensor — only offer if ws variants exist
    if ($HasWs -and $hint -eq "springtide") {
        Write-Host ""
        Info "Enable water safety sensor? (stops motors when out of water)"
        Write-Host "  1) No (default)"
        Write-Host "  2) Yes"
        Prompt "Choice: "
        $wsChoice = Read-Host
        if ($wsChoice -eq "2") { $ws = "-ws" }
    }

    return "$ks$ws$freq"
}

function Confirm-And-Flash($hexFile) {
    # If the exact file doesn't exist, try the suffixed variant (e.g.
    # firmware-test.hex might actually be firmware-test-20m.hex when built
    # with all-freqs).
    if (-not (Test-Path $hexFile)) {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($hexFile)
        $alt = Join-Path (Split-Path -Parent $hexFile) "$base-20m.hex"
        if (Test-Path $alt) {
            $hexFile = $alt
        } else {
            Die "Hex file not found: $(Split-Path -Leaf $hexFile) (also tried $(Split-Path -Leaf $alt))"
        }
    }

    $size = (Get-Item $hexFile).Length
    Write-Host ""
    Info "Firmware: $(Split-Path -Leaf $hexFile)"
    Info "Size: $size bytes"
    Write-Host ""
    Prompt "Flash this firmware to ATtiny414? (y/N): "
    $confirm = Read-Host
    if ($confirm -notmatch '^[Yy]$') {
        Info "Aborted."
        return
    }

    Write-Host ""
    Info "Flashing..."
    & $Avrdude -c serialupdi -P $Port -p $Mcu -U flash:w:"$hexFile":i
    if ($LASTEXITCODE -ne 0) {
        Die "Flash failed"
    }

    Ok "Flash successful!"
}

# ---------------------------------------------------------------------------
# Fuse writing
# ---------------------------------------------------------------------------

function Offer-Fuses {
    Write-Host ""
    Info "Would you like to set safe fuses for 3xAA operation?"
    Write-Host "  Recommended: BOD enabled @ 2.6V, 20 MHz oscillator"
    Write-Host ""
    Prompt "Write safe fuses? (y/N): "
    $confirm = Read-Host
    if ($confirm -notmatch '^[Yy]$') {
        return
    }

    Show-Fuses $Port

    Write-Host ""
    Info "Proposed new values:"
    Write-Host "  fuse1 (BODCFG) = 0x0A   -- BOD enabled @ 2.6V"
    Write-Host "  fuse2 (OSCCFG) = 0x02   -- 20 MHz oscillator"
    Write-Host ""
    Prompt "Confirm fuse write? (y/N): "
    $confirm2 = Read-Host
    if ($confirm2 -notmatch '^[Yy]$') {
        Info "Fuse write cancelled."
        return
    }

    & $Avrdude -c serialupdi -P $Port -p $Mcu -U fuse1:w:0x0A:m -U fuse2:w:0x02:m
    if ($LASTEXITCODE -ne 0) {
        Die "Fuse write failed"
    }

    Ok "Fuses written successfully."
    Write-Host ""
    Show-Fuses $Port
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Banner

# Check avrdude
if (-not (Get-Command $Avrdude -ErrorAction SilentlyContinue)) {
    Die "avrdude not found in PATH. Please install it."
}
Ok "avrdude found: $(Get-Command $Avrdude).Source"

# Detect or ask for port
if (-not $Port) {
    Info "Detecting programmer port..."
    $Port = Detect-Port
    if (-not $Port) {
        Write-Host ""
        Warn "Could not auto-detect programmer port."
        Prompt "Please enter port manually (e.g. COM3): "
        $Port = Read-Host
        if (-not $Port) { Die "No port specified." }
    }
}
Ok "Using port: $Port"

# Check hex files
$hexFiles = Get-ChildItem $HexDir\*.hex -ErrorAction SilentlyContinue
if (-not $hexFiles) {
    Die "No .hex files found in $HexDir"
}

# Main loop
while ($true) {
    Write-Host ""
    Write-Host "Main menu:"
    Write-Host "  1) Read current fuses"
    Write-Host "  2) Flash firmware"
    Write-Host "  3) Write safe fuses (BOD @ 2.6V, 20 MHz)"
    Write-Host "  q) Quit"
    Prompt "Choice: "
    $menuChoice = Read-Host

    switch ($menuChoice) {
        "1" { Show-Fuses $Port }
        "2" { Select-Firmware }
        "3" { Offer-Fuses }
        "q" { Ok "Goodbye!"; exit 0 }
        default { Warn "Invalid choice" }
    }
}
