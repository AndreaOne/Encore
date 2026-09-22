from __future__ import annotations

import cgi
import json
import mimetypes
from http.server import BaseHTTPRequestHandler
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from score_bundle import analyze_musicxml
from score_bundle import export_bundle
from score_bundle import slugify
from score_bundle import validate_bundle


ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = ROOT / "web"
GENERATED_DIR = ROOT / "generated"
ENCORE_DIR = GENERATED_DIR / "encore"
ENCORE_DATA_DIR = ENCORE_DIR / "data"
ENCORE_SCORE_BUNDLE_PATH = ENCORE_DATA_DIR / "score_bundle.json"
ENCORE_BUILTIN_BUNDLE_HEADER_PATH = ENCORE_DIR / "BuiltInScoreBundle.h"


def _compact_bundle_json(bundle: dict[str, object]) -> str:
    validated_bundle = validate_bundle(bundle)
    return json.dumps(validated_bundle, separators=(",", ":"), ensure_ascii=True)


def _write_encore_builtin_bundle_header(bundle: dict[str, object]) -> Path:
    compact_json = _compact_bundle_json(bundle)
    header_text = (
        "#pragma once\n\n"
        "static const char kBuiltInScoreBundleJson[] = R\"json(\n"
        f"{compact_json}\n"
        ")json\";\n"
    )
    ENCORE_BUILTIN_BUNDLE_HEADER_PATH.write_text(header_text, encoding="utf-8")
    return ENCORE_BUILTIN_BUNDLE_HEADER_PATH


def _sync_encore_runtime_bundle(bundle: dict[str, object]) -> tuple[Path, Path]:
    score_path = export_bundle(bundle, ENCORE_SCORE_BUNDLE_PATH)
    header_path = _write_encore_builtin_bundle_header(bundle)
    return score_path, header_path


class ScoreToolHandler(BaseHTTPRequestHandler):
    server_version = "EncoreScoreTool/1.0"

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/":
            self._serve_file(WEB_DIR / "index.html")
            return

        if path.startswith("/generated/"):
            relative = path.removeprefix("/generated/")
            self._serve_safe(relative, GENERATED_DIR)
            return

        self._serve_safe(path.lstrip("/"), WEB_DIR)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)

        try:
            if parsed.path == "/api/parse":
                self._handle_parse()
                return
            if parsed.path == "/api/export":
                self._handle_export()
                return
            self._send_json(404, {"error": "Not found"})
        except ValueError as exc:
            self._send_json(400, {"error": str(exc)})
        except Exception as exc:  # pragma: no cover - defensive runtime path
            self._send_json(500, {"error": str(exc)})

    def log_message(self, format: str, *args: object) -> None:
        return

    def _handle_parse(self) -> None:
        content_type = self.headers.get("Content-Type", "")
        if "multipart/form-data" not in content_type:
            raise ValueError("Expected multipart/form-data with a file upload.")

        form = cgi.FieldStorage(
            fp=self.rfile,
            headers=self.headers,
            environ={
                "REQUEST_METHOD": "POST",
                "CONTENT_TYPE": content_type,
            },
        )
        upload = form["file"] if "file" in form else None
        if upload is None or getattr(upload, "file", None) is None:
            raise ValueError("No uploaded file was found in the request.")

        raw_bytes = upload.file.read()
        if not raw_bytes:
            raise ValueError("The uploaded file was empty.")

        self._send_json(200, analyze_musicxml(raw_bytes))

    def _handle_export(self) -> None:
        content_length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(content_length)
        request_body = json.loads(payload.decode("utf-8"))
        bundle_payload = request_body.get("bundle", request_body) if isinstance(request_body, dict) else {}
        bundle = validate_bundle(bundle_payload)

        title = bundle["title"]
        _, part_name = bundle["part"]
        filename = f"{slugify(title)}-{slugify(part_name)}.score.json"
        output_path = export_bundle(bundle, GENERATED_DIR / filename)
        synced_score_path, synced_header_path = _sync_encore_runtime_bundle(bundle)

        self._send_json(
            200,
            {
                "file": str(output_path),
                "url": f"/generated/{output_path.name}",
                "encoreScoreFile": str(synced_score_path),
                "encoreBuiltInHeader": str(synced_header_path),
            },
        )

    def _serve_safe(self, relative_path: str, base_dir: Path) -> None:
        target = (base_dir / relative_path).resolve()
        base = base_dir.resolve()
        if not str(target).startswith(str(base)) or not target.is_file():
            self._send_json(404, {"error": "File not found"})
            return
        self._serve_file(target)

    def _serve_file(self, file_path: Path) -> None:
        mime_type = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", mime_type)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(file_path.read_bytes())

    def _send_json(self, status: int, payload: dict[str, object]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


def main() -> None:
    host = "127.0.0.1"
    port = 8000
    server = ThreadingHTTPServer((host, port), ScoreToolHandler)
    print(f"Encore score tool listening at http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
