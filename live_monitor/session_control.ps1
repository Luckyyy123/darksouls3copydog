function Enter-MonitorControl([string]$Root) {
    $path = Join-Path $Root 'capture-control.lock'
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        try {
            return [IO.File]::Open($path, 'OpenOrCreate', 'ReadWrite', 'None')
        } catch [IO.IOException] {
            if ($elapsed.Elapsed.TotalSeconds -ge 45) { throw "Another monitor control operation is still running for $Root" }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Write-MonitorJson([string]$Path, $Value) {
    $temporary = "$Path.$PID.tmp"
    $json = $Value | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
    try {
        for ($attempt=0; ; $attempt++) {
            try {
                if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
                else { [IO.File]::Move($temporary, $Path) }
                return
            } catch [IO.IOException] {
                if ($attempt -ge 20) { throw }
                Start-Sleep -Milliseconds 25
            }
        }
    } finally {
        [IO.File]::Delete($temporary)
    }
}
