"""Opt-in SCons upload hook for a dedicated OTA environment, never USB.

An environment named elecrow-crowpanel-128-ota (or explicitly using
upload_protocol = ota) may load this file as a pre: extra_script. No key is
read during SCons setup; all validation is delegated to the workstation client.
This hook does not add the firmware marker or change build flags: OTA and USB
must build the same marked application binary in their firmware configuration.
"""

from __future__ import annotations

import importlib
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Mapping, Sequence


def is_ota_environment(env: Any) -> bool:
    """Recognize only the dedicated OTA environment or an explicit OTA protocol."""
    name = env.subst("$PIOENV")
    protocol = env.GetProjectOption("upload_protocol", "")
    return name.endswith("-ota") or protocol == "ota"


def reject_filesystem(targets: Sequence[object]) -> None:
    """Reject both filesystem build/upload targets before any action can run."""
    if any(str(target).lower() in ("uploadfs", "buildfs") for target in targets):
        raise RuntimeError("OTA filesystem targets are forbidden; only application firmware may be uploaded.")


def python_command(env: Any) -> str:
    """Use SCons' Python interpreter, falling back only to a known virtualenv."""
    executable = env.subst("$PYTHONEXE").strip()
    if executable and "$" not in executable:
        return executable
    if sys.prefix != sys.base_prefix and sys.executable:
        return sys.executable
    raise RuntimeError("OTA requires a configured PlatformIO Python interpreter.")


def install(env: Any, targets: Sequence[object], environ: Mapping[str, str] | None = None) -> None:
    """Install a callable application uploader only for the OTA environment."""
    if not is_ota_environment(env):
        return
    reject_filesystem(targets)
    variables = os.environ if environ is None else environ

    def upload_action(source: Sequence[Any], target: Sequence[Any], env: Any) -> int:
        """Invoke the stdlib client once with the application binary and private key path."""
        reject_filesystem(targets)
        reject_filesystem(target)
        project = Path(env.subst("$PROJECT_DIR"))
        host = (variables.get("RV_CONTROL_UI_OTA_HOST", "")
            or env.GetProjectOption("custom_ota_host", "")
            or "192.168.77.1")
        key = variables.get("RV_CONTROL_UI_OTA_KEY_FILE", "") or str(project / "secrets" / "ota.key")
        firmware = env.subst("$BUILD_DIR/${PROGNAME}.bin")
        command = [python_command(env), str(project / "scripts" / "ota_upload.py"),
                   "upload", "--host", host, "--key-file", key, "--firmware", firmware]
        return subprocess.call(command, cwd=project)

    env.Replace(UPLOADCMD=upload_action)


if "Import" in globals():
    globals()["Import"]("env")
    scons = importlib.import_module("SCons.Script")
    build_env = globals()["env"]
    install(build_env, list(globals().get("TARGETS", []))
            + list(scons.COMMAND_LINE_TARGETS) + list(build_env.get("TARGETS", [])))