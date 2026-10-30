$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyPath = Join-Path $projectRoot 'third_party/libvncserver'
$revision = '98aaee0560baa0332339b2ef464ea96ed9b9b28c'
if (-not (Test-Path -LiteralPath $dependencyPath)) {
    git clone https://github.com/LibVNC/libvncserver.git $dependencyPath
    if ($LASTEXITCODE -ne 0) { throw 'LibVNC clone failed' }
}
git -C $dependencyPath checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'LibVNC checkout failed; preserve any local dependency edits before retrying' }
