"""FastAPI app for the Biscuit local sync host."""

from __future__ import annotations

import hmac
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response, StreamingResponse

from biscuit_sync_common import (
    CHALLENGE_TTL_SECONDS,
    PACK_FILES,
    SCHEMA_HOST,
    SyncHostError,
    SyncState,
    bootstrap_signature,
    collect_outgoing_study_pack_artifacts,
    create_or_get_bootstrap_device,
    default_repo_root,
    default_root,
    ensure_dirs,
    import_payload,
    load_bootstrap,
    load_host,
    load_paired,
    obsolete_installed_study_pack_ids,
    sync_signature,
    utc_now,
)


def error_response(code: int, message: str) -> JSONResponse:
    return JSONResponse(status_code=code, content={"ok": False, "error": message})


def debug_enabled() -> bool:
    return os.environ.get("BISCUIT_SYNC_DEBUG", "").lower() in {"1", "true", "yes", "on"}


def debug_log(message: str, **fields: Any) -> None:
    if not debug_enabled():
        return
    safe_fields = {
        key: value
        for key, value in fields.items()
        if key not in {"secret", "signature"}
    }
    suffix = ""
    if safe_fields:
        suffix = " " + json.dumps(safe_fields, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    print(f"[debug] {message}{suffix}", file=sys.stderr, flush=True)


def root_from_env() -> Path:
    configured = os.environ.get("BISCUIT_SYNC_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    return default_root()


def body_as_object(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SyncHostError("expected JSON object body")
    return value


async def read_body(request: Request) -> dict[str, Any]:
    try:
        return body_as_object(await request.json())
    except json.JSONDecodeError as exc:
        raise SyncHostError(f"invalid JSON body: {exc}") from exc


def create_app() -> FastAPI:
    root = root_from_env()
    ensure_dirs(root)
    host = load_host(root)
    state = SyncState()
    app = FastAPI(title="Biscuit Sync Host", version="0.1.0")

    @app.exception_handler(SyncHostError)
    async def sync_host_error_handler(request: Request, exc: SyncHostError) -> JSONResponse:
        del request
        return error_response(400, str(exc))

    @app.exception_handler(Exception)
    async def generic_error_handler(request: Request, exc: Exception) -> JSONResponse:
        debug_log("internal error", path=str(request.url.path), error=repr(exc))
        return error_response(500, f"internal error: {exc}")

    @app.get("/health")
    async def health() -> dict[str, Any]:
        return {"ok": True, "schema": SCHEMA_HOST, "host_id": host["host_id"], "time_utc": utc_now()}

    @app.get("/api/v0/host")
    async def host_info() -> dict[str, Any]:
        return {
            "ok": True,
            "schema": SCHEMA_HOST,
            "host_id": host["host_id"],
            "host_name": host["host_name"],
            "capabilities": [
                "challenge",
                "bootstrap",
                "sync-push",
                "sync-summary-v0",
                "notes-v0",
                "study-logs-v0",
                "study-pack-delivery-v0",
                "study-pack-artifacts-v0",
                "study-pack-artifacts-range-v1",
                "status-v0",
            ],
        }

    @app.post("/api/v0/bootstrap/challenge")
    async def bootstrap_challenge(request: Request) -> JSONResponse:
        body = await read_body(request)
        device_id = str(body.get("device_id", ""))
        bootstrap = load_bootstrap(root)
        bootstrap_secret = str(bootstrap.get("secret", ""))
        if not bootstrap.get("enabled") or not bootstrap_secret:
            debug_log("bootstrap challenge rejected", reason="disabled")
            return error_response(403, "bootstrap disabled")
        if not device_id:
            return error_response(400, "missing device_id")
        nonce = state.issue_challenge(device_id, time.time())
        response = {
            "ok": True,
            "host_id": host["host_id"],
            "device_id": device_id,
            "nonce": nonce,
            "expires_ms": CHALLENGE_TTL_SECONDS * 1000,
            "server_time_utc": utc_now(),
        }
        debug_log("bootstrap challenge response", device_id=device_id, response=response)
        return JSONResponse(content=response)

    @app.post("/api/v0/bootstrap")
    async def bootstrap_pair(request: Request) -> JSONResponse:
        body = await read_body(request)
        auth = body.get("auth", {})
        bootstrap = load_bootstrap(root)
        bootstrap_secret = str(bootstrap.get("secret", ""))
        if not bootstrap.get("enabled") or not bootstrap_secret:
            debug_log("bootstrap rejected", reason="disabled")
            return error_response(403, "bootstrap disabled")
        if not isinstance(auth, dict):
            return error_response(400, "missing auth")
        device_id = str(auth.get("device_id", ""))
        nonce = str(auth.get("nonce", ""))
        signature = str(auth.get("signature", ""))
        if not device_id:
            return error_response(400, "missing device_id")
        if not state.consume_challenge(device_id, nonce, time.time()):
            debug_log("bootstrap rejected", device_id=device_id, reason="invalid or expired challenge")
            return error_response(401, "invalid or expired challenge")
        expected = bootstrap_signature(bootstrap_secret, device_id, nonce)
        if not hmac.compare_digest(expected, signature):
            debug_log("bootstrap rejected", device_id=device_id, reason="bad signature")
            return error_response(401, "bad signature")

        secret, created = create_or_get_bootstrap_device(root, host, device_id)
        response = {
            "ok": True,
            "schema": "biscuit.device-sync-host.v0",
            "host_id": host["host_id"],
            "device_id": device_id,
            "secret": secret,
            "created": created,
            "server_time_utc": utc_now(),
        }
        debug_log("bootstrap response", device_id=device_id, created=created)
        return JSONResponse(content=response)

    @app.post("/api/v0/challenge")
    async def challenge(request: Request) -> JSONResponse:
        body = await read_body(request)
        device_id = str(body.get("device_id", ""))
        debug_log(
            "challenge request",
            client=request.client.host if request.client else "",
            content_length=request.headers.get("content-length", "0"),
            device_id=device_id,
        )
        paired = load_paired(root)
        devices = paired.get("devices", {})
        if not device_id or not isinstance(devices, dict) or device_id not in devices:
            debug_log("challenge rejected", device_id=device_id, reason="unknown device")
            return error_response(404, "unknown device")
        nonce = state.issue_challenge(device_id, time.time())
        response = {
            "ok": True,
            "host_id": host["host_id"],
            "device_id": device_id,
            "nonce": nonce,
            "expires_ms": CHALLENGE_TTL_SECONDS * 1000,
            "server_time_utc": utc_now(),
        }
        debug_log("challenge response", device_id=device_id, response=response)
        return JSONResponse(content=response)

    @app.post("/api/v0/sync")
    async def sync(request: Request) -> JSONResponse:
        body = await read_body(request)
        auth = body.get("auth", {})
        payload = body.get("payload", {})
        debug_log(
            "sync request",
            client=request.client.host if request.client else "",
            content_length=request.headers.get("content-length", "0"),
            has_auth=isinstance(auth, dict),
            payload_keys=sorted(payload.keys()) if isinstance(payload, dict) else [],
        )
        if not isinstance(auth, dict) or not isinstance(payload, dict):
            return error_response(400, "missing auth or payload")
        device_id = str(auth.get("device_id", ""))
        nonce = str(auth.get("nonce", ""))
        signature = str(auth.get("signature", ""))
        paired = load_paired(root)
        devices = paired.get("devices", {})
        device = devices.get(device_id) if isinstance(devices, dict) else None
        if not isinstance(device, dict):
            debug_log("sync rejected", device_id=device_id, reason="unknown device")
            return error_response(404, "unknown device")
        if not state.consume_challenge(device_id, nonce, time.time()):
            debug_log("sync rejected", device_id=device_id, reason="invalid or expired challenge")
            return error_response(401, "invalid or expired challenge")
        expected = sync_signature(str(device.get("secret", "")), device_id, nonce, payload)
        if not hmac.compare_digest(expected, signature):
            debug_log("sync rejected", device_id=device_id, reason="bad signature")
            return error_response(401, "bad signature")
        acked = import_payload(root, device_id, payload)
        artifact_now = time.time()

        def issue_artifact_url(pack_id: str, file_name: str) -> str:
            token = state.issue_artifact_token(device_id, pack_id, file_name, artifact_now)
            return f"/api/v0/artifacts/{token}"

        study_pack_artifacts, remaining_study_packs = collect_outgoing_study_pack_artifacts(
            payload,
            issue_artifact_url,
        )
        obsolete_study_pack_ids = obsolete_installed_study_pack_ids(payload)
        received = {
            "study_packs": [],
            "study_pack_count": 0,
            "study_pack_artifacts": study_pack_artifacts,
            "study_pack_artifact_count": len(study_pack_artifacts),
            "remaining_study_pack_count": remaining_study_packs,
            "remove_study_pack_ids": obsolete_study_pack_ids,
            "remove_study_pack_count": len(obsolete_study_pack_ids),
            "has_more": remaining_study_packs > 0,
            "commands": [],
        }
        accepted = {
            "status": 1 if acked.get("status") else 0,
            "notes": len(acked.get("notes", [])) if isinstance(acked.get("notes"), list) else 0,
            "study_logs": len(acked.get("study_logs", [])) if isinstance(acked.get("study_logs"), list) else 0,
            "acked_study_log_seq": acked.get("acked_study_log_seq", 0),
        }
        response = {
            "ok": True,
            "host_id": host["host_id"],
            "device_id": device_id,
            "acked": acked,
            "received": received,
            "summary": {
                "accepted": accepted,
                "received": received,
                "messages": [],
            },
            "server_time_utc": utc_now(),
        }
        debug_log(
            "sync response",
            device_id=device_id,
            accepted=accepted,
            study_pack_artifact_count=len(study_pack_artifacts),
            remaining_study_pack_count=remaining_study_packs,
            study_pack_ids=[pack.get("pack_id") for pack in study_pack_artifacts],
            remove_study_pack_ids=obsolete_study_pack_ids,
        )
        return JSONResponse(content=response)

    def parse_range_header(range_header: str, file_size: int) -> tuple[int, int] | None:
        if not range_header.startswith("bytes="):
            return None
        spec = range_header[len("bytes="):].strip()
        if "," in spec or "-" not in spec:
            return None
        start_text, end_text = spec.split("-", 1)
        try:
            if start_text == "":
                suffix_len = int(end_text)
                if suffix_len <= 0:
                    return None
                start = max(0, file_size - suffix_len)
                end = file_size - 1
            else:
                start = int(start_text)
                end = file_size - 1 if end_text == "" else int(end_text)
        except ValueError:
            return None
        if start < 0 or end < start or start >= file_size:
            return None
        return start, min(end, file_size - 1)

    def iter_file_range(path: Path, start: int, end: int, chunk_size: int = 128 * 1024):
        with path.open("rb") as handle:
            handle.seek(start)
            remaining = end - start + 1
            while remaining > 0:
                chunk = handle.read(min(chunk_size, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                yield chunk

    @app.get("/api/v0/artifacts/{token}")
    async def artifact(token: str, request: Request) -> Response:
        resolved = state.resolve_artifact_token(token, time.time())
        if resolved is None:
            return error_response(404, "unknown or expired artifact")
        device_id, pack_id, file_name = resolved
        if file_name not in PACK_FILES:
            debug_log("artifact rejected", device_id=device_id, pack_id=pack_id, file_name=file_name, reason="bad name")
            return error_response(404, "unknown artifact")
        path = default_repo_root() / "agents" / "study-twin" / "examples" / "packs" / pack_id / file_name
        try:
            stat = path.stat()
        except OSError:
            debug_log("artifact missing", device_id=device_id, pack_id=pack_id, file_name=file_name)
            return error_response(404, "missing artifact")
        file_size = stat.st_size
        start = 0
        end = max(0, file_size - 1)
        status_code = 200
        range_header = request.headers.get("range", "")
        if range_header:
            parsed = parse_range_header(range_header, file_size)
            if parsed is None:
                return Response(
                    status_code=416,
                    headers={
                        "Accept-Ranges": "bytes",
                        "Content-Range": f"bytes */{file_size}",
                        "Cache-Control": "no-store",
                    },
                )
            start, end = parsed
            status_code = 206

        content_length = end - start + 1 if file_size > 0 else 0
        headers = {
            "Accept-Ranges": "bytes",
            "Cache-Control": "no-store",
            "Content-Length": str(content_length),
            "X-Biscuit-Pack-Id": pack_id,
        }
        if status_code == 206:
            headers["Content-Range"] = f"bytes {start}-{end}/{file_size}"
        debug_log(
            "artifact response",
            device_id=device_id,
            pack_id=pack_id,
            file_name=file_name,
            range=f"{start}-{end}",
            bytes=content_length,
        )
        return StreamingResponse(
            iter_file_range(path, start, end),
            status_code=status_code,
            media_type="application/octet-stream",
            headers=headers,
        )

    return app
