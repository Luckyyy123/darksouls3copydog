function Resolve-CopydogCompiler([string]$Compiler) {
    if (-not $Compiler) { $Compiler = $env:COPYDOG_GCC }
    if (-not $Compiler) {
        $found = Get-Command gcc.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $Compiler = $found.Source }
    }
    if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
        throw 'Install a Windows x64 MinGW-w64 GCC toolchain, put gcc.exe on PATH, or pass -Compiler / set COPYDOG_GCC.'
    }
    $Compiler = (Resolve-Path -LiteralPath $Compiler).ProviderPath
    $env:PATH = [IO.Path]::GetDirectoryName($Compiler) + ';' + $env:PATH
    $target = & $Compiler -dumpmachine
    if ($LASTEXITCODE -or $target -notmatch '^x86_64-.*mingw') { throw 'A Windows x64 MinGW-w64 compiler is required.' }
    return $Compiler
}

function Resolve-CopydogPython {
    if ($env:COPYDOG_PYTHON) {
        if (-not (Test-Path -LiteralPath $env:COPYDOG_PYTHON -PathType Leaf)) { throw 'COPYDOG_PYTHON does not name a Python executable.' }
        $python = (Resolve-Path -LiteralPath $env:COPYDOG_PYTHON).ProviderPath
    } else {
        $found = Get-Command python.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $found) { throw 'Install Python 3.10+ and put python.exe on PATH, or set COPYDOG_PYTHON.' }
        $python = $found.Source
    }
    & $python -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'
    if ($LASTEXITCODE) { throw 'Python 3.10+ is required.' }
    return $python
}
