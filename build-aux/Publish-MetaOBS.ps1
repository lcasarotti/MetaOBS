<#
.SYNOPSIS
Builds the MetaOBS package for Windows and optionally publishes it as a GitHub
release.

.DESCRIPTION
Reads METAOBS_VERSION and METAOBS_REPOSITORY from
frontend/cmake/feature-metaobs.cmake, builds the windows-x64 preset, installs
it into build_x64/metaobs-package/MetaOBS and zips it as
MetaOBS-<version>-windows-x64.zip, which is the package the built-in updater
downloads.

With -Publish, it also creates the release metaobs-v<version> on GitHub with
gh, from the current commit. That commit must be pushed, and the version must
not have been released yet. The release notes list the commits of the branch
since the previous release, unless -NotesFile gives them.

.EXAMPLE
./build-aux/Publish-MetaOBS.ps1

.EXAMPLE
./build-aux/Publish-MetaOBS.ps1 -Publish -NotesFile notes.md
#>

[CmdletBinding()]
param(
    [switch] $Publish,
    [string] $NotesFile,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -Path "$PSScriptRoot/..").Path
$cmake = 'C:/Program Files/CMake/bin/cmake.exe'
if (-not (Test-Path -Path $cmake)) {
    $cmake = 'cmake'
}

function Invoke-Checked([string] $FilePath, [string[]] $Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$settings = Get-Content -Path "$root/frontend/cmake/feature-metaobs.cmake" -Raw
if ($settings -notmatch 'set\(METAOBS_VERSION "([0-9.]+)"\)') {
    throw 'METAOBS_VERSION not found in feature-metaobs.cmake'
}
$version = $Matches[1]
if ($settings -notmatch 'set\(METAOBS_REPOSITORY "([^"]+)"\)') {
    throw 'METAOBS_REPOSITORY not found in feature-metaobs.cmake'
}
$repository = $Matches[1]

$tag = "metaobs-v$version"
$build = "$root/build_x64"
$packageDir = "$build/metaobs-package"
$stage = "$packageDir/MetaOBS"
$package = "$packageDir/MetaOBS-$version-windows-x64.zip"

Push-Location -Path $root
try {
    if ($Publish) {
        if (git status --porcelain --untracked-files=no) {
            throw 'Commit your changes before publishing.'
        }

        $branch = git branch --show-current
        $remote = git config "branch.$branch.remote"
        if (-not $remote) {
            throw "The branch $branch does not track a remote branch."
        }
        Invoke-Checked git @('fetch', '--quiet', $remote)
        $head = git rev-parse HEAD
        if ($head -ne (git rev-parse '@{u}')) {
            throw "Push $branch before publishing: the release is created from the pushed commit."
        }

        gh release view $tag --repo $repository *> $null
        if ($LASTEXITCODE -eq 0) {
            throw "$tag already exists. Raise METAOBS_VERSION in feature-metaobs.cmake and commit it."
        }
    }

    if (-not $SkipBuild) {
        Invoke-Checked $cmake @('--preset', 'windows-x64', '-DENABLE_WHATSNEW=OFF')
        Invoke-Checked $cmake @('--build', $build, '--config', 'RelWithDebInfo', '--parallel')
    }

    if (Test-Path -Path $packageDir) {
        Remove-Item -Path $packageDir -Recurse -Force
    }
    Invoke-Checked $cmake @('--install', $build, '--prefix', $stage, '--config', 'RelWithDebInfo')

    # Debug symbols and the accessibility tests are not shipped.
    Get-ChildItem -Path $stage -Recurse -File -Include '*.pdb', 'obs-a11y-tests.exe' | Remove-Item -Force

    Write-Host "Compressing $package"
    Compress-Archive -Path "$stage/*" -DestinationPath $package -CompressionLevel Optimal
    $hash = (Get-FileHash -Path $package -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Package: $package"
    Write-Host "SHA-256: $hash"

    if (-not $Publish) {
        return
    }

    if (-not $NotesFile) {
        $NotesFile = "$packageDir/notes.md"
        $previous = gh release view --repo $repository --json tagName --jq .tagName 2> $null
        if ($LASTEXITCODE -eq 0 -and $previous) {
            Invoke-Checked git @('fetch', '--quiet', $remote, 'tag', $previous)
            $changes = git log --first-parent --format='- %s' "$previous..HEAD"
            $notes = @('Changes since the previous release:', '') + $changes
        } else {
            $notes = @('First release of MetaOBS.')
        }
        Set-Content -Path $NotesFile -Value $notes -Encoding utf8
    }

    Write-Host "Publishing $tag to $repository"
    Invoke-Checked gh @('release', 'create', $tag, $package, '--repo', $repository, '--target', $head,
        '--title', "MetaOBS $version", '--notes-file', $NotesFile)
} finally {
    Pop-Location
}
