param(
    [string]$Compiler=$env:COPYDOG_GCC,
    [string]$OutputDirectory,
    [string]$BuildDirectory,
    [switch]$DllOnly,
    [switch]$DebugSymbols
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'scripts/Toolchain.ps1')
$gcc=Resolve-CopydogCompiler $Compiler
$output=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $root 'dist'}
$build=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory)}else{Join-Path $root 'build/native'}
New-Item -ItemType Directory -Path $output,$build -Force | Out-Null
$flags=@('-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-multichar',
    "-ffile-prefix-map=$root=.","-fdebug-prefix-map=$root=.")
if($DebugSymbols){$flags+='-g'}else{$flags+=@('-g0','-s')}
$dllSources=@('copydog.c','defense.c','parry.c','parry_empirical.c','control.c',
    'roll_direction.c','game_address.c','gamedata.c','runtime.c','hook_lifetime.c',
    'receive_bridge.c','config.c','combat.c','combat_core.c','prediction.c',
    'prediction_geometry.c','combat_hooks.S','worker_guard.S')
Push-Location (Join-Path $root 'src')
try {
    & $gcc @flags -shared -o (Join-Path $output 'copydog.dll') @dllSources -lwinmm -lpsapi -luser32 '-Wl,--nxcompat,--dynamicbase'
    if($LASTEXITCODE){throw 'copydog.dll compilation failed.'}
    if(-not $DllOnly){
        $windres=Join-Path ([IO.Path]::GetDirectoryName($gcc)) 'windres.exe'
        if(-not (Test-Path -LiteralPath $windres)){throw 'The MinGW-w64 windres resource compiler is required for the loader manifest.'}
        $resource=Join-Path $build 'loader-res.o'
        & $windres (Join-Path $root 'packaging/loader.rc') -I (Join-Path $root 'packaging') -O coff -o $resource
        if($LASTEXITCODE){throw 'Loader manifest compilation failed.'}
        & $gcc @flags -municode -o (Join-Path $output 'copydog-loader.exe') loader.c config.c $resource -ladvapi32 -lpsapi -luser32 '-Wl,--subsystem,console,--nxcompat,--dynamicbase'
        if($LASTEXITCODE){throw 'copydog-loader.exe compilation failed.'}
    }
} finally {Pop-Location}
Copy-Item -LiteralPath (Join-Path $root 'config/config.txt') -Destination (Join-Path $output 'config.txt') -Force
if(-not $DllOnly){
    Get-ChildItem -LiteralPath (Join-Path $root 'packaging/commands') -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $output $_.Name) -Force
    }
}
Copy-Item -LiteralPath (Join-Path $root 'docs/PLAYER_README.txt') -Destination (Join-Path $output 'README.txt') -Force
$notices=Join-Path $output 'notices'
New-Item -ItemType Directory -Path $notices -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'third_party/notices') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $notices $_.Name) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs/LICENSING.md') -Destination (Join-Path $notices 'PROJECT-LICENSING.md') -Force
Get-Item -LiteralPath (Join-Path $output 'copydog.dll') | Select-Object Name,Length
if(-not $DllOnly){Get-Item -LiteralPath (Join-Path $output 'copydog-loader.exe') | Select-Object Name,Length}
