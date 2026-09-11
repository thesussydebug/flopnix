# Checks the app watchdog in a disposable guest.
param(
    [string]$Only = '',
    [switch]$KeepRunning,
    [int]$Port = 4455
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$elf  = Join-Path $root 'out\kernel.elf'
$img  = Join-Path $root 'built\flopnix.img'
$readelf = 'C:\msys64\clang64\bin\readelf.exe'

$SYM = @{}; $SYMSZ = @{}
& $readelf -sW $elf | ForEach-Object {
    if ($_ -match '^\s*\d+:\s+([0-9a-f]{8})\s+(\d+)\s+OBJECT\s+\S+\s+\S+\s+\S+\s+(\S+)$') {
        $SYM[$Matches[3]]   = [Convert]::ToUInt32($Matches[1], 16)
        $SYMSZ[$Matches[3]] = [int]$Matches[2]
    }
}
function Sym($n) {
    if (-not $SYM.ContainsKey($n)) { throw "symbol not found: $n" }
    return $SYM[$n]
}
$A_ticks   = Sym 'ticks'
$A_runwin  = Sym 'run_win'
$A_runt0   = Sym 'run_t0'
$A_killwin = Sym 'kill_win'
$A_askwin  = Sym 'hang_ask_win'
$A_hwwin   = Sym 'hangw.0'
$A_hwnext  = Sym 'hangw.1'

$A_hwprom = 0
foreach ($k in $SYM.Keys) { if ($k -like 'hangw.*' -and $SYMSZ[$k] -eq 1) { $A_hwprom = $SYM[$k] } }
if ($A_hwprom -eq 0) { throw "could not find the 1-byte hangw.prompted field" }
$A_close   = Sym 'close_pending'
$A_ovdraw  = Sym 'ov_draw'
$A_wins    = Sym 'wins'
$A_recov   = Sym 'fault_recoveries'
$A_mx      = Sym 'mx'
$A_my      = Sym 'my'
$A_hangend = Sym 'hang_ended'
Write-Output "symbols ok (wins=0x$('{0:x}' -f $A_wins) run_win=0x$('{0:x}' -f $A_runwin))"

Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
$qargs = @('-cpu','pentium2','-m','32','-fda',"`"$img`"",'-boot','a',
           '-display','none','-qmp',"tcp:127.0.0.1:$Port,server,nowait",
           '-name','flopnix-hangtest')
$proc = Start-Process 'C:\Program Files\qemu\qemu-system-i386.exe' -ArgumentList $qargs -PassThru
Start-Sleep -Seconds 14

$cli = New-Object Net.Sockets.TcpClient('127.0.0.1', $Port)
$stm = $cli.GetStream()
$rd  = New-Object IO.StreamReader($stm)
$wr  = New-Object IO.StreamWriter($stm); $wr.AutoFlush = $true
$null = $rd.ReadLine()
$wr.WriteLine('{"execute":"qmp_capabilities"}'); $null = $rd.ReadLine()

function Qmp($json) {
    $wr.WriteLine($json)
    do { $r = $rd.ReadLine() } while ($r -match '"event"')
    return $r
}
function Hmp($cmd) {
    return Qmp ('{"execute":"human-monitor-command","arguments":{"command-line":"' + $cmd + '"}}')
}
function Rd32([uint32]$addr) {
    $r = Hmp ("xp/1wx 0x{0:x}" -f $addr)
    if ($r -match ': 0x([0-9a-f]+)') { return [Convert]::ToUInt32($Matches[1], 16) }
    throw "bad read at 0x$('{0:x}' -f $addr): $r"
}
function RdI32([uint32]$addr) {
    $v = Rd32 $addr
    if ($v -gt 2147483647) { return [int]($v - 4294967296) }
    return [int]$v
}
function Rd8([uint32]$addr) {
    $r = Hmp ("xp/1bx 0x{0:x}" -f $addr)
    if ($r -match ': 0x([0-9a-f]+)') { return [int][Convert]::ToUInt32($Matches[1], 16) }
    throw "bad byte read"
}
function RdStr([uint32]$addr, [int]$max = 32) {
    if ($addr -eq 0) { return '' }
    $r = Hmp ("xp/{0}bx 0x{1:x}" -f $max, $addr)
    $out = ''
    foreach ($m in [regex]::Matches($r, '0x([0-9a-f]{2})\b')) {
        $b = [Convert]::ToInt32($m.Groups[1].Value, 16)
        if ($b -eq 0) { break }
        $out += [char]$b
    }
    return $out
}

function WinRect([int]$i) {
    $b = $A_wins + [uint32](52 * $i)
    $used = Rd8 $b
    if ($used -eq 0) { return $null }
    $title = RdStr (Rd32 ($b + 20))
    if ((Rd8 ($b + 48)) -ne 0) { $title = RdStr ($b + 24) }
    return @{ idx = $i; x = RdI32 ($b + 4); y = RdI32 ($b + 8)
              w = RdI32 ($b + 12); h = RdI32 ($b + 16); title = $title }
}
function FindWin([string]$name) {
    for ($i = 0; $i -lt 12; $i++) {
        $r = WinRect $i
        if ($r -ne $null -and $r.title -like "*$name*") { return $r }
    }
    return $null
}
function WinUsed([int]$i) { return (Rd8 ($A_wins + [uint32](52 * $i))) }

function SendRel($dx, $dy) {
    $null = Qmp ('{"execute":"input-send-event","arguments":{"events":[' +
        '{"type":"rel","data":{"axis":"x","value":' + $dx + '}},' +
        '{"type":"rel","data":{"axis":"y","value":' + $dy + '}}]}}')
    Start-Sleep -Milliseconds 70
}

function MoveTo([int]$tx, [int]$ty) {
    for ($try = 0; $try -lt 14; $try++) {
        $cx = RdI32 $A_mx; $cy = RdI32 $A_my
        $dx = $tx - $cx; $dy = $ty - $cy
        if ($dx -eq 0 -and $dy -eq 0) { return $true }
        if ([Math]::Abs($dx) -gt 40) { $dx = 40 * [Math]::Sign($dx) }
        if ([Math]::Abs($dy) -gt 40) { $dy = 40 * [Math]::Sign($dy) }
        SendRel $dx $dy
    }
    $cx = RdI32 $A_mx; $cy = RdI32 $A_my
    Write-Output ("  ! MoveTo({0},{1}) ended at ({2},{3})" -f $tx,$ty,$cx,$cy)
    return $false
}
function ClickNow() {
    $null = Qmp '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
    Start-Sleep -Milliseconds 110
    $null = Qmp '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
    Start-Sleep -Milliseconds 140
}

function ClickUntil([int]$x, [int]$y, [scriptblock]$done, [int]$tries = 5) {
    for ($t = 0; $t -lt $tries; $t++) {
        $null = MoveTo $x $y
        ClickNow
        Start-Sleep -Milliseconds 350
        if (& $done) { return $true }
    }
    return $false
}
function WaitFor([scriptblock]$cond, [int]$ms) {
    $spent = 0
    while ($spent -lt $ms) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 200; $spent += 200 }
    return $false
}

$script:pass = 0; $script:fail = 0
function OK($name, $cond, $detail = '') {
    if ($cond) { $script:pass++; Write-Output "  ok   $name" }
    else { $script:fail++; Write-Output "  FAIL $name $detail" }
}
function Phase($n) { Write-Output ""; Write-Output "== $n" }

function OpenApp([string]$name, [int]$subY) {
    $null = MoveTo 30 466; ClickNow; Start-Sleep -Milliseconds 400
    $null = MoveTo 70 394; Start-Sleep -Milliseconds 300
    $null = MoveTo 200 394; Start-Sleep -Milliseconds 300
    $null = MoveTo 200 $subY; Start-Sleep -Milliseconds 300
    ClickNow; Start-Sleep -Milliseconds 900
    return (FindWin $name)
}

function CsButton($win, [int]$i) {
    $cx = $win.x + 3; $cy = $win.y + 22
    return @{ x = $cx + 12 + 100; y = $cy + 40 + $i * 26 + 11 }
}
$IDX_SLOW = 6; $IDX_HANG = 7

Write-Output "=== FLOPNIX hang-watchdog test loop ==="
$w = OpenApp 'Crash Test' 343
if ($w -eq $null) { Write-Output "FATAL: could not open Crash Test"; exit 99 }
Write-Output ("Crash Test at ({0},{1}) {2}x{3} slot {4}" -f $w.x,$w.y,$w.w,$w.h,$w.idx)
$CSIDX = $w.idx

Phase 'S1  hang is detected and the dialog opens'
$recov0 = Rd32 $A_recov
$hb = CsButton $w $IDX_HANG
$null = MoveTo $hb.x $hb.y
ClickNow
$got = WaitFor { (RdI32 $A_runwin) -eq $CSIDX } 3000
OK 'handler is in flight (run_win == Crash Test)' $got
OK 'no dialog yet at t<3s' ((RdI32 $A_askwin) -lt 0)
$el = (Rd32 $A_ticks) - (Rd32 $A_runt0)
OK 'elapsed under the 300-tick threshold so far' ($el -lt 300) "elapsed=$el"
$asked = WaitFor { (RdI32 $A_askwin) -eq $CSIDX } 6000
OK 'watchdog raised the prompt' $asked
OK 'a modal overlay is really up (ov_draw != 0)' ((Rd32 $A_ovdraw) -ne 0)
OK 'hangw latched prompted=1' ((Rd8 $A_hwprom) -eq 1)
OK 'title would show (not responding): elapsed >= 300' (((Rd32 $A_ticks) - (Rd32 $A_runt0)) -ge 300)
OK 'no kill requested yet' ((RdI32 $A_killwin) -lt 0)
OK 'the app is NOT closed while we ask' ((WinUsed $CSIDX) -eq 1)

Phase 'S2  No/Wait re-arms and the watchdog asks AGAIN (never tested before)'

$noOk = ClickUntil 356 277 { (RdI32 (Sym 'hang_ask_win')) -lt 0 } 5
OK 'No/Wait dismissed the dialog' $noOk
OK 'overlay cleared' ((Rd32 $A_ovdraw) -eq 0)
OK 'hw_rearm cleared prompted' ((Rd8 $A_hwprom) -eq 0)
OK 'still hung, still not killed' (((RdI32 $A_runwin) -eq $CSIDX) -and ((WinUsed $CSIDX) -eq 1))
$again = WaitFor { (RdI32 $A_askwin) -eq $CSIDX } 6000
OK 'watchdog asked a SECOND time after Wait' $again
OK 'second dialog is really drawn' ((Rd32 $A_ovdraw) -ne 0)

Phase 'S3  Yes ends the task and unwinds the worker'
$yesOk = ClickUntil 284 277 { (RdI32 (Sym 'hang_ask_win')) -lt 0 } 5
OK 'Yes was accepted' $yesOk
$killed = WaitFor { (WinUsed $CSIDX) -eq 0 } 4000
OK 'the hung window is gone' $killed
OK 'worker is free again (run_win == -1)' ((RdI32 $A_runwin) -lt 0)
OK 'kill request consumed (kill_win == -1)' ((RdI32 $A_killwin) -lt 0)
OK 'hang_ended flag was consumed, not left set' ((Rd8 $A_hangend) -eq 0)
OK 'deferred close drained (close_pending == -1)' ((RdI32 $A_close) -lt 0)
OK 'exactly one recovery was recorded' ((Rd32 $A_recov) -eq ($recov0 + 1)) ("was $recov0 now " + (Rd32 $A_recov))

Phase 'S4  the desktop is genuinely alive after the kill'

$w4 = OpenApp 'Crash Test' 343
OK 'Crash Test reopened after the kill' ($w4 -ne $null)
if ($w4 -ne $null) {
    $sb = CsButton $w4 $IDX_SLOW
    $null = MoveTo $sb.x $sb.y
    ClickNow
    $busy = WaitFor { (RdI32 $A_runwin) -eq $w4.idx } 3000
    OK 'the worker picked up a new event (run_win went busy)' $busy
    $done = WaitFor { (RdI32 $A_runwin) -lt 0 } 6000
    OK 'the 2s handler RAN TO COMPLETION and the worker went idle' $done
    OK 'no watchdog prompt for a 2s handler (under the 3s threshold)' ((RdI32 $A_askwin) -lt 0)
    OK 'no extra recovery was recorded by the slow handler' ((Rd32 $A_recov) -eq ($recov0 + 1))
}

Phase 'S5  a second hang in the same session still works'
$w2 = $w4
if ($w2 -eq $null) { $w2 = FindWin 'Crash Test' }
if ($w2 -eq $null) { $w2 = OpenApp 'Crash Test' 343 }
if ($w2 -ne $null) {
    $CS2 = $w2.idx
    $recov1 = Rd32 $A_recov
    $hb2 = CsButton $w2 $IDX_HANG
    $null = MoveTo $hb2.x $hb2.y
    ClickNow
    $h2 = WaitFor { (RdI32 $A_runwin) -eq $CS2 } 3000
    OK 'second hang wedges the worker again' $h2
    $ask2 = WaitFor { (RdI32 $A_askwin) -eq $CS2 } 6000
    OK 'watchdog prompts for the second hang too' $ask2
    $yes2 = ClickUntil 284 277 { (RdI32 (Sym 'hang_ask_win')) -lt 0 } 5
    OK 'second Yes accepted' $yes2
    $k2 = WaitFor { (WinUsed $CS2) -eq 0 } 4000
    OK 'second hung window is gone' $k2
    OK 'second recovery recorded' ((Rd32 $A_recov) -eq ($recov1 + 1))
} else {
    OK 'could not reopen Crash Test for the second hang' $false
}

Phase 'S6  kill a wedge that is INSIDE gui_pump (the realistic device-wait case)'

$A_present = Sym 'presenting'
if ($SYM.ContainsKey('pump_inside')) { $A_inside = Sym 'pump_inside' }
else { $A_inside = Sym 'gui_pump.inside' }
$A_noprmpt = Sym 'nopreempt'
$w6 = FindWin 'Crash Test'
if ($w6 -eq $null) { $w6 = OpenApp 'Crash Test' 343 }
if ($w6 -eq $null) {
    OK 'could not open Crash Test for S6' $false
} else {
    $CS6 = $w6.idx
    $recovB = Rd32 $A_recov
    $pb = CsButton $w6 8
    $null = MoveTo $pb.x $pb.y
    ClickNow
    $busy6 = WaitFor { (RdI32 $A_runwin) -eq $CS6 } 3000
    OK 'the pumping wedge is in flight' $busy6

    OK 'S6-pre a pumping wedge is NOT accused at 3s (progress counts)' `
       (-not (WaitFor { (RdI32 $A_askwin) -eq $CS6 } 6000))

    $ask6 = WaitFor { (RdI32 $A_askwin) -eq $CS6 } 200000
    OK 'the hard ceiling still catches a pumping wedge' $ask6
    $yes6 = ClickUntil 284 277 { (RdI32 (Sym 'hang_ask_win')) -lt 0 } 6
    OK 'Yes accepted for the pumping wedge' $yes6
    Start-Sleep -Milliseconds 1500
    OK 'S6a the correct window was closed' ((WinUsed $CS6) -eq 0)
    OK 'S6b worker is idle again' ((RdI32 $A_runwin) -lt 0)
    OK 'S6c present lock was released (presenting == 0)' ((Rd8 $A_present) -eq 0)
    OK 'S6d gui_pump re-entry latch released (inside == 0)' ((Rd8 $A_inside) -eq 0)
    OK 'S6e preempt depth back to 0 (nopreempt == 0)' ((Rd32 $A_noprmpt) -eq 0)
    OK 'S6f exactly one recovery recorded' ((Rd32 $A_recov) -eq ($recovB + 1))

    $t1 = Rd32 $A_ticks
    Start-Sleep -Milliseconds 800
    OK 'S6g the kernel is still ticking' ((Rd32 $A_ticks) -gt $t1)
    $null = MoveTo 400 200
    Start-Sleep -Milliseconds 500
    OK 'S6h the compositor still presents (cursor still tracked)' ((RdI32 $A_mx) -eq 400)
}

Phase 'S7  FALSE POSITIVE: 5s of honest work that pumps (a file copy, a scan)'
$w7 = FindWin 'Crash Test'
if ($w7 -eq $null) { $w7 = OpenApp 'Crash Test' 343 }
if ($w7 -eq $null) {
    OK 'could not open Crash Test for S7' $false
} else {
    $CS7 = $w7.idx
    $recovC = Rd32 $A_recov
    $sp7 = CsButton $w7 9
    $null = MoveTo $sp7.x $sp7.y
    ClickNow
    $b7 = WaitFor { (RdI32 $A_runwin) -eq $CS7 } 3000
    OK 'the honest 5s job is running' $b7

    $accused = WaitFor { (RdI32 $A_askwin) -eq $CS7 } 5000
    if ($accused) {
        Write-Output "  ** FALSE POSITIVE CONFIRMED: a working app was accused of hanging"
    }
    OK 'S7a a healthy app is NOT accused of hanging' (-not $accused)

    $fin = WaitFor { (RdI32 $A_runwin) -lt 0 } 12000
    OK 'S7b the honest job ran to completion' $fin
    OK 'S7c its window is still open (nothing was killed)' ((WinUsed $CS7) -eq 1)
    OK 'S7d no recovery was recorded for honest work' ((Rd32 $A_recov) -eq $recovC)

    if ((RdI32 $A_askwin) -ge 0) { $null = ClickUntil 356 277 { (RdI32 (Sym 'hang_ask_win')) -lt 0 } 4 }
}

Write-Output ""
Write-Output "RESULT: $($script:pass) pass $($script:fail) fail"
$cli.Close()
if (-not $KeepRunning) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
exit $script:fail
