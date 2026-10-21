# Persona 3 FES Recompiled - engine updater (development builds).
#
# Downloads the latest engine from the repository's dev-builds branch: ps2runtime.dll, pcsx2gs.dll and its resources,
# run.bat and this updater. The game executable (ps2EntryRunner.exe, built from your own copy of the game) is never
# downloaded or replaced. If an engine update needs a newer game executable, nothing is changed.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = 'poopoocacacacaca/p3fes-recompiled'
$branch = 'dev-builds'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

function Read-Text($path) {
    if (Test-Path $path) { return (Get-Content $path -Raw).Trim() }
    return ''
}

function Finish($code) {
    Write-Host ''
    Read-Host 'Press Enter to close'
    exit $code
}

try {
    if (Get-Process -Name 'ps2EntryRunner' -ErrorAction SilentlyContinue) {
        Write-Host 'The game is running. Close it, then run Update again.' -ForegroundColor Yellow
        Finish 1
    }

    Write-Host 'Checking for updates...'
    $headers = @{ 'User-Agent' = 'p3fes-updater' }
    $head = Invoke-RestMethod "https://api.github.com/repos/$repo/commits/$branch" -Headers $headers
    $sha = $head.sha
    $current = Read-Text 'engine_version.txt'
    if ($sha -eq $current) {
        Write-Host 'Already up to date.' -ForegroundColor Green
        Finish 0
    }

    $work = Join-Path $env:TEMP ("p3fes-update-" + $sha.Substring(0, 12))
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
    New-Item -ItemType Directory $work | Out-Null
    $zip = Join-Path $work 'engine.zip'
    Write-Host 'Downloading...'
    Invoke-WebRequest "https://github.com/$repo/archive/$sha.zip" -OutFile $zip -Headers $headers -UseBasicParsing
    Expand-Archive $zip -DestinationPath $work -Force
    $src = Get-ChildItem $work -Directory | Select-Object -First 1

    # The engine and the game executable must come from the same runtime headers.
    $needAbi = Read-Text (Join-Path $src.FullName 'abi.txt')
    $haveAbi = Read-Text 'game_abi.txt'
    if ($haveAbi -and $needAbi -and $needAbi -ne $haveAbi) {
        Write-Host ''
        Write-Host 'This update needs a newer game executable (ps2EntryRunner.exe), which is built from your own copy' -ForegroundColor Yellow
        Write-Host 'of the game and is not downloadable. Nothing was changed. Ask for a new ps2EntryRunner.exe.' -ForegroundColor Yellow
        Finish 1
    }

    # Keep the previous engine so an update can be undone by copying these back.
    $backup = Join-Path $here 'engine_backup'
    if (Test-Path $backup) { Remove-Item $backup -Recurse -Force }
    New-Item -ItemType Directory $backup | Out-Null
    foreach ($name in @('ps2runtime.dll', 'pcsx2gs.dll', 'run.bat', 'engine_version.txt')) {
        if (Test-Path $name) { Copy-Item $name $backup }
    }

    Copy-Item (Join-Path $src.FullName 'ps2runtime.dll') $here -Force
    Copy-Item (Join-Path $src.FullName 'pcsx2gs.dll') $here -Force
    Copy-Item (Join-Path $src.FullName 'pcsx2gs_resources') $here -Recurse -Force
    Copy-Item (Join-Path $src.FullName 'run.bat') $here -Force
    Copy-Item (Join-Path $src.FullName 'Update.bat') $here -Force
    if (-not $haveAbi -and $needAbi) { Set-Content 'game_abi.txt' $needAbi }
    Set-Content 'engine_version.txt' $sha

    Write-Host ''
    Write-Host ('Updated to ' + $sha.Substring(0, 7) + '.') -ForegroundColor Green
    $notes = Join-Path $src.FullName 'CHANGES.txt'
    if (Test-Path $notes) {
        Write-Host ''
        Get-Content $notes | Write-Host
    }

    # Replace this script last (it is running).
    Copy-Item (Join-Path $src.FullName 'Update.ps1') (Join-Path $here 'Update.ps1') -Force
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Finish 0
}
catch {
    Write-Host ''
    Write-Host ('Update failed: ' + $_.Exception.Message) -ForegroundColor Red
    Finish 1
}
