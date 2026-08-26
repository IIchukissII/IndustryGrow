# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
#
# Windows control-node helper: copy the gateway/ bundle to the Pi over scp and
# run provision.sh over ssh. Uses ONLY ssh/scp (OpenSSH ships with Windows 10/11)
# and the existing SSH key — no secrets, no extra tooling. Ansible (via WSL) is
# the documented fleet-scale upgrade path; it is NOT required for bring-up.
#
# Usage:
#   .\deploy.ps1 -HostName gbox-dev
#   .\deploy.ps1 -HostName 192.168.1.50 -User igrow -SshKey $HOME\.ssh\id_ed25519
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $HostName,
    [string] $User    = "igrow",
    [string] $SshKey  = "",
    [int]    $Port    = 22
)

$ErrorActionPreference = "Stop"
$bundle  = $PSScriptRoot
$target  = "$User@$HostName"
$remote  = "/tmp/industrygrow-provision"

# scp spells the port -P; -p means "preserve times" and is silently accepted, so
# passing ssh's option array to scp sends every non-default port to 22.
$sshOpts = @("-p", "$Port")
$scpOpts = @("-P", "$Port")
if ($SshKey -ne "") { $sshOpts += @("-i", $SshKey); $scpOpts += @("-i", $SshKey) }

# The DSDL definitions are compiled on the Pi at provisioning time (ADR-0005 d10)
# and live in the firmware tree, which is not part of gateway/. They are staged
# beside provision.sh rather than copied into the bundle in the repo: one tree,
# and provision.sh finds it either way (default_dsdl_roots).
$repo = Split-Path $PSScriptRoot -Parent
$dsdlRoots = @(
    (Join-Path $repo "firmware\third_party\public_regulated_data_types\uavcan"),
    (Join-Path $repo "firmware\dsdl\industryflow")
)

Write-Host "[deploy] copying bundle to ${target}:$remote" -ForegroundColor Green
# Stage the bundle on the Pi (recursive). scp uses the same key/agent as ssh.
& ssh @sshOpts $target "rm -rf $remote && mkdir -p $remote/dsdl"
& scp @scpOpts -r "$bundle\*" "${target}:$remote/"
if ($LASTEXITCODE -ne 0) { throw "scp failed (exit $LASTEXITCODE)" }

foreach ($root in $dsdlRoots) {
    if (-not (Test-Path $root)) {
        throw "DSDL namespace missing: $root (run firmware/tools/bootstrap.sh to fetch the submodule)"
    }
    & scp @scpOpts -r $root "${target}:$remote/dsdl/"
    if ($LASTEXITCODE -ne 0) { throw "scp of $root failed (exit $LASTEXITCODE)" }
}

Write-Host "[deploy] running provision.sh (sudo) on $HostName" -ForegroundColor Green
# CRLF safety: the bundle's *.sh are LF-pinned via .gitattributes, but normalise
# defensively in case the working tree was checked out with autocrlf.
& ssh @sshOpts $target "sed -i 's/\r$//' $remote/provision.sh $remote/requirements.txt; sudo bash $remote/provision.sh"
if ($LASTEXITCODE -ne 0) { throw "provision.sh failed (exit $LASTEXITCODE)" }

Write-Host "[deploy] done. See the Manual's 'Verification' section." -ForegroundColor Green
