param([string]$Dotnet)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Dotnet) {
    $found = Get-Command dotnet.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { throw 'Install the .NET 10 SDK and put dotnet.exe on PATH, or pass -Dotnet.' }
    $Dotnet = $found.Source
}
if (-not (Test-Path -LiteralPath $Dotnet -PathType Leaf)) { throw 'The .NET executable was not found.' }
& $Dotnet build (Join-Path $root 'live_monitor/latency_etw/LatencyEtw.csproj') --configuration Release --nologo
if ($LASTEXITCODE) { throw 'Optional ETW monitor build failed.' }
Write-Output 'Built the optional network sidecar. Start-Monitor.ps1 can now discover it.'
