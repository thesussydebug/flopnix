# Builds and runs the guest test suite on an image copy.
param([int]$BootSeconds = 14, [string]$Cpu = 'pentium2')
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$rootU = $root -replace '\\','/'
$img   = Join-Path $root 'built\flopnix.img'
$tmp   = Join-Path $env:TEMP 'flopnix-tests'
New-Item -ItemType Directory -Force $tmp | Out-Null
$testimg  = Join-Path $tmp 'test.img'
$results  = Join-Path $tmp 'results.txt'
$resultsU = $results -replace '\\','/'
Remove-Item $results -ErrorAction SilentlyContinue

if (-not (Test-Path $img)) { throw "no built/flopnix.img - run build.sh first" }

Copy-Item $img $testimg -Force
$bash = 'C:\msys64\usr\bin\bash.exe'
$cmd  = "export PATH=/c/msys64/clang64/bin:/c/msys64/usr/bin:`$PATH; cd '$rootU' && " +
        "bash tools/mkkext.sh kexts/selftest.c && " +
        "python tools/fscp.py '$($testimg -replace '\\','/')' kexts/selftest.kx sys/selftest.kx"
& $bash --noprofile --norc -c $cmd
if ($LASTEXITCODE -ne 0) { throw "build/install of selftest.kx failed" }

$qargs = @('-cpu',$Cpu,'-m','32','-fda',$testimg,'-boot','a',
           '-chardev',"file,id=ser0,path=$resultsU",'-serial','chardev:ser0',
           '-display','none','-name','flopnix-tests')
$qemu = 'C:\msys64\clang64\bin\qemu-system-i386.exe'
if (-not (Test-Path $qemu)) { $qemu = 'C:\Program Files\qemu\qemu-system-i386.exe' }
if (-not (Test-Path $qemu)) { $qemu = 'qemu-system-i386' }
$p = Start-Process $qemu -ArgumentList $qargs -PassThru -WindowStyle Hidden
try { Start-Sleep -Seconds $BootSeconds }
finally { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

Write-Output "===== selftest results ====="
if (Test-Path $results) {
    $txt = (Get-Content $results -Raw) -replace "`r",''
    Write-Output $txt
    if ($txt -match 'SELFTEST DONE: \d+ pass (\d+) fail') {
        if ([int]$Matches[1] -ne 0) { throw "$($Matches[1]) tests failed" }
        Write-Output 'RESULT: ALL PASS'
    } else { throw 'Incomplete test run: no SELFTEST DONE line' }
} else { throw 'No serial output from the test guest' }
