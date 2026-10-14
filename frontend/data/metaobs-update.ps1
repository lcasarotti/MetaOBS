# Installs a MetaOBS update. MetaOBS downloads and verifies the package,
# writes the settings below into a JSON file and starts this script, then
# closes. The script waits for it to exit, installs the package, writes the
# result for MetaOBS to report, and starts MetaOBS again.
#
# Settings: version, mode, packagePath, installerArguments, installDir,
# executable, arguments, processId, elevated, resultPath, logPath.
#
# In "installer" mode the package is the MetaOBS installer, run with
# installerArguments; it asks for administrator rights itself when needed.
# In "files" mode the package is a zip whose files are copied over the
# installation. With elevated set, only the copy runs as administrator, in a
# second instance started with -CopyFrom, so that MetaOBS starts again as the
# user.
#
# Keep this file ASCII: Windows PowerShell reads scripts without a byte order
# mark in the ANSI code page.

param(
    [Parameter(Mandatory = $true)]
    [string] $ConfigPath,
    [string] $CopyFrom
)

$ErrorActionPreference = 'Stop'

$config = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$installDir = $config.installDir.TrimEnd('\')
$staging = Join-Path (Split-Path -Parent $ConfigPath) 'staging'
$copyErrorPath = "$ConfigPath.copy-error"

function Write-Log([string] $message) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $message
    Add-Content -LiteralPath $config.logPath -Value $line -Encoding UTF8
}

# Processes started from the installation, such as the browser source
# helpers, keep its files in use.
function Get-InstallProcesses {
    Get-Process | Where-Object {
        try { $_.Path -and $_.Path.StartsWith($installDir + '\', [StringComparison]::OrdinalIgnoreCase) }
        catch { $false }
    }
}

# Replaces one file. A file in use, such as the virtual camera loaded by
# another application, cannot be overwritten but can be renamed, so it is
# moved aside and removed by the next update.
function Copy-UpdateFile([string] $source, [string] $target) {
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        try {
            Copy-Item -LiteralPath $source -Destination $target -Force
            return
        } catch {
            if ($attempt -eq 3 -and (Test-Path -LiteralPath $target)) {
                try { Move-Item -LiteralPath $target -Destination "$target.metaobs-old" -Force } catch { }
            }
            Start-Sleep -Milliseconds 500
        }
    }
    throw "Could not replace $target"
}

function Copy-Update([string] $source) {
    # The file names below are long paths, so the source must be one too.
    $source = (Get-Item -LiteralPath $source).FullName.TrimEnd('\')

    Get-ChildItem -LiteralPath $installDir -Recurse -File -Filter '*.metaobs-old' -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }

    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File)
    Write-Log "Copying $($files.Count) files"
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length).TrimStart('\')
        $target = Join-Path $installDir $relative
        $directory = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $directory)) {
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
        }
        Copy-UpdateFile $file.FullName $target
    }
}

function Install-WithInstaller {
    Write-Log 'Running the installer'
    $installer = Start-Process -FilePath $config.packagePath -ArgumentList $config.installerArguments -Wait -PassThru
    if ($installer.ExitCode -ne 0) {
        throw "The installer stopped with exit code $($installer.ExitCode). Its log is next to this one."
    }
}

function Install-Files {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    Write-Log 'Extracting the package'
    Expand-Archive -LiteralPath $config.packagePath -DestinationPath $staging -Force

    # The package holds the installation either at its root or in a single
    # top-level folder.
    $source = $staging
    if (-not (Test-Path -LiteralPath (Join-Path $source 'bin'))) {
        $folders = @(Get-ChildItem -LiteralPath $source -Directory)
        if ($folders.Count -eq 1) {
            $source = $folders[0].FullName
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'bin\64bit\obs64.exe'))) {
        throw 'The package does not contain MetaOBS.'
    }

    if (-not $config.elevated) {
        Copy-Update $source
        return
    }

    Write-Log 'Asking for administrator rights'
    Remove-Item -LiteralPath $copyErrorPath -Force -ErrorAction SilentlyContinue
    $arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden ' +
        "-File `"$PSCommandPath`" -ConfigPath `"$ConfigPath`" -CopyFrom `"$source`""
    try {
        $copy = Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList $arguments -Verb RunAs `
            -WindowStyle Hidden -Wait -PassThru
    } catch {
        throw 'Administrator rights were not granted, so the update was not installed.'
    }
    if ($copy.ExitCode -ne 0) {
        $copyError = 'Copying the files as administrator failed.'
        if (Test-Path -LiteralPath $copyErrorPath) {
            $copyError = [IO.File]::ReadAllText($copyErrorPath)
            Remove-Item -LiteralPath $copyErrorPath -Force -ErrorAction SilentlyContinue
        }
        throw $copyError
    }
}

# The instance running as administrator only copies.
if ($CopyFrom) {
    try {
        Copy-Update $CopyFrom
        exit 0
    } catch {
        [IO.File]::WriteAllText($copyErrorPath, $_.Exception.Message)
        try { Write-Log "Copy failed: $($_.Exception.Message)" } catch { }
        exit 1
    }
}

$result = [ordered]@{ version = $config.version; success = $false; error = '' }

try {
    Write-Log "Installing MetaOBS $($config.version) into $installDir ($($config.mode))"

    $process = Get-Process -Id $config.processId -ErrorAction SilentlyContinue
    if ($process) {
        Write-Log 'Waiting for MetaOBS to close'
        $process.WaitForExit()
    }

    $deadline = (Get-Date).AddSeconds(60)
    while (Get-InstallProcesses) {
        if ((Get-Date) -gt $deadline) {
            $names = (Get-InstallProcesses | ForEach-Object { $_.ProcessName } | Sort-Object -Unique) -join ', '
            throw "These programs are still running from the installation: $names"
        }
        Start-Sleep -Seconds 1
    }

    if ($config.mode -eq 'installer') {
        Install-WithInstaller
    } else {
        Install-Files
    }

    $result.success = $true
    Write-Log 'Update installed'
} catch {
    $result.error = $_.Exception.Message
    try { Write-Log "Update failed: $($result.error)" } catch { }
} finally {
    $json = $result | ConvertTo-Json
    [IO.File]::WriteAllText($config.resultPath, $json)

    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $config.packagePath -Force -ErrorAction SilentlyContinue

    try {
        $workingDirectory = Split-Path -Parent $config.executable
        if ($config.arguments) {
            Start-Process -FilePath $config.executable -WorkingDirectory $workingDirectory `
                -ArgumentList $config.arguments
        } else {
            Start-Process -FilePath $config.executable -WorkingDirectory $workingDirectory
        }
    } catch {
        try { Write-Log "Could not start MetaOBS: $($_.Exception.Message)" } catch { }
    }
}
