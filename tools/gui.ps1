# Runs scripted mouse and keyboard actions in QEMU.
param(
    [string[]]$Steps,
    [string]$Img  = '',
    [string]$Out  = '',
    [int]$Boot    = 13,
    [int]$Port    = 4455,
    [string[]]$Extra = @(),
    [switch]$KeepRunning,
    [switch]$NoCopy
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Img) { $Img = Join-Path $root 'built\flopnix.img' }
if (-not $Out) { $Out = Join-Path $env:TEMP 'flopnix-gui' }
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not (Test-Path $Img)) { throw "no image at $Img" }

if (-not $NoCopy) {
    $work = Join-Path $Out 'run.img'
    Copy-Item -LiteralPath $Img -Destination $work -Force
    $Img = $work
}

Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

$qargs = @('-cpu','pentium2','-m','32','-fda',"`"$Img`"",'-boot','a',
           '-display','none','-qmp',"tcp:127.0.0.1:$Port,server,nowait",
           '-name','flopnix-gui') + $Extra
$p = Start-Process 'C:\Program Files\qemu\qemu-system-i386.exe' -ArgumentList $qargs -PassThru
Start-Sleep -Seconds $Boot

$c = New-Object Net.Sockets.TcpClient('127.0.0.1', $Port)
$s = $c.GetStream()
$r = New-Object IO.StreamReader($s)
$w = New-Object IO.StreamWriter($s); $w.AutoFlush = $true
$null = $r.ReadLine()
$w.WriteLine('{"execute":"qmp_capabilities"}'); $null = $r.ReadLine()

function Send($json) {
    $w.WriteLine($json)
    do { $resp = $r.ReadLine() } while ($resp -match '"event"')
    if ($resp -match '"error"') { Write-Output "QMP ERROR: $resp" }
}

function Key($qcode) {
    $keys = ($qcode -split '\+' | ForEach-Object {
        '{"type":"qcode","data":"' + $_ + '"}' }) -join ','
    Send ('{"execute":"send-key","arguments":{"keys":[' + $keys + ']}}')
    Start-Sleep -Milliseconds 75
}
$map = @{
 ' '='spc'; '.'='dot'; ','='comma'; '/'='slash'; '-'='minus'; '='='equal';
 ';'='semicolon'; "'"='apostrophe'; '['='bracket_left'; ']'='bracket_right';
 '\'='backslash'; '`'='grave_accent'
}

$shifted = @{
 '>'='dot'; '<'='comma'; '?'='slash'; ':'='semicolon'; '"'='apostrophe';
 '|'='backslash'; '~'='grave_accent'; '_'='minus'; '+'='equal';
 '{'='bracket_left'; '}'='bracket_right';
 '!'='1'; '@'='2'; '#'='3'; '$'='4'; '%'='5'; '^'='6'; '&'='7'; '*'='8';
 '('='9'; ')'='0'
}
function ShiftKey($qcode) {
    Send ('{"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"shift"},{"type":"qcode","data":"' + $qcode + '"}]}}')
    Start-Sleep -Milliseconds 75
}
function TypeStr($str) {
    foreach ($ch in $str.ToCharArray()) {
        $lc = [string]$ch
        if ($map.ContainsKey($lc)) { Key $map[$lc] }
        elseif ($shifted.ContainsKey($lc)) { ShiftKey $shifted[$lc] }
        elseif ($ch -cmatch '[A-Z]') { ShiftKey $lc.ToLower() }
        else { Key $lc }
    }
}

function MoveRel($dx, $dy) {
    $step = 100
    while ($dx -ne 0 -or $dy -ne 0) {
        $sx = [Math]::Max([Math]::Min($dx, $step), -$step)
        $sy = [Math]::Max([Math]::Min($dy, $step), -$step)
        $dx -= $sx; $dy -= $sy
        Send ('{"execute":"input-send-event","arguments":{"events":[' +
              '{"type":"rel","data":{"axis":"x","value":' + $sx + '}},' +
              '{"type":"rel","data":{"axis":"y","value":' + $sy + '}}]}}')
        Start-Sleep -Milliseconds 45
    }
    Start-Sleep -Milliseconds 60
}
function Btn($down, $btn = 'left') {
    Send ('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":' + $down + ',"button":"' + $btn + '"}}]}}')
    Start-Sleep -Milliseconds 90
}

$A_mx = 0; $A_my = 0
$elf = Join-Path $root 'out\kernel.elf'
$readelf = 'C:\msys64\clang64\bin\readelf.exe'
if ((Test-Path $elf) -and (Test-Path $readelf)) {
    & $readelf -sW $elf | ForEach-Object {
        if ($_ -match '^\s*\d+:\s+([0-9a-f]{8})\s+\d+\s+OBJECT\s+\S+\s+\S+\s+\S+\s+(mx|my)$') {
            if ($Matches[2] -eq 'mx') { $A_mx = [Convert]::ToUInt32($Matches[1], 16) }
            else                      { $A_my = [Convert]::ToUInt32($Matches[1], 16) }
        }
    }
}
function Hmp($cmd) {
    $w.WriteLine('{"execute":"human-monitor-command","arguments":{"command-line":"' + $cmd + '"}}')
    do { $resp = $r.ReadLine() } while ($resp -match '"event"')
    return $resp
}
function GuestPos() {
    $rx = Hmp ("xp/1wx 0x{0:x}" -f $A_mx)
    $ry = Hmp ("xp/1wx 0x{0:x}" -f $A_my)
    if ($rx -match ': 0x([0-9a-f]+)' ) { $gx = [int][Convert]::ToUInt32($Matches[1], 16) } else { return $null }
    if ($ry -match ': 0x([0-9a-f]+)' ) { $gy = [int][Convert]::ToUInt32($Matches[1], 16) } else { return $null }
    return @($gx, $gy)
}
function MoveVerified($dx, $dy, $tx, $ty) {

    MoveRel $dx $dy
    if ($A_mx -eq 0 -or $A_my -eq 0) { return }
    for ($try = 0; $try -lt 5; $try++) {
        $vp = GuestPos
        if ($null -eq $vp) { return }
        if ($tx -lt 0) { return }
        if ($vp[0] -eq $tx -and $vp[1] -eq $ty) { return }
        Start-Sleep -Milliseconds 80
        MoveRel ($tx - $vp[0]) ($ty - $vp[1])
    }
    Write-Output ("?? pointer stuck: wanted {0},{1} got {2},{3}" -f $tx, $ty, $vp[0], $vp[1])
}

foreach ($step in $Steps) {
    if ($step -match '^k:([^*]+)(\*(\d+))?$') {
        $n = if ($Matches[3]) { [int]$Matches[3] } else { 1 }
        for ($i = 0; $i -lt $n; $i++) { Key $Matches[1] }
    }
    elseif ($step -match '^t:(.*)$')       { TypeStr $Matches[1] }
    elseif ($step -match '^m:(-?\d+),(-?\d+)$') {
        $dx = [int]$Matches[1]; $dy = [int]$Matches[2]
        $tx = -1; $ty = -1
        if ($A_mx -ne 0 -and $A_my -ne 0) {
            $gp = GuestPos
            if ($null -ne $gp) { $tx = $gp[0] + $dx; $ty = $gp[1] + $dy }
        }
        MoveVerified $dx $dy $tx $ty
    }
    elseif ($step -eq 'home')              { MoveVerified (-2000) (-2000) 0 0 }
    elseif ($step -eq 'c')                 { Btn 'true'; Btn 'false' }
    elseif ($step -eq 'c:r')               { Btn 'true' 'right'; Btn 'false' 'right' }
    elseif ($step -eq 'c:d')               { Btn 'true'; Btn 'false'; Start-Sleep -Milliseconds 60; Btn 'true'; Btn 'false' }
    elseif ($step -eq 'down')              { Btn 'true' }
    elseif ($step -eq 'up')                { Btn 'false' }
    elseif ($step -match '^w:(\d+)$')      { Start-Sleep -Milliseconds ([int]$Matches[1]) }
    elseif ($step -match '^shot:(.+)$') {
        $f = (Join-Path $Out ($Matches[1] + '.ppm')) -replace '\\','/'
        Send ('{"execute":"screendump","arguments":{"filename":"' + $f + '"}}')
        Start-Sleep -Milliseconds 500
        Write-Output "shot -> $f"
    }
    else { Write-Output "?? unknown step: $step" }
}
$c.Close()
if (-not $KeepRunning) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
Write-Output "done (out: $Out)"
