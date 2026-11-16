param([double]$Hours = 10, [int]$IntervalMs = 5, [switch]$Status)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'session_control.ps1')

if ([double]::IsNaN($Hours) -or $Hours -le 0 -or $Hours -gt 168 -or $IntervalMs -lt 2 -or $IntervalMs -gt 1000) { throw 'Invalid monitoring duration/interval.' }
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/Toolchain.ps1')
$python = Resolve-CopydogPython
$helper = Join-Path $PSScriptRoot 'latency_etw\bin\Release\net10.0-windows\LatencyEtw.exe'
if (-not [IO.File]::Exists($python)) { throw 'Monitor Python executable not found.' }
$env:PYTHONUTF8 = '1'
if ($Status) {
    & $python -B (Join-Path $PSScriptRoot 'parry_status.py')
    if ($LASTEXITCODE) { throw "Parry status reader exited with code $LASTEXITCODE." }
    return
}
$hoursArgument = $Hours.ToString([Globalization.CultureInfo]::InvariantCulture)
$children = [Collections.Generic.List[Diagnostics.Process]]::new()
$workers = @{}

function Start-CaptureWorker([string]$Role, [string]$File, [string[]]$Arguments) {
    if ([IO.File]::Exists($result.stop_file)) { throw 'Session already stopped.' }
    $raw = & $python -B (Join-Path $PSScriptRoot 'launch_worker.py') $folder $Role $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Could not start the $Role worker." }
    $identity = $raw | ConvertFrom-Json
    $result["${Role}_pid"] = $identity.pid
    $workers[$Role] = $identity
    Write-MonitorJson (Join-Path $folder 'workers.json') $workers
    $worker = [Diagnostics.Process]::GetProcessById([int]$identity.pid)
    $children.Add($worker)
    $null = $worker.Handle
    if ([string]$worker.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $identity.started_filetime) {
        throw "$Role worker exited before readiness."
    }
    return $worker
}

function Wait-CaptureReady($Worker, [string]$StatusFile, [int]$Seconds, [scriptblock]$Ready) {
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    while ($elapsed.Elapsed.TotalSeconds -lt $Seconds) {
        $Worker.Refresh()
        if ($Worker.HasExited) { throw "Worker $($Worker.Id) exited before reporting readiness to $StatusFile" }
        if ([IO.File]::Exists($result.stop_file)) { throw 'Session ended during startup.' }
        if ([IO.File]::Exists($StatusFile)) {
            $snapshot = $null
            try { $snapshot = Get-Content -LiteralPath $StatusFile -Raw | ConvertFrom-Json } catch {}
            if ($snapshot -and (& $Ready $snapshot)) { return $snapshot }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Worker $($Worker.Id) did not report readiness to $StatusFile"
}

$control = Enter-MonitorControl $PSScriptRoot
$folder = $null
try {
    $path = Join-Path $PSScriptRoot ('sessions\capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $folder = (New-Item -ItemType Directory -Path $path).FullName
    $result = @{ folder=$folder; hours=$Hours; stop_file=(Join-Path $folder 'STOP') }
    $monitorArgs = @('-B',(Join-Path $PSScriptRoot 'monitor.py'), '--hours', $hoursArgument, '--interval-ms', "$IntervalMs", '--output', $folder)
    $monitor = Start-CaptureWorker 'monitor' $python $monitorArgs
    $captureReady = Wait-CaptureReady $monitor (Join-Path $folder 'latest.json') 4 { param($s) $s.running -and $s.monitor_pid -eq $monitor.Id }
    $result.deadline = ([DateTimeOffset]$captureReady.deadline).ToString('o')

    $shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $networkArgs = @('--hours', $hoursArgument, '--output', (Join-Path $folder 'network'), '--stop-file', $result.stop_file)
    $displayArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'capture_display_latency.ps1'),'-Session',$folder,'-Deadline',$result.deadline)
    $sidecars = @(
        @{ name='network'; executable=$helper; arguments=$networkArgs; status='network\status.json'; timeout=10; ready={ param($s) $s.running } },
        @{ name='display'; executable=$shell; arguments=$displayArgs; status='display_latency\sidecar_pid.json'; timeout=4; ready={ param($s) $s.pid -eq $child.Id } }
    )
    foreach ($name in @('action_names','ranged')) {
        $scriptName = if ($name -eq 'action_names') { 'capture_action_names.py' } else { 'ranged_watch.py' }
        $arguments = @('-B',(Join-Path $PSScriptRoot $scriptName),'--session',$folder)
        $sidecars += @{ name=$name; executable=$python; arguments=$arguments; status="${name}_status.json"; timeout=5; ready={ param($s) $s.running -and $s.pid -eq $child.Id } }
    }
    foreach ($sidecar in $sidecars) {
        $name = $sidecar.name
        $result["${name}_pid"] = $null
        $result["${name}_startup_error"] = $null
        try {
            $child = Start-CaptureWorker $name $sidecar.executable $sidecar.arguments
            $null = Wait-CaptureReady $child (Join-Path $folder $sidecar.status) $sidecar.timeout $sidecar.ready
        } catch {
            if ([IO.File]::Exists($result.stop_file)) { throw }
            $result["${name}_startup_error"] = $_.Exception.Message
            Write-Warning "$name did not report readiness: $($_.Exception.Message)"
        }
    }
    Write-MonitorJson (Join-Path $PSScriptRoot 'active_session.json') $result
    $result | ConvertTo-Json
} catch {
    if ($folder) { [IO.File]::WriteAllText((Join-Path $folder 'STOP'), '') }
    throw
} finally {
    foreach ($child in $children) { $child.Dispose() }
    $control.Dispose()
}
