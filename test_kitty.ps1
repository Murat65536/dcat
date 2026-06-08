# Minimal Kitty graphics test: draws a 64x64 red square using the same
# inline (direct, f=32 RGBA, a=T) transport dcat's --kitty-direct uses.
# If you see a red square, ConPTY passes graphics through and the bug is in dcat.
# If you see nothing, ConPTY is stripping the sequences (a Windows limitation).

$ESC = [char]27
$ST  = "$ESC\"
$w = 64; $h = 64

# Build a solid red RGBA buffer
$bytes = New-Object byte[] ($w * $h * 4)
for ($i = 0; $i -lt $bytes.Length; $i += 4) {
    $bytes[$i]   = 255  # R
    $bytes[$i+1] = 0    # G
    $bytes[$i+2] = 0    # B
    $bytes[$i+3] = 255  # A
}

$b64 = [Convert]::ToBase64String($bytes)

# Mirror dcat's screen state: enter the alternate screen first.
[Console]::Out.Write("${ESC}[?1049h")

# Chunk into <=4096-char base64 pieces, kitty m=1 (more) / m=0 (last)
$chunkSize = 4096
$first = $true
for ($off = 0; $off -lt $b64.Length; $off += $chunkSize) {
    $len  = [Math]::Min($chunkSize, $b64.Length - $off)
    $piece = $b64.Substring($off, $len)
    $last = ($off + $len) -ge $b64.Length
    $m = if ($last) { 0 } else { 1 }
    if ($first) {
        $hdr = "${ESC}_Ga=T,f=32,s=$w,v=$h,C=1,q=1,m=$m;"
        $first = $false
    } else {
        $hdr = "${ESC}_Gm=$m;"
    }
    [Console]::Out.Write("$hdr$piece$ST")
}
[Console]::Out.Write("`n")
Write-Host "Red square here = kitty works in the ALTERNATE screen. Press Enter to exit."
[Console]::In.ReadLine() | Out-Null
[Console]::Out.Write("${ESC}[?1049l")
