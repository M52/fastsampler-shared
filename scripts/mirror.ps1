param()
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
function Invoke-Git { & git @args; if ($LASTEXITCODE -ne 0) { throw 'Git command failed.' } }
if ((Invoke-Git status --porcelain)) { throw 'Commit or stash local changes before mirroring.' }
if ((Invoke-Git branch --show-current) -ne 'main') { throw 'Run from main.' }
$origin = Invoke-Git remote get-url origin
$github = Invoke-Git remote get-url github
if ($origin -ne 'git@git.omkserver.nl:Macaberz/fastsampler-shared.git') { throw 'Unexpected upstream.' }
if ($github -ne 'git@github.com:M52/fastsampler-shared.git') { throw 'Unexpected mirror.' }
Invoke-Git push origin main --follow-tags
Invoke-Git push github main --follow-tags
