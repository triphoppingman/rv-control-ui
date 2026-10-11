"""Hardware-free unittest checks for workstation-only OTA tooling."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import stat
import struct
import sys
import tempfile
import threading
import time
from types import ModuleType
from typing import Any
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load_script(name: str) -> ModuleType:
    """Load a workstation script without requiring a package or SCons installation."""
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


ota = load_script("ota_upload")
hook = load_script("platformio_ota")
ui_api = load_script("ui_api")
KEY = bytes(range(32))
NONCE = "ab" * 32
ELF = "cd" * 32


def make_image(*, marker: bytes = ota.MARKER, appended_hash: bool = True,
               extra: bytes = b"") -> bytes:
    """Construct a checksummed ESP32-S3 app matching the verified SDK layout."""
    header = bytearray(24)
    header[0:2] = bytes((0xE9, 1))
    struct.pack_into("<H", header, 12, 9)
    header[23] = int(appended_hash)
    descriptor = bytearray(256)
    struct.pack_into("<I", descriptor, 0, 0xABCD5432)
    descriptor[144:176] = bytes.fromhex(ELF)
    data = bytes(descriptor) + marker + extra
    data += b"\0" * (-len(data) % 4)
    image = bytes(header) + struct.pack("<II", 0x3C000020, len(data)) + data
    checksum = 0xEF
    for byte in data:
        checksum ^= byte
    image += b"\0" * ((15 - len(image) % 16) % 16) + bytes((checksum,))
    return image + hashlib.sha256(image).digest() if appended_hash else image


class AuthTests(unittest.TestCase):
    """Pin exact canonical authentication bytes and invalid parameter handling."""

    def test_canonical_vector(self) -> None:
        """Compare the protocol vector to independently assembled literal bytes."""
        expected_message = (b"rv-control-ui-ota-v1\nPOST\n/api/firmware\n"
                            + b"ab" * 32 + b"\n123\n" + b"ef" * 32
                            + b"\nelecrow-crowpanel-128\n")
        headers = ota.auth_headers(KEY, "POST", "/api/firmware", NONCE, 123, "ef" * 32)
        self.assertEqual(headers["X-OTA-Auth"], hmac.new(KEY, expected_message, hashlib.sha256).hexdigest())
        self.assertEqual(headers["X-OTA-Auth"], "26e3f5812b50c4103dde1cc720ee7ab372df92f3d820f19985ffa2d3dd66ab47")
        self.assertEqual(headers["X-OTA-Size"], "123")
        self.assertEqual(headers["X-OTA-Target"], "elecrow-crowpanel-128")
        self.assertNotEqual(headers["X-OTA-Auth"], ota.auth_headers(b"\xff" * 32, "POST", "/api/firmware", NONCE, 123, "ef" * 32)["X-OTA-Auth"])

    def test_reject_invalid_parameters(self) -> None:
        """Reject malformed nonces and unapproved paths before signing."""
        for nonce, path in ((NONCE.upper(), "/api/firmware"), (NONCE, "/other")):
            with self.subTest(nonce=nonce, path=path), self.assertRaises(ota.OtaError):
                ota.auth_headers(KEY, "POST", path, nonce, 1, "ef" * 32)


class ImageTests(unittest.TestCase):
    """Check binary identity, bounds, marker, checksum, and appended digest."""

    def test_identity_and_digest(self) -> None:
        """Extract ELF identity separately from the exact binary SHA256."""
        for appended_hash in (False, True):
            data = make_image(appended_hash=appended_hash)
            image = ota.inspect_image(io.BytesIO(data))
            self.assertEqual(image.elf_sha256, ELF)
            self.assertEqual(image.sha256, hashlib.sha256(data).hexdigest())
            self.assertEqual(image.size, len(data))

    def test_malformed_and_truncated(self) -> None:
        """Reject damaged headers, descriptors, lengths, checksum, and hashes."""
        good = make_image()
        bad_images = [good[:size] for size in (0, 23, 31, 100, 287, len(good) - 1)]
        for offset, value in ((0, 0), (1, 0), (12, 2), (23, 2), (32, 0), (176, 1), (len(good) - 1, 0)):
            changed = bytearray(good)
            changed[offset] = value
            bad_images.append(bytes(changed))
        oversized = bytearray(good)
        struct.pack_into("<I", oversized, 28, 0xFFFFFFFC)
        bad_images.extend((bytes(oversized), good + b"extra", make_image(marker=b"wrong board"), make_image(marker=ota.MARKER[:-1] + b"x")))
        for index, data in enumerate(bad_images):
            with self.subTest(index=index), self.assertRaises(ota.OtaError):
                ota.inspect_image(io.BytesIO(data))

    def test_marker_crosses_chunk_boundary(self) -> None:
        """Find a NUL-terminated marker split between streamed segment reads."""
        descriptor_prefix = b"x" * (ota.CHUNK_SIZE - 256 - 12)
        data = make_image(marker=descriptor_prefix + ota.MARKER)
        self.assertEqual(ota.inspect_image(io.BytesIO(data)).elf_sha256, ELF)


class ProvisionTests(unittest.TestCase):
    """Exercise provisioning exclusively in disposable temporary directories."""

    def setUp(self) -> None:
        """Allocate isolated files, never touching device-local runtime config."""
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.config = Path(self.directory.name) / "config.json"
        self.key = Path(self.directory.name) / "secrets" / "ota.key"
        self.config.write_text('{"display":{"value":42}}', encoding="utf-8")

    def test_idempotent_private_and_rotation(self) -> None:
        """Preserve existing content and keys except during explicit rotation."""
        with patch.object(ota.secrets, "token_hex", return_value=KEY.hex()) as generate:
            self.assertTrue(ota.provision(self.config, self.key))
            original = self.config.read_bytes(), self.key.read_bytes()
            self.assertFalse(ota.provision(self.config, self.key))
            self.assertEqual(original, (self.config.read_bytes(), self.key.read_bytes()))
            generate.assert_called_once_with(32)
        self.assertEqual(json.loads(self.config.read_bytes())["display"], {"value": 42})
        for path in (self.config, self.key):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        with patch.object(ota.secrets, "token_hex", return_value="fe" * 32):
            self.assertTrue(ota.provision(self.config, self.key, rotate=True))
        self.assertEqual(ota.load_key(self.key), b"\xfe" * 32)

    def test_keygen_is_private_and_idempotent(self) -> None:
        """Create one key file without printing it or replacing it on repeat."""
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(ota.secrets, "token_hex", return_value=KEY.hex()) as generate:
            with redirect_stdout(output), redirect_stderr(errors):
                self.assertEqual(ota.main(["keygen", "--key-file", str(self.key)]), 0)
                first = self.key.read_bytes()
                self.assertEqual(ota.main(["keygen", "--key-file", str(self.key)]), 0)
        self.assertEqual(first, (KEY.hex() + "\n").encode("ascii"))
        self.assertEqual(self.key.read_bytes(), first)
        self.assertEqual(stat.S_IMODE(self.key.stat().st_mode), 0o600)
        self.assertNotIn(KEY.hex(), output.getvalue() + errors.getvalue())
        generate.assert_called_once_with(32)

    def test_conflict_preserves_both(self) -> None:
        """Refuse to silently replace an already provisioned mismatching key."""
        self.key.parent.mkdir()
        self.key.write_text(KEY.hex(), encoding="ascii")
        self.config.write_text(json.dumps({"ota": {"shared_secret": "fe" * 32}}), encoding="ascii")
        original = self.config.read_bytes(), self.key.read_bytes()
        with self.assertRaises(ota.OtaError):
            ota.provision(self.config, self.key)
        self.assertEqual(original, (self.config.read_bytes(), self.key.read_bytes()))

    def test_recover_key_without_generating(self) -> None:
        """Complete a partially provisioned pair using its existing local key."""
        self.key.parent.mkdir()
        self.key.write_text(KEY.hex(), encoding="ascii")
        with patch.object(ota.secrets, "token_hex") as generate:
            ota.provision(self.config, self.key)
            generate.assert_not_called()
        self.assertEqual(json.loads(self.config.read_bytes())["ota"]["shared_secret"], KEY.hex())

    def test_no_disclosure(self) -> None:
        """Keep generated key and configuration contents out of CLI streams."""
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(ota.secrets, "token_hex", return_value=KEY.hex()), redirect_stdout(output), redirect_stderr(errors):
            result = ota.main(["provision", "--config", str(self.config), "--key-file", str(self.key)])
        self.assertEqual(result, 0)
        self.assertNotIn(KEY.hex(), output.getvalue() + errors.getvalue())
        self.assertNotIn("display", output.getvalue() + errors.getvalue())

    def test_preserve_existing_uppercase_secret(self) -> None:
        """Treat uppercase hex as the same key without rewriting existing files."""
        self.key.parent.mkdir()
        self.key.write_text(KEY.hex().upper() + "\n", encoding="ascii")
        self.config.write_text(json.dumps({"ota": {"shared_secret": KEY.hex().upper()}}), encoding="ascii")
        original = self.config.read_bytes(), self.key.read_bytes()
        with patch.object(ota.secrets, "token_hex") as generate:
            self.assertFalse(ota.provision(self.config, self.key))
            generate.assert_not_called()
        self.assertEqual(original, (self.config.read_bytes(), self.key.read_bytes()))

    def test_malformed_config_preserved_and_sanitized(self) -> None:
        """Reject private malformed JSON without exposing it or writing a key."""
        content = b'{"private": "not-for-console",'
        self.config.write_bytes(content)
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors), patch.object(ota.secrets, "token_hex") as generate:
            result = ota.main(["provision", "--config", str(self.config), "--key-file", str(self.key)])
        self.assertEqual(result, 1)
        self.assertEqual(self.config.read_bytes(), content)
        self.assertFalse(self.key.exists())
        self.assertNotIn("not-for-console", output.getvalue() + errors.getvalue())
        generate.assert_not_called()


class FakeEnv:
    """Minimal SCons environment with observable uploads and option lookups."""

    def __init__(self, name: str, protocol: str = "custom", port: str = "") -> None:
        """Initialize only the variables used by the OTA hook."""
        self.name = name
        self.protocol = protocol
        self.port = port
        self.replacements: dict[str, object] = {}
        self.python = "/chosen/venv/bin/python"

    def subst(self, value: str) -> str:
        """Resolve fixed SCons expressions without reading key files."""
        return {"$PIOENV": self.name, "$UPLOAD_PORT": self.port,
                "$PROJECT_DIR": str(ROOT), "$PYTHONEXE": self.python,
                "$BUILD_DIR/${PROGNAME}.bin": "/build/firmware.bin"}[value]

    def GetProjectOption(self, name: str, default: str) -> str:
        """Expose the upload protocol using SCons' option method shape."""
        if name == "upload_protocol":
            return self.protocol
        if name == "custom_ota_host":
            return self.port
        return default

    def Replace(self, **values: object) -> None:
        """Record which construction variables the script modifies."""
        self.replacements.update(values)

    def get(self, name: str, default: object) -> object:
        """Provide the empty construction-target list used during SCons setup."""
        return default


class HookTests(unittest.TestCase):
    """Ensure the OTA hook cannot alter normal USB or filesystem actions."""

    def test_usb_is_noop(self) -> None:
        """Ignore USB targets entirely, including filesystem and key lookup."""
        env = FakeEnv("elecrow-crowpanel-128", protocol="esptool")
        with patch.object(hook, "python_command", side_effect=AssertionError("USB lookup")), patch.object(hook.subprocess, "call") as call:
            hook.install(env, ["uploadfs"], {})
        self.assertEqual(env.replacements, {})
        call.assert_not_called()

    def test_filesystem_rejected_early(self) -> None:
        """Reject uploadfs and buildfs before installing UPLOADCMD."""
        for target in ("uploadfs", "buildfs"):
            env = FakeEnv("elecrow-crowpanel-128-ota")
            with self.subTest(target=target), self.assertRaises(RuntimeError):
                hook.install(env, [target], {})
            self.assertEqual(env.replacements, {})

    def test_upload_callable_uses_scons_python_and_port(self) -> None:
        """Forward host, private key path, and app binary without a shell command."""
        env = FakeEnv("elecrow-crowpanel-128-ota", port="example.invalid:8080")
        hook.install(env, ["upload"], {"RV_CONTROL_UI_OTA_KEY_FILE": "/private/ota.key"})
        action = env.replacements["UPLOADCMD"]
        self.assertTrue(callable(action))
        with patch.object(hook.subprocess, "call", return_value=0) as call:
            self.assertEqual(action([], [], env), 0)
        self.assertEqual(call.call_args.args[0], ["/chosen/venv/bin/python", str(ROOT / "scripts" / "ota_upload.py"), "upload", "--host", "example.invalid:8080", "--key-file", "/private/ota.key", "--firmware", "/build/firmware.bin"])

    def test_host_fallback_and_no_unconfigured_python(self) -> None:
        """Use the host fallback and refuse an unknown non-venv interpreter."""
        env = FakeEnv("elecrow-crowpanel-128-ota")
        hook.install(env, [], {"RV_CONTROL_UI_OTA_HOST": "fallback.invalid"})
        with patch.object(hook.subprocess, "call", return_value=0) as call:
            env.replacements["UPLOADCMD"]([], [], env)
        self.assertIn("fallback.invalid", call.call_args.args[0])
        env = FakeEnv("elecrow-crowpanel-128-ota")
        hook.install(env, [], {})
        with patch.object(hook.subprocess, "call", return_value=0) as call:
            env.replacements["UPLOADCMD"]([], [], env)
        self.assertIn("192.168.77.1", call.call_args.args[0])
        env.python = "$PYTHONEXE"
        with patch.object(hook.sys, "prefix", "same"), patch.object(hook.sys, "base_prefix", "same"), self.assertRaises(RuntimeError):
            hook.python_command(env)

    def test_scons_injected_target_lists(self) -> None:
        """Reject either SCons target list during script execution, not at upload."""
        module = ModuleType("SCons.Script")
        for global_targets, command_targets in ((["buildfs"], []), ([], ["uploadfs"])):
            module.COMMAND_LINE_TARGETS = command_targets
            env = FakeEnv("elecrow-crowpanel-128-ota")

            def import_env(name: str) -> None:
                """Emulate SCons Import with an already injected construction env."""
                self.assertEqual(name, "env")

            with self.subTest(global_targets=global_targets, command_targets=command_targets), patch.dict(sys.modules, {"SCons.Script": module}), self.assertRaises(RuntimeError):
                runpy.run_path(str(ROOT / "scripts" / "platformio_ota.py"), init_globals={"Import": import_env, "env": env, "TARGETS": global_targets})
            self.assertEqual(env.replacements, {})


class UiApiTests(unittest.TestCase):
    """Cover the standard-library CLI's command compatibility and privacy."""

    def test_get_config_writes_privately(self) -> None:
        """Save config data to a private file without printing its contents."""
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "device.json"
            key_file = Path(directory) / "outside" / "ota.key"
            stdout = io.StringIO()
            response = {"wifi": {"password": "private-test-value"}}
            with patch.dict(os.environ, {"RV_CONTROL_UI_OTA_KEY_FILE": str(key_file)}), \
                    patch.object(ui_api.ota_upload, "api", return_value=response) as request, redirect_stdout(stdout):
                result = ui_api.main(["get-config", "--host", "127.0.0.1", "--output", str(output)])
            self.assertEqual(result, 0)
            self.assertEqual(request.call_args.args[1], key_file)
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), response)
            self.assertNotIn("private-test-value", stdout.getvalue())

    def test_set_config_and_restart_delegate_authenticated_api(self) -> None:
        """Preserve set-config/restart commands and support root or command options."""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "config.json"
            source.write_text('{"display":{"brightness":42}}', encoding="utf-8")
            with patch.object(ui_api.ota_upload, "api", return_value={"status": "ok"}) as request:
                self.assertEqual(ui_api.main(["--host", "127.0.0.1", "set-config", "--input", str(source)]), 0)
                self.assertEqual(ui_api.main(["restart", "--host", "127.0.0.1"]), 0)
            self.assertEqual(request.call_args_list[0].kwargs["body"], source.read_bytes())
            self.assertEqual(request.call_args_list[1].args[2:4], ("POST", "/api/restart"))

    def test_parser_defaults_to_hotspot_and_rejects_short_timeout(self) -> None:
        """Keep the existing host default and validate an unusable deadline."""
        arguments = ui_api.argument_parser().parse_args(["info"])
        self.assertEqual(arguments.host, "192.168.77.1")
        with self.assertRaises(ota.OtaError):
            ui_api.api_client(arguments.host, 0.01)


class MockDevice:
    """Loopback-only device with independent signature verification and state."""

    def __init__(self) -> None:
        """Start a mock HTTP server without contacting real RV hardware."""
        self.enabled = True
        self.failure_status = 0
        self.drop_response = False
        self.pending_polls = 0
        self.final_state = "valid"
        self.wrong_identity = False
        self.uploaded = b""
        self.posts = 0
        self.info_calls = 0
        self.challenge_calls = 0
        self.api_calls = 0
        self.nonce = ""
        self.challenge_override: dict[str, object] = {}
        self.api_bodies: list[bytes] = []
        self.stall_headers = False
        self.release = threading.Event()
        device = self

        class Handler(BaseHTTPRequestHandler):
            """Serve only the firmware contract with suppressed HTTP access logs."""

            def log_message(self, format: str, *args: object) -> None:
                """Prevent test request logs and any sensitive payload disclosure."""
                return

            def reply(self, data: dict[str, object], status: int = 200) -> None:
                """Send one bounded JSON response or tolerate a disconnected client."""
                encoded = json.dumps(data).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                try:
                    self.wfile.write(encoded)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def authenticated(self, body: bytes) -> bool:
                """Independently verify the exact wire request fields and HMAC."""
                if not device.enabled and self.path != "/api/firmware":
                    return "X-OTA-Auth" not in self.headers
                digest = hashlib.sha256(body).hexdigest()
                message = (f"rv-control-ui-ota-v1\n{self.command}\n{self.path}\n"
                           f"{device.nonce}\n{len(body)}\n{digest}\nelecrow-crowpanel-128\n").encode("ascii")
                expected = hmac.new(KEY, message, hashlib.sha256).hexdigest()
                accepted = bool(device.nonce) and (
                    self.headers.get("X-OTA-Nonce") == device.nonce
                    and self.headers.get("X-OTA-Size") == str(len(body))
                    and self.headers.get("X-OTA-SHA256") == digest
                    and self.headers.get("X-OTA-Target") == "elecrow-crowpanel-128"
                    and hmac.compare_digest(self.headers.get("X-OTA-Auth", ""), expected))
                device.nonce = ""
                return accepted

            def do_GET(self) -> None:
                """Serve preflight, challenge, confirmation, and protected config."""
                if device.stall_headers:
                    self.wfile.write(b"HTTP/1.1 200 OK\r\n")
                    self.wfile.flush()
                    device.release.wait(2)
                    return
                if self.path == "/api/info":
                    device.info_calls += 1
                    uploaded = bool(device.uploaded)
                    state = "pending" if uploaded and device.pending_polls else device.final_state
                    if uploaded and device.pending_polls:
                        device.pending_polls -= 1
                    self.reply({"build": {"elf_sha256": ELF if uploaded and not device.wrong_identity else "11" * 32},
                                "device": {"model": "test"},
                                "ota": {"enabled": device.enabled, "state": state,
                                        "target": "elecrow-crowpanel-128", "capacity": 2 * 1024 * 1024}})
                elif self.path == "/api/ota/challenge":
                    device.challenge_calls += 1
                    device.nonce = f"{device.challenge_calls:064x}"
                    self.reply({"nonce": device.nonce, "target": "elecrow-crowpanel-128",
                                "capacity": 2 * 1024 * 1024, "enabled": device.enabled,
                                "expires_in_ms": 60000, **device.challenge_override})
                elif self.path == "/api/config" and self.authenticated(b""):
                    device.api_calls += 1
                    self.reply({"ota": {"shared_secret": KEY.hex()}, "wifi": {"password": "test-only-private-value"}})
                else:
                    self.reply({"private": KEY.hex()}, 403)

            def do_POST(self) -> None:
                """Verify exact request length/signature and emulate an OTA reboot."""
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                if self.path == "/api/firmware":
                    device.posts += 1
                if not self.authenticated(body):
                    self.reply({"private": KEY.hex()}, 403)
                elif device.failure_status:
                    self.reply({"private": KEY.hex()}, device.failure_status)
                elif self.path == "/api/firmware":
                    if self.headers.get("Content-Type") != "application/octet-stream":
                        self.reply({}, 415)
                        return
                    device.uploaded = body
                    if device.drop_response:
                        self.close_connection = True
                        self.connection.close()
                    else:
                        self.reply({"accepted": True})
                elif self.path in ("/api/config", "/api/restart"):
                    device.api_calls += 1
                    device.api_bodies.append(body)
                    self.reply({"ok": True})
                else:
                    self.reply({}, 404)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.host = f"127.0.0.1:{self.server.server_port}"

        def serve() -> None:
            """Run the temporary loopback server with prompt shutdown polling."""
            self.server.serve_forever(poll_interval=0.01)

        self.thread = threading.Thread(target=serve, daemon=True)
        self.thread.start()

    def close(self) -> None:
        """Release waiting handlers and stop the server at test cleanup."""
        self.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


class ServerTests(unittest.TestCase):
    """Exercise streamed uploads, no replay, deadlines, and protected API calls."""

    def setUp(self) -> None:
        """Allocate deterministic test-only keys/images and a loopback device."""
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.key = Path(self.directory.name) / "ota.key"
        self.key.write_text(KEY.hex(), encoding="ascii")
        self.firmware = Path(self.directory.name) / "firmware.bin"
        self.binary = make_image(extra=b"payload" * 20000)
        self.firmware.write_bytes(self.binary)
        self.device = MockDevice()
        self.addCleanup(self.device.close)

    def client(self, seconds: float = 2.0) -> Any:
        """Create a client with a short bounded test deadline."""
        return ota.Client(self.device.host, deadline=time.monotonic() + seconds)

    def test_stream_success_and_pending_confirmation(self) -> None:
        """Send multiple chunks and wait for valid, not merely matching/pending."""
        self.device.pending_polls = 2
        with patch.object(ota, "POLL_INTERVAL", 0.01):
            image = ota.upload(self.device.host, self.key, self.firmware, client=self.client())
        self.assertEqual(image.elf_sha256, ELF)
        self.assertEqual(self.device.uploaded, self.binary)
        self.assertEqual(self.device.posts, 1)
        self.assertEqual(self.device.info_calls, 4)

    def test_lost_response_reconciles_without_replay(self) -> None:
        """Treat a dropped POST response as unknown and confirm solely via GET."""
        self.device.drop_response = True
        ota.upload(self.device.host, self.key, self.firmware, client=self.client())
        self.assertEqual(self.device.posts, 1)
        self.assertEqual(self.device.uploaded, self.binary)

    def test_wrong_key_and_http_failure(self) -> None:
        """Stop on definite rejection without replaying or leaking response bodies."""
        self.key.write_text("ff" * 32, encoding="ascii")
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            result = ota.main(["upload", "--host", self.device.host, "--key-file", str(self.key), "--firmware", str(self.firmware)])
        self.assertEqual(result, 1)
        self.assertEqual(self.device.posts, 1)
        self.assertNotIn(KEY.hex(), output.getvalue() + errors.getvalue())
        self.assertNotIn("ff" * 32, output.getvalue() + errors.getvalue())
        self.key.write_text(KEY.hex(), encoding="ascii")
        self.device.failure_status = 500
        with self.assertRaises(ota.HttpError):
            ota.upload(self.device.host, self.key, self.firmware, client=self.client())
        self.assertEqual(self.device.posts, 2)

    def test_reject_unmanaged_wrong_identity_and_pending(self) -> None:
        """Require both the exact ELF and valid state before the deadline."""
        for state, wrong in (("unmanaged", False), ("pending", False), ("valid", True)):
            self.device.uploaded = b""
            self.device.final_state = state
            self.device.wrong_identity = wrong
            with self.subTest(state=state, wrong=wrong), patch.object(ota, "POLL_INTERVAL", 0.01), self.assertRaises(ota.TransportError):
                ota.upload(self.device.host, self.key, self.firmware, client=self.client(0.15))
        self.assertEqual(self.device.posts, 3)

    def test_disabled_and_invalid_challenge_prevent_post(self) -> None:
        """Reject disabled OTA and mismatched challenge fields before any write."""
        self.device.enabled = False
        with self.assertRaises(ota.OtaError):
            ota.upload(self.device.host, self.key, self.firmware, client=self.client())
        self.device.enabled = True
        for field, value in (("nonce", NONCE.upper()), ("target", "other"), ("capacity", 123), ("expires_in_ms", 59999), ("enabled", False)):
            self.device.challenge_override = {field: value}
            with self.subTest(field=field), self.assertRaises(ota.OtaError):
                ota.upload(self.device.host, self.key, self.firmware, client=self.client())
        self.assertEqual(self.device.posts, 0)

    def test_api_authenticated_and_private_output(self) -> None:
        """Authenticate GET and POST exact JSON bytes, never printing responses."""
        output, errors = io.StringIO(), io.StringIO()
        destination = Path(self.directory.name) / "response.json"
        with redirect_stdout(output), redirect_stderr(errors):
            result = ota.main(["api", "GET", "/api/config", "--host", self.device.host, "--key-file", str(self.key), "--output", str(destination)])
        self.assertEqual(result, 0)
        self.assertEqual(stat.S_IMODE(destination.stat().st_mode), 0o600)
        self.assertIn(KEY.hex(), destination.read_text(encoding="utf-8"))
        self.assertNotIn(KEY.hex(), output.getvalue() + errors.getvalue())
        self.assertNotIn("test-only-private-value", output.getvalue() + errors.getvalue())
        body = b'{ "display": {"brightness": 42} }\n'
        ota.api(self.device.host, self.key, "POST", "/api/config", body=body, client=self.client())
        ota.api(self.device.host, self.key, "POST", "/api/restart", client=self.client())
        self.assertEqual(self.device.api_bodies, [body, b""])
        self.assertEqual(self.device.challenge_calls, 3)

    def test_disabled_api_does_not_require_key(self) -> None:
        """Allow local config access without authentication only when disabled."""
        self.device.enabled = False
        self.key.unlink()
        result = ota.api(self.device.host, self.key, "GET", "/api/config", client=self.client())
        self.assertIn("ota", result)
        self.assertEqual(self.device.challenge_calls, 0)

    def test_stalled_headers_obey_overall_deadline(self) -> None:
        """Interrupt a server that sends a status line but never completes headers."""
        self.device.stall_headers = True
        started = time.monotonic()
        with self.assertRaises(ota.TransportError):
            self.client(0.1).info()
        self.assertLess(time.monotonic() - started, 1.0)

    def test_dns_resolution_obeys_connect_budget(self) -> None:
        """Bound a stuck OS resolver without needing an external DNS service."""
        release = threading.Event()
        self.addCleanup(release.set)

        def resolve(*args: object, **kwargs: object) -> list[object]:
            """Simulate an OS resolver that never returns during the test budget."""
            release.wait(2)
            return []

        started = time.monotonic()
        with patch.object(ota.socket, "getaddrinfo", side_effect=resolve), self.assertRaises(ota.TransportError):
            self.client(0.1).info()
        release.set()
        self.assertLess(time.monotonic() - started, 1.0)


if __name__ == "__main__":
    unittest.main()