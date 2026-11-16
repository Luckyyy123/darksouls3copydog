param([string]$Compiler=$env:COPYDOG_GCC,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'scripts/Toolchain.ps1')
$gcc=Resolve-CopydogCompiler $Compiler
$python=Resolve-CopydogPython
$testOutput=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $root 'build/tests'}
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
Push-Location $root
try {
    foreach($mode in @('O0','O2')) {
        foreach($test in @('prediction_geometry','prediction_runtime','turn_forecast','prediction_prefix')) {
            & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror "tests\test_$test.c" src\prediction_geometry.c -o "$testOutput\test_${test}_$mode.exe"
            if($LASTEXITCODE){throw "$test compilation failed"}
            & "$testOutput\test_${test}_$mode.exe"
            if($LASTEXITCODE){throw "$test tests failed"}
        }
        foreach($test in @('phase_timing','phase_attack_data','phase_runtime','roll_phase','roll_dispatch','roll_observed_map','projectile_set','projectile_motion','projectile_area_geometry','frame_formula')) {
            & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror "tests\test_$test.c" -o "$testOutput\test_${test}_$mode.exe"
            if($LASTEXITCODE){throw "$test compilation failed"}
            & "$testOutput\test_${test}_$mode.exe"
            if($LASTEXITCODE){throw "$test tests failed"}
        }
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_projectile_geometry.c src\combat_core.c -o "$testOutput\test_projectile_geometry_$mode.exe"
        if($LASTEXITCODE){throw 'Projectile geometry compilation failed'}
        & "$testOutput\test_projectile_geometry_$mode.exe"
        if($LASTEXITCODE){throw 'Projectile geometry tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar tests\test_frontal_riposte.c src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_frontal_riposte_$mode.exe" -lwinmm
        if($LASTEXITCODE){throw 'Frontal riposte compilation failed'}
        & "$testOutput\test_frontal_riposte_$mode.exe"
        if($LASTEXITCODE){throw 'Frontal riposte tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar tests\test_roll_poll_priority.c src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_roll_poll_priority_$mode.exe" -lwinmm
        if($LASTEXITCODE){throw 'Roll polling priority compilation failed'}
        & "$testOutput\test_roll_poll_priority_$mode.exe"
        if($LASTEXITCODE){throw 'Roll polling priority tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar tests\test_crossbow_roll.c src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_crossbow_roll_$mode.exe" -lwinmm
        if($LASTEXITCODE){throw 'Crossbow roll compilation failed'}
        & "$testOutput\test_crossbow_roll_$mode.exe"
        if($LASTEXITCODE){throw 'Crossbow roll tests failed'}
        foreach($test in @('roll_actionable','roll_chain','roll_repeat','roll_threat','sc10_projectiles','prediction_dispatch','parry_move_gate','diagnostic_parry','sc23_parry','sc23_features','sc12_projectiles','curved_eligibility','dynamic_parry','frame_state_review','native_cue_dispatch')) {
            & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar "tests\test_$test.c" src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_${test}_$mode.exe" -lwinmm
            if($LASTEXITCODE){throw "$test integration compilation failed"}
            & "$testOutput\test_${test}_$mode.exe"
            if($LASTEXITCODE){throw "$test integration tests failed"}
        }
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar tests\test_ground_burst.c src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_ground_burst_$mode.exe" -lwinmm
        if($LASTEXITCODE){throw 'Ground burst integration compilation failed'}
        & "$testOutput\test_ground_burst_$mode.exe"
        if($LASTEXITCODE){throw 'Ground burst integration failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_early_clock.c -o "$testOutput\test_early_clock_$mode.exe"
        if($LASTEXITCODE){throw 'Early clock compilation failed'}
        & "$testOutput\test_early_clock_$mode.exe"
        if($LASTEXITCODE){throw 'Early clock tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_shifted_parry.c src\parry.c src\parry_empirical.c -o "$testOutput\test_shifted_parry_$mode.exe"
        if($LASTEXITCODE){throw 'Shifted parry compilation failed'}
        & "$testOutput\test_shifted_parry_$mode.exe"
        if($LASTEXITCODE){throw 'Shifted parry tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_auto_trace.c -o "$testOutput\test_auto_trace_$mode.exe"
        if($LASTEXITCODE){throw 'Auto trace compilation failed'}
        & "$testOutput\test_auto_trace_$mode.exe"
        if($LASTEXITCODE){throw 'Auto trace tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_defense.c src\defense.c -o "$testOutput\test_defense_$mode.exe"
        if($LASTEXITCODE){throw 'Defense unit compilation failed'}
        & "$testOutput\test_defense_$mode.exe" | Tee-Object -FilePath "$testOutput\defense_$mode.results.txt"
        if($LASTEXITCODE){throw 'Defense unit tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror -Wno-multichar tests\test_defense_integration.c src\defense.c src\parry.c src\parry_empirical.c src\roll_direction.c src\control.c src\combat.c src\combat_core.c src\game_address.c src\prediction.c src\prediction_geometry.c -o "$testOutput\test_defense_integration_$mode.exe" -lwinmm
        if($LASTEXITCODE){throw 'Defense integration compilation failed'}
        & "$testOutput\test_defense_integration_$mode.exe" | Tee-Object -FilePath "$testOutput\defense_integration_$mode.results.txt"
        if($LASTEXITCODE){throw 'Defense integration tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_observed_clock.c src\parry.c src\control.c -o "$testOutput\test_observed_clock_$mode.exe"
        if($LASTEXITCODE){throw 'Observed clock compilation failed'}
        & "$testOutput\test_observed_clock_$mode.exe" | Tee-Object -FilePath "$testOutput\observed_clock_$mode.results.txt"
        if($LASTEXITCODE){throw 'Observed clock tests failed'}
        & $gcc -std=gnu11 "-$mode" -Wall -Wextra -Werror tests\test_empirical.c src\parry_empirical.c -o "$testOutput\test_empirical_$mode.exe"
        if($LASTEXITCODE){throw 'Empirical profile compilation failed'}
        & "$testOutput\test_empirical_$mode.exe" | Tee-Object -FilePath "$testOutput\empirical_$mode.results.txt"
        if($LASTEXITCODE){throw 'Empirical profile tests failed'}
    }
    & $python -B offline_sim/test_early_native.py --compiler $gcc
    if($LASTEXITCODE){throw 'Recorded native-onset replay failed'}
    & $python -B offline_sim/test_parry_evidence.py --compiler $gcc
    if($LASTEXITCODE){throw 'Recorded parry contact evidence replay failed'}
} finally {Pop-Location}
