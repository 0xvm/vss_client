$inputPath = $null
$outputPath = $null
$xorSeed = $null

for ($i = 0; $i -lt $args.Length; $i++) {
    $arg = $args[$i]
    if ($arg -eq "--xor-seed") {
        if ($i + 1 -ge $args.Length) {
            Write-Host "Usage: powershell -File unscramble.ps1 <scrambled.zip> [output.zip] [--xor-seed seed]"
            exit 1
        }
        $xorSeed = $args[++$i]
        continue
    }

    if (-not $inputPath) {
        $inputPath = $arg
    }
    elseif (-not $outputPath) {
        $outputPath = $arg
    }
    else {
        Write-Host "Usage: powershell -File unscramble.ps1 <scrambled.zip> [output.zip] [--xor-seed seed]"
        exit 1
    }
}

if (-not $inputPath) {
    Write-Host "Usage: powershell -File unscramble.ps1 <scrambled.zip> [output.zip] [--xor-seed seed]"
    exit 1
}

if (-not $outputPath) {
    $outputPath = "$inputPath.fixed.zip"
}

if (-not (Test-Path $inputPath)) {
    Write-Host "Input file not found: $inputPath"
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($inputPath)
$byteCount = $bytes.Length

if ($null -ne $xorSeed) {
    try {
        $style = [System.Globalization.NumberStyles]::Integer
        $text = $xorSeed
        if ($xorSeed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
            $style = [System.Globalization.NumberStyles]::AllowHexSpecifier
            $text = $xorSeed.Substring(2)
        }
        $state = [uint32]::Parse($text, $style, [System.Globalization.CultureInfo]::InvariantCulture)
    }
    catch {
        Write-Host "Invalid XOR seed."
        exit 1
    }

    $activity = "Applying XOR stream"
    if ($byteCount -gt 0) {
        $percentStride = [Math]::Max([int]([Math]::Floor($byteCount / 100.0)), 1)
        $progressStride = [Math]::Min($percentStride, 65536)
    }

    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $state = [uint32]((([uint64]214013 * $state + 2531011) % 0x100000000))
        $keyByte = [byte]($state -shr 24)
        $bytes[$i] = $bytes[$i] -bxor $keyByte
        if ($byteCount -gt 0) {
            $processed = $i + 1
            if (($processed % $progressStride) -eq 0 -or $processed -eq $byteCount) {
                $percent = [int](($processed * 100) / $byteCount)
                Write-Progress -Activity $activity -Status "$percent% complete" -PercentComplete $percent
            }
        }
    }
    if ($byteCount -gt 0) {
        Write-Progress -Activity $activity -Completed -Status "Done"
    }
}

[System.IO.File]::WriteAllBytes($outputPath, $bytes)
Write-Host "Patched archive written to $outputPath"
