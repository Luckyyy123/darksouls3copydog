param(
    [Parameter(Mandatory=$true)][string]$Session,
    [string]$Deadline = ([DateTimeOffset]::Now.AddHours(10).ToString('o'))
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$sessionRoot = [IO.Path]::GetFullPath($Session)
$output = Join-Path $sessionRoot 'display_latency'
[IO.Directory]::CreateDirectory($output) | Out-Null
$stopFile = Join-Path $sessionRoot 'STOP'
$deadlineTime = [DateTimeOffset]::Parse($Deadline)
$encoding = New-Object System.Text.UTF8Encoding($false)
$latest = Join-Path $output 'latest.json'
$journal = New-Object IO.StreamWriter((Join-Path $output 'display_latency.jsonl'), $true, $encoding)
$journal.AutoFlush = $true
$runId = 'display_' + $PID + '_' + [DateTime]::UtcNow.Ticks
$sequence = 0
$gridCondition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::AutomationIdProperty, 'dataGridSession')
$rowCondition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::DataItem)
$textCondition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Text)

function Write-Snapshot($value) {
    $json = $value | ConvertTo-Json -Depth 8 -Compress
    $journal.WriteLine($json)
    $temp = $latest + '.' + $PID + '.tmp'
    [IO.File]::WriteAllText($temp, $json, $encoding)
    for ($attempt = 0; ; $attempt++) {
        try {
            if ([IO.File]::Exists($latest)) { [IO.File]::Replace($temp, $latest, [NullString]::Value) }
            else { [IO.File]::Move($temp, $latest) }
            return
        } catch {
            $cause = $_.Exception.GetBaseException()
            if ($attempt -ge 40 -or -not ($cause -is [IO.IOException] -or $cause -is [UnauthorizedAccessException])) { throw }
            Start-Sleep -Milliseconds 25
        }
    }
}

function Read-CellText($cell) {
    $pattern = $null
    if ($cell.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
        return [string]$pattern.Current.Value
    }
    $text = $cell.FindFirst([Windows.Automation.TreeScope]::Descendants, $textCondition)
    if ($null -ne $text) { return [string]$text.Current.Name }
    return $null
}

function Read-Display {
    $peers = New-Object 'System.Collections.Generic.List[object]'
    $processes = @(Get-Process -Name DS3ConnectionInfo -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) { return @{ state='waiting_for_tool'; peers=@(); process_id=$null } }
    try {
        foreach ($process in $processes) {
            if ($process.MainWindowHandle -eq [IntPtr]::Zero) { continue }
            $window = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
            $grid = $window.FindFirst([Windows.Automation.TreeScope]::Descendants, $gridCondition)
            if ($null -eq $grid) { continue }
            $rows = $grid.FindAll([Windows.Automation.TreeScope]::Children, $rowCondition)
            for ($r = 0; $r -lt $rows.Count; $r++) {
                $slotText = $null
                $pingText = $null
                $cells = $rows[$r].FindAll([Windows.Automation.TreeScope]::Children, [Windows.Automation.Condition]::TrueCondition)
                for ($c = 0; $c -lt $cells.Count; $c++) {
                    $gridItem = $null
                    if (-not $cells[$c].TryGetCurrentPattern([Windows.Automation.GridItemPattern]::Pattern, [ref]$gridItem)) { continue }
                    if ($gridItem.Current.Column -eq 0) { $slotText = Read-CellText $cells[$c] }
                    # This tool hides columns: visible Ping index 5 is underlying GridItem column 6.
                    if ($gridItem.Current.Column -eq 6) { $pingText = Read-CellText $cells[$c] }
                }
                $ping = 0
                $parsed = [int]::TryParse($pingText, [ref]$ping)
                $valid = $parsed -and $ping -ge 0
                $displayValue = $null
                if ($valid) { $displayValue = $ping }
                $peers.Add([ordered]@{
                    ui_row=$r; ui_slot=$slotText; ping_ms_display=$displayValue; ping_display_text=$pingText; valid=$valid
                    validity=$(if ($valid) {'display_value_available'} else {'unknown'})
                    actor_mapping='unconfirmed'; original_sample_age_ms=$null
                    source='ds3connectioninfo_ui_display'; display_precision='integer_ms'
                    ui_ping_grid_column=6
                })
            }
            return @{ state='capturing_display'; peers=@($peers.ToArray()); process_id=$process.Id }
        }
    } finally {
        foreach ($process in $processes) { $process.Dispose() }
    }
    return @{ state='waiting_for_grid'; peers=@(); process_id=$null }
}

function New-Snapshot($state, $running, $peers, $toolPid, $detail) {
    $script:sequence++
    return [ordered]@{
        type='display_latency_snapshot'; display_snapshot_id=($runId + ':' + $sequence)
        updated_wall=[DateTimeOffset]::Now.ToString('o')
        updated_mono_ns=[long]([Diagnostics.Stopwatch]::GetTimestamp() * (1000000000.0 / [Diagnostics.Stopwatch]::Frequency))
        source='ds3connectioninfo_ui_display'; running=$running; state=$state
        tool_process_id=$toolPid; peers=@($peers); detail=$detail
        deadline=$deadlineTime.ToString('o'); stop_file=$stopFile
        original_sample_age_ms=$null; actor_mapping='unconfirmed'; display_precision='integer_ms'
        windows_activated=$false; packets_sent=$false; game_memory_written=$false
    }
}

[IO.File]::WriteAllText((Join-Path $output 'sidecar_pid.json'), (@{
    pid=$PID; started_wall=[DateTimeOffset]::Now.ToString('o'); source='ds3connectioninfo_ui_display'
    session=$sessionRoot; deadline=$deadlineTime.ToString('o')
} | ConvertTo-Json -Compress), $encoding)
try {
    while (-not [IO.File]::Exists($stopFile) -and [DateTimeOffset]::Now -lt $deadlineTime) {
        try {
            $data = Read-Display
            $snapshot = New-Snapshot $data.state $true $data.peers $data.process_id $null
        } catch {
            $snapshot = New-Snapshot 'ui_read_error_retrying' $true @() $null $_.Exception.GetBaseException().Message
        }
        Write-Snapshot $snapshot
        Start-Sleep -Milliseconds 1000
    }
    $reason = if ([IO.File]::Exists($stopFile)) {'stop_file'} else {'deadline'}
    Write-Snapshot (New-Snapshot 'stopped' $false @() $null $reason)
} catch {
    $failure = New-Snapshot 'error' $false @() $null $_.Exception.ToString()
    $json = $failure | ConvertTo-Json -Depth 8 -Compress
    [Console]::Error.WriteLine($json)
    try { $journal.WriteLine($json) } catch { [Console]::Error.WriteLine($_.Exception.ToString()) }
    exit 6
} finally { $journal.Dispose() }
