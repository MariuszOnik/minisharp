# MiniSharp helper for VS Code tasks.
#   ms.ps1 check   <file.cs>   compile with MiniSharp on the PC, errors as "file:line: error: msg"
#   ms.ps1 deploy  <file.cs>   check, then upload to sdmc:/minisharp/ over FTP
#   ms.ps1 preview <file.cs>   run 120 frames on the PC and open the last frame as PNG
#   ms.ps1 log                 download sdmc:/minisharp/log.txt and print it
#   ms.ps1 app                 build MiniSharp.3dsx and upload it to sdmc:/3ds/
# Console address: $env:MINISHARP_3DS (default 192.168.1.90:5000, ftpd on the 3DS).
param([Parameter(Mandatory = $true)][string]$Action, [string]$File)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ftp = if ($env:MINISHARP_3DS) { $env:MINISHARP_3DS } else { '192.168.1.90:5000' }
$exe = Join-Path $root 'minisharp.exe'
$gccDir = 'C:\msys64\mingw64\bin'
$devkitPro = 'D:\devkitPro'

function Build-Pc {
    $sources = Get-ChildItem "$root\src\*.c", "$root\pc\*.c", "$root\src\*.h"
    if ((Test-Path $exe) -and -not ($sources | Where-Object { $_.LastWriteTime -gt (Get-Item $exe).LastWriteTime })) { return }
    Write-Host 'Building minisharp.exe...'
    $env:Path = "$gccDir;$env:Path"
    Push-Location $root
    & "$gccDir\gcc.exe" -O2 -std=c99 -D_DEFAULT_SOURCE -o minisharp.exe src/parser.c src/compiler.c src/vm.c src/natives.c src/disasm.c pc/main_pc.c pc/platform_pc.c -lm
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { throw 'gcc failed' }
}

function Check([string]$path) {
    Build-Pc
    # PowerShell 5.1 turns native stderr into error records, which 'Stop' would throw on
    $ErrorActionPreference = 'Continue'
    $out = & $exe $path --check 2>&1 | ForEach-Object { "$_" } | Out-String
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -eq 0) { Write-Host "OK: $(Split-Path -Leaf $path)"; return $true }
    # "compile error: line 12: msg" -> "C:\...\game.cs:12: error: msg" for the problem matcher
    if ($out -match 'line (\d+): (.*)') { Write-Host "${path}:$($Matches[1]): error: $($Matches[2].Trim())" }
    else { Write-Host "${path}:1: error: $($out.Trim())" }
    return $false
}

function Upload([string]$local, [string]$remoteDir) {
    & curl.exe -s -S --connect-timeout 5 --ftp-create-dirs -T $local "ftp://$ftp/$remoteDir/"
    if ($LASTEXITCODE -ne 0) { throw "FTP upload failed - is ftpd running on the 3DS ($ftp)?" }
    Write-Host "Sent $(Split-Path -Leaf $local) -> sdmc:/$remoteDir/"
}

switch ($Action) {
    'check' { if (-not (Check $File)) { exit 1 } }
    'deploy' {
        if (-not (Check $File)) { exit 1 }
        Upload $File 'minisharp'
        # images and sounds next to the program, only the ones changed since the last deploy from this folder
        $srcDir = Split-Path -Parent (Resolve-Path $File)
        $stamp = Join-Path $root ("build\.deployed_" + ($srcDir -replace '[:\\/ ]', '_'))
        $since = if (Test-Path $stamp) { (Get-Item $stamp).LastWriteTime } else { [DateTime]::MinValue }
        Get-ChildItem $srcDir -File | Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|bmp|wav)$' -and $_.LastWriteTime -gt $since } |
            ForEach-Object { Upload $_.FullName 'minisharp' }
        New-Item -ItemType Directory -Force (Split-Path $stamp) | Out-Null
        Set-Content -Path $stamp -Value (Get-Date) -Encoding utf8
        Write-Host 'On the 3DS: Y refreshes the list, A runs.'
    }
    'preview' {
        if (-not (Check $File)) { exit 1 }
        $dir = Join-Path $root 'build\preview'
        New-Item -ItemType Directory -Force $dir | Out-Null
        $name = [IO.Path]::GetFileNameWithoutExtension($File)
        $ppm = Join-Path $dir "$name.ppm"
        & $exe $File --frames 120 --ppm $ppm
        $png = [IO.Path]::ChangeExtension($ppm, '.png')
        if (Get-Command magick -ErrorAction SilentlyContinue) {
            & magick $ppm $png
            & code -r $png
        } else { Write-Host "Frame saved to $ppm (install ImageMagick to get PNG)" }
    }
    'log' {
        $dst = Join-Path $root 'build\log.txt'
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        & curl.exe -s -S --connect-timeout 5 -o $dst "ftp://$ftp/minisharp/log.txt"
        if ($LASTEXITCODE -ne 0) { throw "FTP download failed - is ftpd running on the 3DS ($ftp)?" }
        Get-Content $dst -Tail 150         # the log keeps several sessions; the newest is at the end
    }
    'app' {
        $env:DEVKITPRO = '/d/devkitPro'; $env:DEVKITARM = '/d/devkitPro/devkitARM'
        $env:Path = "$devkitPro\msys2\usr\bin;$devkitPro\devkitARM\bin;$devkitPro\tools\bin;$env:Path"
        & "$devkitPro\msys2\usr\bin\make.exe" -C $root 'MAKE=/d/devkitPro/msys2/usr/bin/make'
        if ($LASTEXITCODE -ne 0) { throw 'make failed' }
        Upload (Join-Path $root 'MiniSharp.3dsx') '3ds'
    }
    default { throw "unknown action '$Action' (check, deploy, preview, log, app)" }
}
