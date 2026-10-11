"""Workstation OTA client; HTTP responses are untrusted, not authenticated.

Requests use HMAC-SHA256, but plain HTTP provides neither response authenticity
nor confidentiality. Configuration bodies can contain secrets and are written
only to explicitly selected private files. Firmware POSTs are never retried.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import queue
import re
import secrets
import socket
import struct
import sys
import tempfile
import threading
import time
from typing import BinaryIO, Mapping
from urllib.parse import urlsplit


TARGET = "elecrow-crowpanel-128"
MARKER = b"RV_CONTROL_UI_TARGET:elecrow-crowpanel-128:v1\0"
DEFAULT_HOST = "192.168.77.1"
DEFAULT_KEY_FILE = Path(__file__).resolve().parent.parent / "secrets" / "ota.key"
CONNECT_TIMEOUT = 5.0
READ_TIMEOUT = 10.0
OVERALL_TIMEOUT = 180.0
POLL_INTERVAL = 1.0
CHUNK_SIZE = 65536
MAX_JSON_SIZE = 65536
MAX_IMAGE_SIZE = 16 * 1024 * 1024
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
ELF_HEX64 = re.compile(r"[0-9a-fA-F]{64}\Z")
IMAGE_HEADER_SIZE = 24
SEGMENT_HEADER_SIZE = 8
APP_DESC_SIZE = 256
APP_DESC_MAGIC = 0xABCD5432
APP_ELF_SHA_OFFSET = 144


class OtaError(Exception):
    """Represent a sanitized user-facing protocol or local validation failure."""


class TransportError(OtaError):
    """Represent an HTTP transport failure with potentially unknown POST outcome."""


class HttpError(OtaError):
    """Represent a definite non-success HTTP response without exposing its body."""

    def __init__(self, status: int) -> None:
        """Retain only the response status, never device-supplied error text."""
        self.status = status
        super().__init__(f"Device returned HTTP {status}.")


@dataclass(frozen=True)
class Image:
    """Describe the exact binary and the ELF identity embedded by ESP-IDF."""

    size: int
    sha256: str
    elf_sha256: str


def is_hex64(value: object) -> bool:
    """Recognize canonical lowercase 256-bit hexadecimal protocol fields."""
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def auth_headers(key: bytes, method: str, path: str, nonce: str,
                 size: int, sha256: str, target: str = TARGET) -> dict[str, str]:
    """Sign the exact newline-terminated v1 ASCII message for a request."""
    if (len(key) != 32 or not is_hex64(nonce) or not is_hex64(sha256)
            or type(size) is not int or size < 0 or target != TARGET
            or (method, path) not in {("POST", "/api/firmware"),
                                     ("GET", "/api/config"),
                                     ("POST", "/api/config"),
                                     ("POST", "/api/restart")}):
        raise OtaError("Invalid authentication parameters.")
    message = f"rv-control-ui-ota-v1\n{method}\n{path}\n{nonce}\n{size}\n{sha256}\n{target}\n"
    return {
        "X-OTA-Nonce": nonce,
        "X-OTA-Size": str(size),
        "X-OTA-SHA256": sha256,
        "X-OTA-Target": target,
        "X-OTA-Auth": hmac.new(key, message.encode("ascii"), hashlib.sha256).hexdigest(),
    }


def load_key(path: Path) -> bytes:
    """Read a private, bounded key file without including its contents in errors."""
    try:
        with path.open("rb") as stream:
            raw = stream.read(129)
        text = raw.decode("ascii").strip()
    except (OSError, UnicodeError) as error:
        raise OtaError("Cannot read the OTA key file.") from error
    if len(raw) > 128 or ELF_HEX64.fullmatch(text) is None:
        raise OtaError("OTA key file must contain 64 hexadecimal characters.")
    return bytes.fromhex(text)


def read_exact(stream: BinaryIO, size: int) -> bytes:
    """Reject truncated image structures instead of unpacking partial bytes."""
    data = stream.read(size)
    if len(data) != size:
        raise OtaError("Truncated ESP application image.")
    return data


def inspect_image(stream: BinaryIO) -> Image:
    """Validate an unsigned ESP32-S3 application using installed SDK layouts.

    esp_app_format.h defines a 24-byte image header, an 8-byte segment header,
    and a 256-byte esp_app_desc_t with app_elf_sha256 at descriptor offset 144.
    The descriptor starts at offset 32 in the first (DROM) segment.
    """
    stream.seek(0, os.SEEK_END)
    size = stream.tell()
    if not IMAGE_HEADER_SIZE + SEGMENT_HEADER_SIZE + APP_DESC_SIZE <= size <= MAX_IMAGE_SIZE:
        raise OtaError("Firmware image size is invalid.")
    stream.seek(0)
    header = read_exact(stream, IMAGE_HEADER_SIZE)
    if header[0] != 0xE9 or struct.unpack_from("<H", header, 12)[0] != 9:
        raise OtaError("Firmware is not an ESP32-S3 application image.")
    if not 1 <= header[1] <= 16 or header[23] not in (0, 1):
        raise OtaError("Malformed ESP image header.")
    checksum = 0xEF
    marker_found = False
    elf_sha256 = ""
    for segment_index in range(header[1]):
        segment_header = read_exact(stream, SEGMENT_HEADER_SIZE)
        length = struct.unpack_from("<I", segment_header, 4)[0]
        if length % 4 or length > size - stream.tell():
            raise OtaError("Malformed ESP image segment length.")
        if segment_index == 0:
            if length < APP_DESC_SIZE:
                raise OtaError("Missing ESP application descriptor.")
            descriptor = read_exact(stream, APP_DESC_SIZE)
            if struct.unpack_from("<I", descriptor)[0] != APP_DESC_MAGIC:
                raise OtaError("Invalid ESP application descriptor magic.")
            elf_sha256 = descriptor[APP_ELF_SHA_OFFSET:APP_ELF_SHA_OFFSET + 32].hex()
            if elf_sha256 == "0" * 64:
                raise OtaError("Missing application ELF identity.")
            stream.seek(-APP_DESC_SIZE, os.SEEK_CUR)
        tail = b""
        remaining = length
        while remaining:
            block = read_exact(stream, min(CHUNK_SIZE, remaining))
            for byte in block:
                checksum ^= byte
            combined = tail + block
            marker_found = marker_found or MARKER in combined
            tail = combined[-(len(MARKER) - 1):]
            remaining -= len(block)
    padding_length = (15 - stream.tell() % 16) % 16
    if read_exact(stream, padding_length) != b"\0" * padding_length:
        raise OtaError("Malformed ESP image checksum padding.")
    if read_exact(stream, 1)[0] != checksum:
        raise OtaError("ESP image checksum mismatch.")
    hash_end = stream.tell()
    appended_hash = read_exact(stream, 32) if header[23] else None
    if stream.tell() != size:
        raise OtaError("Unsupported trailing data or signed ESP image.")
    if not marker_found:
        raise OtaError("Firmware lacks the required board target marker.")
    stream.seek(0)
    file_hash = hashlib.sha256()
    image_hash = hashlib.sha256()
    position = 0
    while block := stream.read(CHUNK_SIZE):
        file_hash.update(block)
        if position < hash_end:
            image_hash.update(block[:hash_end - position])
        position += len(block)
    if appended_hash is not None and not hmac.compare_digest(appended_hash, image_hash.digest()):
        raise OtaError("ESP image appended SHA256 mismatch.")
    stream.seek(0)
    return Image(size, file_hash.hexdigest(), elf_sha256)


def parse_json(data: bytes) -> dict[str, object]:
    """Parse bounded JSON objects without exposing JSON contents in errors."""
    if len(data) > MAX_JSON_SIZE:
        raise OtaError("JSON exceeds the allowed size.")
    try:
        value = json.loads(data)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise OtaError("Invalid JSON object.") from error
    if not isinstance(value, dict):
        raise OtaError("Expected a JSON object.")
    return value


def read_private_json(path: Path) -> bytes:
    """Read a bounded local JSON file without copying an arbitrarily large file."""
    with path.open("rb") as stream:
        data = stream.read(MAX_JSON_SIZE + 1)
    parse_json(data)
    return data


def positive_capacity(value: object) -> int:
    """Validate untrusted device capacity before copying or sending firmware."""
    if type(value) is not int or not 1 <= value <= MAX_IMAGE_SIZE:
        raise OtaError("Device reported an invalid OTA capacity.")
    return value


def validate_info(info: dict[str, object]) -> tuple[dict[str, object], str]:
    """Validate the device OTA contract and normalize the running ELF identity."""
    ota = info.get("ota")
    build = info.get("build")
    if not isinstance(ota, dict) or not isinstance(build, dict):
        raise OtaError("Device info lacks OTA/build metadata.")
    elf = build.get("elf_sha256")
    if not isinstance(elf, str) or ELF_HEX64.fullmatch(elf) is None:
        raise OtaError("Device reported an invalid ELF identity.")
    if (ota.get("target") != TARGET or type(ota.get("enabled")) is not bool
            or ota.get("state") not in ("pending", "valid", "unmanaged")):
        raise OtaError("Device reported incompatible OTA metadata.")
    positive_capacity(ota.get("capacity"))
    return ota, elf.lower()


class BoundedConnection(http.client.HTTPConnection):
    """Bound DNS resolution as well as TCP connection establishment."""

    def connect(self) -> None:
        """Resolve in a daemon worker and connect within one shared connect budget."""
        end = time.monotonic() + float(self.timeout)
        results: queue.Queue[object] = queue.Queue(maxsize=1)

        def resolve() -> None:
            """Return resolver results without letting an OS DNS stall block the CLI."""
            try:
                results.put(socket.getaddrinfo(self.host, self.port, type=socket.SOCK_STREAM))
            except OSError as error:
                results.put(error)

        worker = threading.Thread(target=resolve, daemon=True)
        worker.start()
        try:
            addresses = results.get(timeout=max(0.001, end - time.monotonic()))
        except queue.Empty as error:
            raise TimeoutError("DNS connect budget expired.") from error
        if isinstance(addresses, OSError):
            raise addresses
        if not isinstance(addresses, list):
            raise OSError("Invalid resolver result.")
        for family, kind, protocol, _, address in addresses[:8]:
            remaining = end - time.monotonic()
            if remaining <= 0:
                break
            candidate = socket.socket(family, kind, protocol)
            try:
                candidate.settimeout(remaining)
                candidate.connect(address)
                self.sock = candidate
                return
            except OSError:
                candidate.close()
        raise TimeoutError("TCP connect budget expired or connection failed.")


class Client:
    """Provide bounded plain-HTTP exchanges without redirect or retry behavior."""

    def __init__(self, host: str, *, deadline: float | None = None) -> None:
        """Accept a host/port or plain HTTP origin and establish one total deadline."""
        try:
            parsed = urlsplit(host if "://" in host else "http://" + host)
            port = parsed.port or 80
            hostname = parsed.hostname
        except ValueError as error:
            raise OtaError("Invalid OTA host.") from error
        if (parsed.scheme != "http" or not hostname or parsed.username is not None
                or parsed.password is not None or parsed.path not in ("", "/")
                or parsed.query or parsed.fragment):
            raise OtaError("OTA host must be a plain HTTP origin without credentials or paths.")
        self.hostname = hostname
        self.port = port
        self.deadline = deadline if deadline is not None else time.monotonic() + OVERALL_TIMEOUT

    def remaining(self) -> float:
        """Reject work once the overall monotonic deadline has elapsed."""
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TransportError("OTA overall deadline expired.")
        return remaining

    def request(self, method: str, path: str, *, body: bytes | BinaryIO = b"",
                size: int = 0, headers: Mapping[str, str] | None = None,
                expect_json: bool = True) -> dict[str, object]:
        """Stream one exact-length request; sanitize errors and bound responses."""
        connection = BoundedConnection(
            self.hostname, self.port, timeout=min(CONNECT_TIMEOUT, self.remaining()))
        watchdog: threading.Timer | None = None
        guard_socket: socket.socket | None = None
        try:
            connection.connect()
            if connection.sock is None:
                raise TransportError("HTTP socket unavailable.")
            guard_socket = connection.sock.dup()

            def abort_request() -> None:
                """Interrupt even slow header/body reads at the overall deadline."""
                try:
                    if guard_socket is not None:
                        guard_socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

            watchdog = threading.Timer(self.remaining(), abort_request)
            watchdog.daemon = True
            watchdog.start()
            connection.putrequest(method, path)
            connection.putheader("Content-Length", str(size))
            for name, value in (headers or {}).items():
                connection.putheader(name, value)
            if connection.sock is not None:
                connection.sock.settimeout(min(READ_TIMEOUT, self.remaining()))
            connection.endheaders()
            sent = 0
            while sent < size:
                block = body[sent:sent + CHUNK_SIZE] if isinstance(body, bytes) else body.read(min(CHUNK_SIZE, size - sent))
                if not block or len(block) > size - sent:
                    raise OtaError("Request body does not match its declared size.")
                if connection.sock is not None:
                    connection.sock.settimeout(min(READ_TIMEOUT, self.remaining()))
                connection.send(block)
                sent += len(block)
            if connection.sock is not None:
                connection.sock.settimeout(min(READ_TIMEOUT, self.remaining()))
            response = connection.getresponse()
            self.remaining()
            if not 200 <= response.status < 300:
                raise HttpError(response.status)
            if not expect_json:
                return {}
            data = bytearray()
            while True:
                self.remaining()
                if connection.sock is not None:
                    connection.sock.settimeout(min(READ_TIMEOUT, self.remaining()))
                block = response.read1(min(CHUNK_SIZE, MAX_JSON_SIZE + 1 - len(data)))
                if not block:
                    break
                data.extend(block)
                if len(data) > MAX_JSON_SIZE:
                    raise OtaError("Device JSON response exceeds the allowed size.")
            self.remaining()
            return parse_json(bytes(data))
        except (OSError, http.client.HTTPException) as error:
            raise TransportError("HTTP transport failed; any POST outcome may be unknown.") from error
        finally:
            if watchdog is not None:
                watchdog.cancel()
            connection.close()
            if guard_socket is not None:
                guard_socket.close()

    def info(self) -> tuple[dict[str, object], str]:
        """Fetch untrusted device metadata for compatibility and reconciliation."""
        return validate_info(self.request("GET", "/api/info"))

    def challenge(self, capacity: int, *, require_enabled: bool = True) -> str:
        """Require a fresh canonical challenge consistent with preflight info."""
        challenge = self.request("GET", "/api/ota/challenge")
        if (not is_hex64(challenge.get("nonce")) or challenge.get("target") != TARGET
                or positive_capacity(challenge.get("capacity")) != capacity
                or type(challenge.get("enabled")) is not bool
                or (require_enabled and challenge.get("enabled") is not True)
                or type(challenge.get("expires_in_ms")) is not int
                or challenge.get("expires_in_ms") != 60000):
            raise OtaError("Device returned an invalid or incompatible OTA challenge.")
        return str(challenge["nonce"])

    def confirm(self, image: Image) -> None:
        """Poll only GET info until the uploaded ELF is running and OTA-valid."""
        while self.remaining() > 0:
            try:
                ota, elf_sha256 = self.info()
                if elf_sha256 == image.elf_sha256 and ota["state"] == "valid":
                    return
            except TransportError:
                pass
            time.sleep(min(POLL_INTERVAL, self.remaining()))
        raise TransportError("OTA was not confirmed before the deadline.")


def upload(host: str, key_file: Path, firmware: Path, *, client: Client | None = None) -> Image:
    """Preflight, snapshot, hash, stream exactly once, and confirm running identity."""
    key = load_key(key_file)
    connection = client if client is not None else Client(host)
    ota, _ = connection.info()
    if ota["enabled"] is not True:
        raise OtaError("OTA is disabled on the device.")
    capacity = positive_capacity(ota["capacity"])
    with tempfile.TemporaryFile("w+b") as snapshot:
        try:
            with firmware.open("rb") as source:
                copied = 0
                while block := source.read(CHUNK_SIZE):
                    connection.remaining()
                    copied += len(block)
                    if copied > capacity:
                        raise OtaError("Firmware exceeds the device OTA capacity.")
                    snapshot.write(block)
        except OSError as error:
            raise OtaError("Cannot read firmware or create its private snapshot.") from error
        image = inspect_image(snapshot)
        nonce = connection.challenge(capacity)
        headers = auth_headers(key, "POST", "/api/firmware", nonce, image.size, image.sha256)
        headers["Content-Type"] = "application/octet-stream"
        try:
            connection.request("POST", "/api/firmware", body=snapshot,
                               size=image.size, headers=headers, expect_json=False)
        except TransportError:
            pass
        connection.confirm(image)
    return image


def private_write(path: Path, data: bytes) -> None:
    """Atomically replace a local file with mode 0600 using a same-directory temp."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".ota-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            os.fchmod(stream.fileno(), 0o600)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def generate_key(key_file: Path) -> bool:
    """Create one local 256-bit key without replacing an existing key."""
    if key_file.exists():
        load_key(key_file)
        key_file.chmod(0o600)
        return False
    private_write(key_file, (secrets.token_hex(32) + "\n").encode("ascii"))
    return True


def provision(config_file: Path, key_file: Path, *, rotate: bool = False) -> bool:
    """Synchronize local shared_secret/key files; never print or replace conflicts.

    A two-file update cannot be transactional across a crash. Write the key first
    so an interrupted operation can be completed by rerunning without rotation.
    """
    if config_file.resolve() == key_file.resolve():
        raise OtaError("Configuration and key files must be different.")
    try:
        config = parse_json(read_private_json(config_file))
        ota = config.get("ota", {})
        if not isinstance(ota, dict):
            raise OtaError("Configuration has an invalid OTA object.")
        existing = ota.get("shared_secret", "")
        if not isinstance(existing, str) or (existing and ELF_HEX64.fullmatch(existing) is None):
            raise OtaError("Configuration has an invalid OTA shared secret.")
        normalized = existing.lower()
        local = load_key(key_file).hex() if key_file.exists() and not rotate else ""
        if not rotate and normalized and local and not hmac.compare_digest(normalized, local):
            raise OtaError("Existing OTA keys disagree; explicit --rotate is required.")
        secret = secrets.token_hex(32) if rotate else normalized or local or secrets.token_hex(32)
        changed = rotate or normalized != secret or not local
        if rotate or local != secret:
            private_write(key_file, (secret + "\n").encode("ascii"))
        else:
            key_file.chmod(0o600)
        if rotate or normalized != secret:
            ota["shared_secret"] = secret
            config["ota"] = ota
            private_write(config_file, (json.dumps(config, indent=2) + "\n").encode("utf-8"))
        else:
            config_file.chmod(0o600)
        return changed
    except OSError as error:
        raise OtaError("Cannot update private provisioning files.") from error


def api(host: str, key_file: Path, method: str, path: str, *, body: bytes = b"",
        client: Client | None = None) -> dict[str, object]:
    """Call only the config/restart API, authenticating when OTA protection is on."""
    if (method, path) not in {("GET", "/api/config"), ("POST", "/api/config"), ("POST", "/api/restart")}:
        raise OtaError("Unsupported API method/path.")
    if method == "GET" and body:
        raise OtaError("GET config must not have a body.")
    if body:
        parse_json(body)
    if path == "/api/config" and method == "POST" and not body:
        raise OtaError("POST config requires a JSON object.")
    connection = client if client is not None else Client(host)
    ota, _ = connection.info()
    headers = {"Content-Type": "application/json"} if body else {}
    if ota["enabled"] is True:
        key = load_key(key_file)
        nonce = connection.challenge(positive_capacity(ota["capacity"]))
        headers.update(auth_headers(key, method, path, nonce, len(body), hashlib.sha256(body).hexdigest()))
    return connection.request(method, path, body=body, size=len(body), headers=headers)


def argument_parser() -> argparse.ArgumentParser:
    """Build an explicit CLI with private file-based input/output defaults."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    upload_parser = commands.add_parser("upload", help="Upload and confirm an application image")
    api_parser = commands.add_parser("api", help="Call the protected local config/restart API")
    for command in (upload_parser, api_parser):
        command.add_argument("--host", default=os.environ.get("RV_CONTROL_UI_OTA_HOST") or DEFAULT_HOST)
        command.add_argument("--key-file", type=Path, default=os.environ.get("RV_CONTROL_UI_OTA_KEY_FILE") or DEFAULT_KEY_FILE)
    upload_parser.add_argument("--firmware", required=True, type=Path)
    api_parser.add_argument("method", choices=("GET", "POST"))
    api_parser.add_argument("path", choices=("/api/config", "/api/restart"))
    api_parser.add_argument("--input", type=Path, help="Private JSON request file (not inline JSON)")
    api_parser.add_argument("--output", type=Path, help="Write response JSON privately; never print it")
    keygen_parser = commands.add_parser("keygen", help="Create or verify a local private OTA key")
    keygen_parser.add_argument("--key-file", type=Path, default=os.environ.get("RV_CONTROL_UI_OTA_KEY_FILE") or DEFAULT_KEY_FILE)
    provision_parser = commands.add_parser("provision", help="Provision local files, without contacting hardware")
    provision_parser.add_argument("--config", type=Path, default=DEFAULT_KEY_FILE.parent.parent / "data" / "config.json")
    provision_parser.add_argument("--key-file", type=Path, default=os.environ.get("RV_CONTROL_UI_OTA_KEY_FILE") or DEFAULT_KEY_FILE)
    provision_parser.add_argument("--rotate", action="store_true", help="Explicitly replace existing shared keys")
    return parser


def main(argv: list[str] | None = None) -> int:
    """Run a command with sanitized diagnostics and no config/key disclosure."""
    arguments = argument_parser().parse_args(argv)
    try:
        if arguments.command == "provision":
            changed = provision(arguments.config, arguments.key_file, rotate=arguments.rotate)
            print("Local OTA provisioning updated." if changed else "Local OTA provisioning unchanged.")
        elif arguments.command == "keygen":
            created = generate_key(arguments.key_file)
            print("Local OTA key created." if created else "Local OTA key already exists and is valid.")
        elif arguments.command == "upload":
            upload(arguments.host, arguments.key_file, arguments.firmware)
            print("OTA confirmed: matching application ELF, state valid.")
        else:
            body = read_private_json(arguments.input) if arguments.input else b""
            if arguments.input and arguments.output and arguments.input.resolve() == arguments.output.resolve():
                raise OtaError("API input and output files must be different.")
            if arguments.output and arguments.output.resolve() == arguments.key_file.resolve():
                raise OtaError("API output must not overwrite the OTA key file.")
            result = api(arguments.host, arguments.key_file, arguments.method, arguments.path, body=body)
            if arguments.output:
                private_write(arguments.output, (json.dumps(result, indent=2) + "\n").encode("utf-8"))
            print("API request succeeded; response written privately." if arguments.output else "API request succeeded; response not displayed.")
        return 0
    except (OtaError, OSError, ValueError) as error:
        message = str(error) if isinstance(error, OtaError) else "Local OTA operation failed."
        print(f"OTA error: {message}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())