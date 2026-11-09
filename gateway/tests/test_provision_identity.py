# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The software identity key of a gateway without a secure element (ADR-0007 d11).

A software key generated on the gateway is a production identity: the binding
records it without a fixture marker, and refuses it while the host does not
protect the file. A test key carries the `fixture` marker. The CA round trip and
the ERP request model are covered by erp/tests/test_gateway_provisioning.py.
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import sys
from pathlib import Path

import pytest

GATEWAY = Path(__file__).resolve().parents[1]

pytestmark = pytest.mark.skipif(not shutil.which("openssl"), reason="the tool signs with openssl")


def _load_tool():
    spec = importlib.util.spec_from_file_location(
        "provision_identity", GATEWAY / "provision_identity.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


tool = _load_tool()


def binding(key: Path, out: Path, *extra: str) -> int:
    return tool.main(
        [
            "binding",
            "--gbox",
            "GBOX_0001",
            "--vendor-serial",
            "SP0004-SN-000123",
            "--software-key",
            str(key),
            "--out",
            str(out),
            *extra,
        ]
    )


def test_the_key_is_generated_in_place_0640_with_the_directory_group(tmp_path):
    key = tmp_path / "pki" / "gateway.key"
    tool.SoftwareSigner.generate(key)
    st = key.stat()
    assert st.st_mode & 0o777 == 0o640
    assert st.st_gid == key.parent.stat().st_gid
    assert "PRIVATE KEY" in key.read_text()


def test_an_existing_key_is_never_regenerated(tmp_path):
    key = tmp_path / "gateway.key"
    tool.SoftwareSigner.generate(key)
    before = key.read_bytes()
    with pytest.raises(tool.ProvisioningError, match="never regenerated"):
        tool.SoftwareSigner.generate(key)
    assert key.read_bytes() == before


def test_the_default_key_is_gateway_key_in_the_pki_directory():
    assert Path("/etc/industrygrow/pki/gateway.key") == tool.DEFAULT_SOFTWARE_KEY
    args = tool.build_parser().parse_args(
        ["binding", "--gbox", "GBOX_0001", "--vendor-serial", "SN1", "--software-key"]
    )
    assert args.software_key == str(tool.DEFAULT_SOFTWARE_KEY)


def test_a_software_key_writes_a_production_binding(tmp_path):
    key = tmp_path / "gateway.key"
    out = tmp_path / "binding.json"
    tool.SoftwareSigner.generate(key)
    assert binding(key, out) == 0

    record = json.loads(out.read_text())
    assert record["provenance"] == "software"
    assert "fixture" not in record
    assert record["atecc_serial"] is None
    assert record["public_key_fingerprint"] == tool.public_key_fingerprint(
        tool.SoftwareSigner(key).public_key()
    )
    assert "PRIVATE KEY" not in out.read_text()


def test_a_test_key_stays_marked_as_a_fixture(tmp_path):
    key = tmp_path / "gateway.key"
    out = tmp_path / "binding.json"
    tool.SoftwareSigner.generate(key)
    assert binding(key, out, "--fixture") == 0

    record = json.loads(out.read_text())
    assert record["provenance"] == "software"
    assert "not a provisioned unit" in record["fixture"]


@pytest.mark.parametrize("mode", [0o644, 0o660, 0o604])
def test_a_key_the_host_does_not_protect_is_refused(tmp_path, capsys, mode):
    key = tmp_path / "gateway.key"
    out = tmp_path / "binding.json"
    tool.SoftwareSigner.generate(key)
    key.chmod(mode)
    assert binding(key, out) == 2
    assert f"mode {mode:04o}" in capsys.readouterr().err
    assert not out.exists()


def test_fixture_requires_a_software_key(capsys):
    assert tool.main(["binding", "--gbox", "GBOX_0001", "--vendor-serial", "SN1", "--fixture"]) == 2
    assert "an ATECC key is never one" in capsys.readouterr().err
