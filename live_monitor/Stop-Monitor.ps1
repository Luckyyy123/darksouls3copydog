param([ValidateRange(1,300)][int]$WaitSeconds = 20)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'session_control.ps1')

function Read-SessionJson([string]$Path) {
    if ([IO.File]::Exists($Path)) { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    return $null
}

$control = Enter-MonitorControl $PSScriptRoot
try {
    $info = Read-SessionJson (Join-Path $PSScriptRoot 'active_session.json')
    if (-not $info) { Write-Output 'No monitor session has been started.'; return }
    $allowed = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'sessions'))
    $folder = [IO.Path]::GetFullPath($info.folder)
    if (-not [string]::Equals([IO.Path]::GetDirectoryName($folder), $allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Session directory is outside this monitor workspace.'
    }
    [IO.File]::WriteAllText((Join-Path $folder 'STOP'), '')
    $workers = Read-SessionJson (Join-Path $folder 'workers.json')
    $settings = @(
        @{name='monitor'; status='latest.json'; id='monitor_pid'; script='monitor.py'},
        @{name='network'; script='LatencyEtw.exe'},
        @{name='display'; status='display_latency\sidecar_pid.json'; id='pid'; script='capture_display_latency.ps1'},
        @{name='action_names'; status='action_names_status.json'; id='pid'; script='capture_action_names.py'},
        @{name='ranged'; status='ranged_status.json'; id='pid'; script='ranged_watch.py'}
    )
    $outcomes = [ordered]@{}
    $pending = @()
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    foreach ($setting in $settings) {
        $role = $setting.name
        $identity = $workers.$role
        $workerId = $identity.pid
        if (-not $workerId) { $workerId = $info."${role}_pid" }
        if (-not $workerId -and $setting.status) {
            $snapshot = Read-SessionJson (Join-Path $folder $setting.status)
            $workerId = $snapshot.($setting.id)
        }
        if (-not $workerId -or $identity.exited) { $outcomes[$role]='inactive'; continue }
        try {
            $process = [Diagnostics.Process]::GetProcessById([int]$workerId)
        } catch [ArgumentException] {
            $outcomes[$role]='exited'; continue
        }
        try {
            # Pin the process object before comparing creation time and waiting.
            $null = $process.Handle
            if ($process.HasExited) { $outcomes[$role]='exited'; continue }
            if ($identity.started_filetime) {
                if ([string]$process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $identity.started_filetime) {
                    $outcomes[$role]='pid_reused'; continue
                }
            } else {
                # Older recordings lack creation times. Verify both the worker and its session argument.
                $instance = Get-CimInstance Win32_Process -Filter "ProcessId=$workerId"
                if ($process.HasExited) { $outcomes[$role]='exited'; continue }
                if (-not $instance.CommandLine) { throw 'Cannot identify this legacy worker.' }
                $sessionPattern = '(?i)(?:--output|--session|-Session)\s+"?' + [regex]::Escape($folder) + '(?:\\network)?(?:"|\s|$)'
                if ($instance.CommandLine -notmatch $sessionPattern -or
                    $instance.CommandLine.IndexOf($setting.script,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
                    $outcomes[$role]='pid_reused'; continue
                }
            }
            $remaining = [Math]::Max(0, [int]($WaitSeconds*1000 - $elapsed.ElapsedMilliseconds))
            if ($process.WaitForExit($remaining)) { $outcomes[$role]='exited' }
            else { $outcomes[$role]='timeout'; $pending += "$role ($workerId)" }
        } catch {
            # A worker may exit between the PID lookup and the first handle acquisition.
            if ($process.HasExited) { $outcomes[$role]='exited' }
            else { $outcomes[$role]=$_.Exception.Message; $pending += "$role ($workerId)" }
        } finally {
            $process.Dispose()
        }
    }
    [ordered]@{folder=$folder; stopped=($pending.Count -eq 0); workers=$outcomes} | ConvertTo-Json
    if ($pending.Count) { throw ('Graceful exit unconfirmed for ' + ($pending -join ', ')) }
} finally {
    $control.Dispose()
}
