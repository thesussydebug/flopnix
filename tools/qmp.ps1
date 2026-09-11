# Sends commands to a running QEMU instance.
param(
    [string[]]$Commands,
    [int]$Port = 4455
)
$c = New-Object Net.Sockets.TcpClient("127.0.0.1", $Port)
$s = $c.GetStream()
$r = New-Object IO.StreamReader($s)
$w = New-Object IO.StreamWriter($s)
$w.AutoFlush = $true
$null = $r.ReadLine()
$w.WriteLine('{"execute":"qmp_capabilities"}')
$null = $r.ReadLine()
foreach ($cmd in $Commands) {
    $w.WriteLine($cmd)
    do { $resp = $r.ReadLine() } while ($resp -match '"event"')
    Write-Output $resp
    Start-Sleep -Milliseconds 80
}
$c.Close()
