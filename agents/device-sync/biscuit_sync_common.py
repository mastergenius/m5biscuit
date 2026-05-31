"""Shared storage, crypto, and import helpers for the Biscuit sync host."""

from __future__ import annotations

import hashlib
import hmac
import json
import platform
import secrets
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


SCHEMA_HOST = "biscuit.sync-host.v0"
SCHEMA_PAIRED = "biscuit.paired-devices.v0"
SCHEMA_BOOTSTRAP = "biscuit.bootstrap-secret.v0"
DEFAULT_PORT = 8787
CHALLENGE_TTL_SECONDS = 30
ARTIFACT_TTL_SECONDS = 30 * 60
PACK_FILES = ("manifest.json", "concepts.jsonl", "episodes.jsonl", "rubrics.jsonl")
DEFAULT_SYNC_PACK_IDS = (
    "mim-systems-method-deep",
    "mim-selfdev-learner-culture",
    "mim-selfdev-bolid-machine",
    "mim-selfdev-worldview-grounding",
    "mim-selfdev-roles-gates",
    "mim-selfdev-values-trajectory",
    "mim-workdev-modeling-objects",
    "mim-workdev-models-communication",
    "mim-workdev-explanations",
    "mim-workdev-object-dynamics",
    "mim-workdev-systems-thinking",
    "fr-sentence-building-a1",
    "fr-a1-vocab-grounding-probe",
    "fr-a1-pronunciation-grounding",
)
MAX_SYNC_PACK_RESPONSE_BYTES = 96 * 1024
MAX_SYNC_PACKS_PER_RESPONSE = 1


class SyncHostError(Exception):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def default_root() -> Path:
    return Path(__file__).resolve().parent


def default_repo_root() -> Path:
    return default_root().parents[1]


def read_json(path: Path, default: dict[str, Any] | None = None) -> dict[str, Any]:
    if not path.exists():
        if default is not None:
            return default
        raise SyncHostError(f"missing file: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SyncHostError(f"{path}: read failed: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SyncHostError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise SyncHostError(f"{path}: expected JSON object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    write_text_atomic(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def write_text_atomic(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.{secrets.token_hex(4)}.tmp")
    tmp.write_text(value, encoding="utf-8")
    tmp.replace(path)


def _parse_review_event_line(line: str) -> tuple[int, str] | None:
    stripped = line.strip()
    if not stripped:
        return None
    try:
        value = json.loads(stripped)
    except json.JSONDecodeError:
        return None
    if not isinstance(value, dict) or value.get("schema") != "biscuit.review-event.v0":
        return None
    seq = value.get("seq")
    if not isinstance(seq, int):
        return None
    normalized = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return seq, normalized


def _review_event_records(content: str) -> tuple[dict[int, str], int]:
    records: dict[int, str] = {}
    invalid_lines = 0
    for line in content.splitlines():
        if not line.strip():
            continue
        parsed = _parse_review_event_line(line)
        if parsed is None:
            invalid_lines += 1
            continue
        seq, normalized = parsed
        records[seq] = normalized
    return records, invalid_lines


def merge_study_log_content_checked(existing: str, incoming: str) -> tuple[str, list[int]]:
    existing_records, _ = _review_event_records(existing)
    incoming_records, _ = _review_event_records(incoming)
    conflicts = [
        seq
        for seq, normalized in incoming_records.items()
        if seq in existing_records and existing_records[seq] != normalized
    ]
    if conflicts:
        return existing, sorted(conflicts)

    records = existing_records | incoming_records
    if not records:
        return incoming, []

    return "".join(f"{records[seq]}\n" for seq in sorted(records)), []


def merge_study_log_content(existing: str, incoming: str) -> str:
    merged, _ = merge_study_log_content_checked(existing, incoming)
    return merged


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def ensure_dirs(root: Path) -> None:
    for rel in (
        "config",
        "state",
        "inbox/notes",
        "inbox/study-logs",
        "inbox/study-log-conflicts",
        "inbox/device-status",
    ):
        (root / rel).mkdir(parents=True, exist_ok=True)


def host_config_path(root: Path) -> Path:
    return root / "config" / "host.json"


def paired_devices_path(root: Path) -> Path:
    return root / "config" / "paired-devices.json"


def bootstrap_config_path(root: Path) -> Path:
    return root / "config" / "bootstrap.json"


def load_host(root: Path) -> dict[str, Any]:
    return read_json(host_config_path(root))


def load_paired(root: Path) -> dict[str, Any]:
    return read_json(paired_devices_path(root), {"schema": SCHEMA_PAIRED, "devices": {}})


def load_bootstrap(root: Path) -> dict[str, Any]:
    return read_json(bootstrap_config_path(root), {"schema": SCHEMA_BOOTSTRAP, "enabled": False})


def save_paired(root: Path, paired: dict[str, Any]) -> None:
    if paired.get("schema") != SCHEMA_PAIRED:
        paired["schema"] = SCHEMA_PAIRED
    paired.setdefault("devices", {})
    write_json(paired_devices_path(root), paired)


def sanitize_id(value: str) -> str:
    out = []
    for ch in value:
        if ch.isalnum() or ch in ("-", "_", "."):
            out.append(ch)
        else:
            out.append("_")
    text = "".join(out).strip("._")
    return text[:96] or "unknown"


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def validate_hex_secret(secret: str, *, label: str = "secret") -> str:
    if len(secret) != 64:
        raise SyncHostError(f"{label} must be 32-byte hex")
    try:
        bytes.fromhex(secret)
    except ValueError as exc:
        raise SyncHostError(f"{label} must be hex") from exc
    return secret


def sync_signature(secret_hex: str, device_id: str, nonce: str, payload: dict[str, Any]) -> str:
    secret = bytes.fromhex(secret_hex)
    message = canonical_json({"device_id": device_id, "nonce": nonce, "payload": payload})
    return hmac.new(secret, message, hashlib.sha256).hexdigest()


def bootstrap_signature(secret_hex: str, device_id: str, nonce: str) -> str:
    secret = bytes.fromhex(secret_hex)
    message = canonical_json({"device_id": device_id, "nonce": nonce, "purpose": "device-bootstrap-v0"})
    return hmac.new(secret, message, hashlib.sha256).hexdigest()


def import_payload(root: Path, device_id: str, payload: dict[str, Any]) -> dict[str, Any]:
    safe_device = sanitize_id(device_id)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    acked: dict[str, Any] = {"notes": [], "study_logs": [], "status": False, "acked_study_log_seq": 0}

    status = payload.get("status")
    if isinstance(status, dict):
        status_path = root / "inbox" / "device-status" / f"{safe_device}_{stamp}.json"
        write_json(status_path, {"device_id": device_id, "imported_utc": utc_now(), "status": status})
        acked["status"] = True

    notes = payload.get("notes", [])
    if isinstance(notes, list):
        note_dir = root / "inbox" / "notes" / safe_device
        note_dir.mkdir(parents=True, exist_ok=True)
        for note in notes:
            if not isinstance(note, dict):
                continue
            note_id = sanitize_id(str(note.get("note_id", "")))
            markdown = note.get("markdown", "")
            if not note_id or not isinstance(markdown, str):
                continue
            write_text_atomic(note_dir / f"{note_id}.md", markdown)
            meta = note.get("meta", {})
            if isinstance(meta, dict):
                write_json(note_dir / f"{note_id}.json", {"device_id": device_id, "imported_utc": utc_now(), "meta": meta})
            acked["notes"].append(note_id)

    study_logs = payload.get("study_logs", [])
    if isinstance(study_logs, list):
        log_dir = root / "inbox" / "study-logs" / safe_device
        log_dir.mkdir(parents=True, exist_ok=True)
        for segment in study_logs:
            if not isinstance(segment, dict):
                continue
            name = sanitize_id(str(segment.get("segment", "")))
            content = segment.get("content", "")
            if not name or not isinstance(content, str):
                continue
            if not name.endswith(".jsonl"):
                name += ".jsonl"
            incoming_records, incoming_invalid_lines = _review_event_records(content)
            if not incoming_records or incoming_invalid_lines:
                continue
            incoming_last_seq = max(incoming_records)
            claimed_last_seq = segment.get("last_seq")
            if claimed_last_seq is not None and (
                type(claimed_last_seq) is not int or claimed_last_seq != incoming_last_seq
            ):
                continue
            claimed_sha256 = segment.get("sha256")
            if isinstance(claimed_sha256, str) and claimed_sha256 and claimed_sha256 != sha256_text(content):
                continue
            log_path = log_dir / name
            existing = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            merged, conflicts = merge_study_log_content_checked(existing, content)
            if conflicts:
                conflict_dir = root / "inbox" / "study-log-conflicts" / safe_device
                conflict_name = f"{stamp}_{name}"
                write_text_atomic(conflict_dir / conflict_name, content)
                continue
            if merged != existing:
                write_text_atomic(log_path, merged)
            acked["study_logs"].append(name)
            acked["acked_study_log_seq"] = max(int(acked["acked_study_log_seq"]), incoming_last_seq)

    return acked


def _installed_study_pack_revisions(payload: dict[str, Any]) -> dict[str, str]:
    installed: dict[str, str] = {}
    raw = payload.get("installed_study_packs", [])
    if not isinstance(raw, list):
        return installed
    for item in raw:
        if not isinstance(item, dict):
            continue
        pack_id = item.get("id") or item.get("pack_id")
        revision = item.get("revision")
        if isinstance(pack_id, str) and isinstance(revision, str) and pack_id:
            installed[pack_id] = revision
    return installed


def obsolete_installed_study_pack_ids(
    payload: dict[str, Any],
    *,
    pack_ids: tuple[str, ...] = DEFAULT_SYNC_PACK_IDS,
) -> list[str]:
    """Return installed StudyPacks that are not part of the curated sync shelf."""

    keep = set(pack_ids)
    obsolete: list[str] = []
    for pack_id in _installed_study_pack_revisions(payload):
        if pack_id not in keep:
            obsolete.append(pack_id)
    return sorted(obsolete)


def _read_outgoing_pack(pack_root: Path, pack_id: str) -> dict[str, Any] | None:
    pack_dir = pack_root / pack_id
    if not pack_dir.is_dir():
        return None
    files: dict[str, str] = {}
    try:
        manifest = json.loads((pack_dir / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema") != "biscuit.study-pack.v0":
            return None
        if manifest.get("pack_id") != pack_id:
            return None
        for name in PACK_FILES:
            files[name] = (pack_dir / name).read_text(encoding="utf-8")
    except (OSError, json.JSONDecodeError):
        return None
    return {
        "pack_id": pack_id,
        "revision": str(manifest.get("revision") or ""),
        "title": str(manifest.get("title") or pack_id),
        "files": files,
    }


def _sha256_file(path: Path, chunk_size: int = 128 * 1024) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(chunk_size)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _read_outgoing_pack_meta(pack_root: Path, pack_id: str) -> dict[str, Any] | None:
    pack_dir = pack_root / pack_id
    if not pack_dir.is_dir():
        return None
    try:
        manifest = json.loads((pack_dir / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema") != "biscuit.study-pack.v0":
            return None
        if manifest.get("pack_id") != pack_id:
            return None
        files = {}
        for name in PACK_FILES:
            path = pack_dir / name
            stat = path.stat()
            files[name] = {
                "size": stat.st_size,
                "sha256": _sha256_file(path),
            }
    except (OSError, json.JSONDecodeError):
        return None
    return {
        "pack_id": pack_id,
        "revision": str(manifest.get("revision") or ""),
        "title": str(manifest.get("title") or pack_id),
        "files": files,
    }


def collect_outgoing_study_packs(
    payload: dict[str, Any],
    *,
    repo_root: Path | None = None,
    max_bytes: int = MAX_SYNC_PACK_RESPONSE_BYTES,
    max_packs: int = MAX_SYNC_PACKS_PER_RESPONSE,
    pack_ids: tuple[str, ...] = DEFAULT_SYNC_PACK_IDS,
) -> list[dict[str, Any]]:
    """Return curated StudyPacks missing from, or stale on, the device.

    The host deliberately does not expose arbitrary filesystem access here. This is a bounded
    delivery lane for generated, checked-in StudyPack directories.
    """

    root = repo_root or default_repo_root()
    pack_root = root / "agents" / "study-twin" / "examples" / "packs"
    installed = _installed_study_pack_revisions(payload)
    outgoing: list[dict[str, Any]] = []
    used_bytes = 0

    for pack_id in pack_ids:
        if len(outgoing) >= max_packs:
            break
        pack = _read_outgoing_pack(pack_root, pack_id)
        if pack is None:
            continue
        revision = str(pack.get("revision") or "")
        if installed.get(pack_id) == revision:
            continue
        files = pack.get("files", {})
        if not isinstance(files, dict):
            continue
        pack_bytes = sum(len(content.encode("utf-8")) for content in files.values() if isinstance(content, str))
        if used_bytes + pack_bytes > max_bytes:
            break
        outgoing.append(pack)
        used_bytes += pack_bytes

    return outgoing


def collect_outgoing_study_pack_artifacts(
    payload: dict[str, Any],
    issue_artifact_url: Callable[[str, str], str],
    *,
    repo_root: Path | None = None,
    max_packs: int = MAX_SYNC_PACKS_PER_RESPONSE,
    pack_ids: tuple[str, ...] = DEFAULT_SYNC_PACK_IDS,
) -> tuple[list[dict[str, Any]], int]:
    """Return a bounded sync plan for missing/stale StudyPacks.

    The sync response carries only descriptors. File bytes are fetched through
    short-lived artifact URLs, so the device does not need to parse a large JSON
    document just to install a pack.
    """

    root = repo_root or default_repo_root()
    pack_root = root / "agents" / "study-twin" / "examples" / "packs"
    installed = _installed_study_pack_revisions(payload)
    candidates: list[dict[str, Any]] = []

    for pack_id in pack_ids:
        pack = _read_outgoing_pack_meta(pack_root, pack_id)
        if pack is None:
            continue
        revision = str(pack.get("revision") or "")
        if installed.get(pack_id) == revision:
            continue
        candidates.append(pack)

    descriptors: list[dict[str, Any]] = []
    for pack in candidates[:max_packs]:
        pack_id = str(pack["pack_id"])
        files = pack.get("files", {})
        if not isinstance(files, dict):
            continue
        file_descriptors: list[dict[str, Any]] = []
        total_bytes = 0
        valid = True
        for name in PACK_FILES:
            file_info = files.get(name)
            if not isinstance(file_info, dict):
                valid = False
                break
            size = file_info.get("size")
            sha256 = file_info.get("sha256")
            if not isinstance(size, int) or size <= 0 or not isinstance(sha256, str) or len(sha256) != 64:
                valid = False
                break
            total_bytes += size
            file_descriptors.append({
                "name": name,
                "url": issue_artifact_url(pack_id, name),
                "size": size,
                "sha256": sha256,
                "range": True,
            })
        if not valid:
            continue
        descriptors.append({
            "pack_id": pack_id,
            "revision": str(pack.get("revision") or ""),
            "title": str(pack.get("title") or pack_id),
            "files": file_descriptors,
            "total_bytes": total_bytes,
        })

    remaining = max(0, len(candidates) - len(descriptors))
    return descriptors, remaining


class SyncState:
    def __init__(self) -> None:
        self.challenges: dict[str, tuple[str, float]] = {}
        self.artifacts: dict[str, tuple[str, str, str, float]] = {}

    def issue_challenge(self, device_id: str, now: float) -> str:
        nonce = secrets.token_hex(16)
        self.challenges[device_id] = (nonce, now + CHALLENGE_TTL_SECONDS)
        return nonce

    def consume_challenge(self, device_id: str, nonce: str, now: float) -> bool:
        entry = self.challenges.get(device_id)
        if not entry:
            return False
        expected_nonce, expires_at = entry
        if now > expires_at:
            self.challenges.pop(device_id, None)
            return False
        if not hmac.compare_digest(expected_nonce, nonce):
            return False
        self.challenges.pop(device_id, None)
        return True

    def issue_artifact_token(self, device_id: str, pack_id: str, file_name: str, now: float) -> str:
        token = secrets.token_urlsafe(24)
        self.artifacts[token] = (device_id, pack_id, file_name, now + ARTIFACT_TTL_SECONDS)
        return token

    def resolve_artifact_token(self, token: str, now: float) -> tuple[str, str, str] | None:
        expired = [key for key, (_, _, _, expires_at) in self.artifacts.items() if now > expires_at]
        for key in expired:
            self.artifacts.pop(key, None)
        entry = self.artifacts.get(token)
        if not entry:
            return None
        device_id, pack_id, file_name, expires_at = entry
        if now > expires_at:
            self.artifacts.pop(token, None)
            return None
        return device_id, pack_id, file_name


def create_host(root: Path, *, host_name: str | None, port: int, force: bool) -> dict[str, Any]:
    ensure_dirs(root)
    path = host_config_path(root)
    if path.exists() and not force:
        raise SyncHostError(f"{path} already exists; pass --force to replace")
    host = {
        "schema": SCHEMA_HOST,
        "host_id": f"host-{secrets.token_hex(8)}",
        "host_name": host_name or platform.node() or "Biscuit Host",
        "created_utc": utc_now(),
        "default_port": port,
    }
    write_json(path, host)
    if not paired_devices_path(root).exists():
        save_paired(root, {"schema": SCHEMA_PAIRED, "devices": {}})
    return host


def add_device(root: Path, device_id: str, *, name: str | None, secret: str | None) -> str:
    ensure_dirs(root)
    host = load_host(root)
    paired = load_paired(root)
    devices = paired.setdefault("devices", {})
    device_secret = validate_hex_secret(secret or secrets.token_hex(32))
    devices[device_id] = {
        "display_name": name or device_id,
        "secret": device_secret,
        "created_utc": utc_now(),
        "host_id": host["host_id"],
        "dev_manual_pairing": True,
    }
    save_paired(root, paired)
    return device_secret


def set_bootstrap_secret(root: Path, *, secret: str | None) -> str:
    ensure_dirs(root)
    load_host(root)
    bootstrap_secret = validate_hex_secret(secret or secrets.token_hex(32), label="bootstrap secret")
    write_json(bootstrap_config_path(root), {
        "schema": SCHEMA_BOOTSTRAP,
        "enabled": True,
        "secret": bootstrap_secret,
        "updated_utc": utc_now(),
    })
    return bootstrap_secret


def create_or_get_bootstrap_device(root: Path, host: dict[str, Any], device_id: str) -> tuple[str, bool]:
    paired = load_paired(root)
    devices = paired.setdefault("devices", {})
    device = devices.get(device_id)
    created = False
    if not isinstance(device, dict):
        device = {
            "display_name": device_id,
            "secret": secrets.token_hex(32),
            "created_utc": utc_now(),
            "host_id": host["host_id"],
            "bootstrap_pairing": True,
        }
        devices[device_id] = device
        save_paired(root, paired)
        created = True
    return str(device.get("secret", "")), created


def maybe_start_mdns(host: dict[str, Any], port: int, advertise: bool) -> subprocess.Popen[str] | None:
    if not advertise:
        return None
    dns_sd = shutil.which("dns-sd")
    if not dns_sd:
        print("warning: --advertise requested but dns-sd was not found")
        return None
    instance = sanitize_id(str(host["host_name"])) or "BiscuitSync"
    args = [
        dns_sd,
        "-R",
        instance,
        "_biscuit-sync._tcp",
        "local",
        str(port),
        f"host_id={host['host_id']}",
        f"schema={SCHEMA_HOST}",
    ]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)


def extract_define(path: Path, name: str) -> str | None:
    if not path.exists():
        return None
    prefix = f"#define {name} "
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            raw = line[len(prefix):].strip()
            if raw.startswith('"') and raw.endswith('"'):
                return raw[1:-1]
    return None


def write_firmware_bootstrap_fallback(
    root: Path,
    *,
    repo_root: Path,
    host_url: str | None,
    device_id: str | None,
) -> Path:
    bootstrap = load_bootstrap(root)
    secret = str(bootstrap.get("secret", ""))
    if not bootstrap.get("enabled") or not secret:
        raise SyncHostError("bootstrap secret is not enabled; run set-bootstrap-secret first")
    validate_hex_secret(secret, label="bootstrap secret")

    path = repo_root / "src/activities/apps/DeviceSyncDebugTemporaryPairingFallback.local.h"
    current_host = extract_define(path, "BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_HOST_URL")
    current_device = extract_define(path, "BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_DEVICE_ID")
    resolved_host = host_url or current_host
    resolved_device = device_id or current_device
    if not resolved_host:
        raise SyncHostError("host URL is missing; pass --host-url")
    if not resolved_device:
        raise SyncHostError("device id is missing; pass --device-id")

    content = f"""#pragma once

// DEBUG-ONLY TEMPORARY FALLBACK.
//
// This file is intentionally ignored by git. It exists only until proper QR pairing and secret
// rotation are implemented. Do not copy it into docs, commits, screenshots, or shared firmware.

#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_HOST_URL "{resolved_host}"
#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_DEVICE_ID "{resolved_device}"
#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_SECRET_HEX \\
  "{secret}"
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path
