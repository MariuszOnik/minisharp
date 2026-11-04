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
    # -static: no MinGW DLLs needed, so it runs without msys64 on PATH
    & "$gccDir\gcc.exe" -static -O2 -std=c99 -D_DEFAULT_SOURCE -o minisharp.exe (Get-ChildItem src\*.c, pc\*.c | ForEach-Object { $_.FullName }) -lm
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { throw 'gcc failed' }
}

# A file in games/<Name>/ or examples/<Name>/ belongs to project <Name>: the whole folder is compiled
function Get-ProjectDir([string]$path) {
    $dir = Split-Path -Parent (Resolve-Path $path)
    $parent = Split-Path -Leaf (Split-Path -Parent $dir)
    $top = Split-Path -Parent (Split-Path -Parent $dir)
    if (($parent -eq 'games' -or $parent -eq 'examples') -and $top -eq (Resolve-Path $root).Path) { return $dir }
    return $null
}

function Check([string]$path) {
    Build-Pc
    $project = Get-ProjectDir $path
    $target = if ($project) { $project } else { $path }
    # PowerShell 5.1 turns native stderr into error records, which 'Stop' would throw on
    $ErrorActionPreference = 'Continue'
    $out = & $exe $target --check 2>&1 | ForEach-Object { "$_" } | Out-String
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -eq 0) { Write-Host "OK: $(Split-Path -Leaf $target)"; return $true }
    # "compile error: line 12: msg" (or "Player.cs line 12: msg" in a project)
    #   -> "C:\...\game.cs:12: error: msg" for the problem matcher
    if ($project -and $out -match '(\S+\.cs) line (\d+): (.*)') { Write-Host "$(Join-Path $project $Matches[1]):$($Matches[2]): error: $($Matches[3].Trim())" }
    elseif ($out -match 'line (\d+): (.*)') { Write-Host "${path}:$($Matches[1]): error: $($Matches[2].Trim())" }
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
        $project = Get-ProjectDir $File
        if ($project) {                 # the whole folder -> sdmc:/minisharp/<Name>/
            $remote = 'minisharp/' + (Split-Path -Leaf $project)
            Get-ChildItem $project -File | Where-Object { $_.Extension -match '^\.(cs|png|jpg|jpeg|bmp|wav)$' } |
                ForEach-Object { Upload $_.FullName $remote }
            Write-Host 'On the 3DS: Y refreshes the list, A runs the project.'
            return
        }
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
