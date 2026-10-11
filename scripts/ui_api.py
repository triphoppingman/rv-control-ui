#!/usr/bin/env python3
"""Manage the local RV Control UI API using only the Python standard library."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time

import ota_upload


def argument_parser() -> argparse.ArgumentParser:
    """Create a small CLI compatible with the companion rv-control utility."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("RV_CONTROL_UI_OTA_HOST") or ota_upload.DEFAULT_HOST,
                        help="UI device hostname or IP address.")
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="Overall HTTP deadline in seconds (default: 10).")
    commands = parser.add_subparsers(dest="command", required=True)

    def add_command_options(command: argparse.ArgumentParser) -> None:
        """Allow shared options both before and after the command name."""
        command.add_argument("--host", default=argparse.SUPPRESS,
                             help="UI device hostname or IP address.")
        command.add_argument("--timeout", type=float, default=argparse.SUPPRESS,
                             help="Overall HTTP deadline in seconds.")

    get_config = commands.add_parser("get-config", help="Save the running device configuration privately.")
    add_command_options(get_config)
    get_config.add_argument("-o", "--output", type=Path, default=Path("config.json"),
                            help="Private local file for the configuration (default: config.json).")

    info = commands.add_parser("info", help="Print device and network diagnostics.")
    add_command_options(info)

    set_config = commands.add_parser("set-config", help="Replace device configuration from a JSON file.")
    add_command_options(set_config)
    set_config.add_argument("-i", "--input", dest="config_path", required=True, type=Path,
                            help="Local JSON configuration file to send.")

    restart = commands.add_parser("restart", help="Restart the device without changing configuration.")
    add_command_options(restart)
    return parser


def api_client(host: str, timeout: float) -> ota_upload.Client:
    """Create one bounded HTTP client for the selected workstation command."""
    if timeout < 0.1:
        raise ota_upload.OtaError("Timeout must be at least 0.1 seconds.")
    return ota_upload.Client(host, deadline=time.monotonic() + timeout)


def run(arguments: argparse.Namespace) -> None:
    """Execute one API operation without printing configuration contents."""
    client = api_client(arguments.host, arguments.timeout)
    key_file = Path(os.environ.get("RV_CONTROL_UI_OTA_KEY_FILE") or ota_upload.DEFAULT_KEY_FILE)
    if arguments.command == "info":
        result = client.request("GET", "/api/info")
        print(json.dumps(result, indent=2, sort_keys=True))
        return

    if arguments.command == "get-config":
        if arguments.output.resolve() == key_file.resolve():
            raise ota_upload.OtaError("Configuration output must not overwrite the OTA key file.")
        result = ota_upload.api(arguments.host, key_file, "GET", "/api/config", client=client)
        ota_upload.private_write(arguments.output, (json.dumps(result, indent=2, sort_keys=True) + "\n").encode("utf-8"))
        print(f"Configuration written privately to {arguments.output}.")
        return

    if arguments.command == "set-config":
        body = ota_upload.read_private_json(arguments.config_path)
        ota_upload.api(arguments.host, key_file, "POST", "/api/config", body=body, client=client)
        print("Configuration update accepted.")
        return

    ota_upload.api(arguments.host, key_file, "POST", "/api/restart", client=client)
    print("Restart request accepted.")


def main(argv: list[str] | None = None) -> int:
    """Run the API CLI and report sanitized local or HTTP errors."""
    arguments = argument_parser().parse_args(argv)
    try:
        run(arguments)
    except (ota_upload.OtaError, OSError, ValueError) as error:
        message = str(error) if isinstance(error, ota_upload.OtaError) else "UI API operation failed."
        print(f"UI API error: {message}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())