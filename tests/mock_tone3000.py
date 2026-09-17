#!/usr/bin/env python3
"""Minimal stand-in for the TONE3000 v1 API, for Tone3000ClientTest and for
running the GUI against a local server (NAM_TONE3000_API_BASE=http://127.0.0.1:PORT/api/v1).

Behaviour knobs (query on /control):
  /control?expire=1     next Bearer request answers 401 once (forces a refresh)
  /control?ratelimit=N  next N API requests answer 429 with Retry-After: 1
Every API call except /oauth/* requires "Authorization: Bearer <token>"; the
token rotates on refresh, and a stale one is rejected with 401.
"""
import json, os, struct, sys, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse, urlencode

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
MODEL_FILE = sys.argv[2] if len(sys.argv) > 2 else None  # optional real .nam to serve

state = {"token": "tok-1", "refresh": "ref-1", "expire_next": False, "ratelimit": 0,
         "favorites": set(), "requests": []}


def png(color):
    """A 64x64 solid PNG without PIL."""
    w = h = 64
    raw = b"".join(b"\x00" + bytes(color) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def base():
    return f"http://127.0.0.1:{PORT}"


def user(i, name):
    return {"id": i, "username": name, "display_name": name.title(), "is_verified": False,
            "avatar_url": f"{base()}/img/avatar{i}.png", "url": f"https://www.tone3000.com/@{name}"}


def tone(i, title, fmt="nam", gear="amp-cab", a2=2, a1=1):
    return {"id": i, "user_id": 7, "user": user(7, "ripper"), "created_at": "2026-01-01T00:00:00Z",
            "updated_at": "2026-01-02T00:00:00Z", "published_at": "2026-01-02T00:00:00Z",
            "title": title, "description": f"Description of {title}.\nSecond line.", "gear": gear,
            "images": [f"{base()}/img/tone{i}.png"], "is_public": True, "links": [],
            "format": fmt, "license": "cc-by", "sizes": ["standard"],
            "makes": [{"id": 1, "name": "Mesa Boogie"}], "tags": [{"id": 1, "name": "metal"}],
            "models_count": a2 + a1, "a1_models_count": a1, "a2_models_count": a2,
            "custom_models_count": 0, "irs_count": 0 if fmt == "nam" else 3,
            "downloads_count": 1234, "favorites_count": 56, "is_favorite": i in state["favorites"],
            "url": f"https://www.tone3000.com/tones/{i}"}


TONES = {1: tone(1, "Dual Rec Rhythm"), 2: tone(2, "Plexi Crunch", a2=0, a1=2),
         3: tone(3, "4x12 V30 IRs", fmt="ir", gear="cab", a2=0, a1=0)}


def models(tone_id, arch):
    t = TONES[tone_id]
    out = []
    if t["format"] == "ir":
        for k in range(3):
            out.append({"id": tone_id * 100 + k, "tone_id": tone_id, "name": f"IR {k+1} SM57",
                        "size": "standard", "architecture_version": None,
                        "model_url": f"{base()}/api/v1/files/ir{k}.wav"})
        return out
    if arch == "2" or arch is None:
        for k in range(t["a2_models_count"]):
            out.append({"id": tone_id * 100 + k, "tone_id": tone_id, "name": f"{t['title']} A2 #{k+1}",
                        "size": "standard", "architecture_version": "2",
                        "model_url": f"{base()}/api/v1/files/model{k}.nam"})
    if arch is None or arch == "1":
        for k in range(t["a1_models_count"]):
            out.append({"id": tone_id * 100 + 50 + k, "tone_id": tone_id, "name": f"{t['title']} A1 #{k+1}",
                        "size": "lite", "architecture_version": "1",
                        "model_url": f"{base()}/api/v1/files/model{k}.nam"})
    return out


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send_json(self, code, obj, headers=None):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def send_bytes(self, body, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def auth_ok(self):
        h = self.headers.get("Authorization", "")
        return h == f"Bearer {state['token']}"

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        state["requests"].append(("GET", p, dict(self.headers)))

        if p == "/control":
            if "expire" in q: state["expire_next"] = True
            if "ratelimit" in q: state["ratelimit"] = int(q["ratelimit"])
            return self.send_json(200, {"ok": True, "requests": len(state["requests"])})
        if p == "/requests":
            return self.send_json(200, [{"m": m, "p": pp, "auth": h.get("Authorization", "")}
                                        for m, pp, h in state["requests"]])
        if p.startswith("/img/"):
            return self.send_bytes(png((200, 60, 40) if "tone" in p else (40, 120, 200)), "image/png")

        if p == "/api/v1/oauth/authorize":
            for k in ("client_id", "redirect_uri", "code_challenge", "state"):
                if k not in q:
                    return self.send_json(400, {"error": f"missing {k}"})
            if q.get("response_type") != "code" or q.get("code_challenge_method") != "S256":
                return self.send_json(400, {"error": "bad_request"})
            state["challenge"] = q["code_challenge"]
            state["redirect"] = q["redirect_uri"]
            params = {"code": "code-xyz", "state": q["state"]}
            if q.get("prompt") == "select_tone":
                params["tone_id"] = "1"
            self.send_response(302)
            self.send_header("Location", q["redirect_uri"] + "?" + urlencode(params))
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        if not p.startswith("/api/v1/"):
            return self.send_json(404, {"error": "not_found"})

        # Bearer-protected API.
        if state["ratelimit"] > 0:
            state["ratelimit"] -= 1
            return self.send_json(429, {"error": "rate_limited"}, {"Retry-After": "1"})
        if state["expire_next"]:
            state["expire_next"] = False
            return self.send_json(401, {"error": "token_expired"})
        if not self.auth_ok():
            return self.send_json(401, {"error": "unauthorized"})

        if p == "/api/v1/user":
            return self.send_json(200, user(1, "tester"))
        def fresh(i):
            t = dict(TONES[i]); t["is_favorite"] = i in state["favorites"]; return t
        if p in ("/api/v1/tones/trending", "/api/v1/tones/latest"):
            return self.send_json(200, {"data": [fresh(1), fresh(2), fresh(3)]})
        if p in ("/api/v1/tones/favorited", "/api/v1/tones/downloaded", "/api/v1/tones/created"):
            data = [fresh(i) for i in sorted(state["favorites"])] if p.endswith("favorited") else [fresh(2)]
            return self.send_json(200, {"data": data, "page": 1, "page_size": 25,
                                        "total": len(data), "total_pages": 1})
        if p.startswith("/api/v1/tones/"):
            tid = int(p.split("/")[4])
            if tid not in TONES:
                return self.send_json(404, {"error": "not_found"})
            return self.send_json(200, fresh(tid))
        if p == "/api/v1/models":
            tid = int(q.get("tone_id", "0"))
            if tid not in TONES:
                return self.send_json(400, {"error": "tone_id required"})
            data = models(tid, q.get("architecture"))
            return self.send_json(200, {"data": data, "page": 1, "page_size": 300,
                                        "total": len(data), "total_pages": 1})
        if p.startswith("/api/v1/models/"):
            mid = int(p.split("/")[4])
            for tid in TONES:
                for m in models(tid, None):
                    if m["id"] == mid:
                        return self.send_json(200, m)
            return self.send_json(404, {"error": "not_found"})
        if p.startswith("/api/v1/files/"):
            # Redirect to "storage" like the real thing; the client must not
            # forward the Bearer header there.
            self.send_response(302)
            self.send_header("Location", f"{base()}/storage/{p.split('/')[-1]}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        return self.send_json(404, {"error": "not_found"})

    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode()).items()}
        state["requests"].append(("POST", u.path, dict(self.headers)))
        if u.path != "/api/v1/oauth/token":
            return self.send_json(404, {"error": "not_found"})
        if form.get("grant_type") == "authorization_code":
            import base64, hashlib
            want = base64.urlsafe_b64encode(hashlib.sha256(form.get("code_verifier", "").encode()).digest()).rstrip(b"=").decode()
            if form.get("code") != "code-xyz" or want != state.get("challenge"):
                return self.send_json(400, {"error": "invalid_grant", "error_description": "bad code/verifier"})
            if form.get("redirect_uri") != state.get("redirect"):
                return self.send_json(400, {"error": "invalid_grant", "error_description": "redirect mismatch"})
            return self.send_json(200, {"access_token": state["token"], "refresh_token": state["refresh"],
                                        "token_type": "bearer", "expires_in": 3600, "scope": ""})
        if form.get("grant_type") == "refresh_token":
            if form.get("refresh_token") != state["refresh"]:
                return self.send_json(400, {"error": "invalid_grant"})
            n = int(state["token"].split("-")[1]) + 1
            state["token"] = f"tok-{n}"
            state["refresh"] = f"ref-{n}"
            return self.send_json(200, {"access_token": state["token"], "refresh_token": state["refresh"],
                                        "token_type": "bearer", "expires_in": 3600, "scope": ""})
        return self.send_json(400, {"error": "unsupported_grant_type"})

    def do_PUT(self):
        u = urlparse(self.path)
        state["requests"].append(("PUT", u.path, dict(self.headers)))
        if not self.auth_ok():
            return self.send_json(401, {"error": "unauthorized"})
        parts = u.path.split("/")
        if len(parts) == 6 and parts[5] == "favorite":
            state["favorites"].add(int(parts[4]))
            return self.send_json(200, {"id": 1, "tone_id": int(parts[4]), "user_id": "1", "created_at": "now"})
        return self.send_json(404, {"error": "not_found"})

    def do_DELETE(self):
        u = urlparse(self.path)
        state["requests"].append(("DELETE", u.path, dict(self.headers)))
        if not self.auth_ok():
            return self.send_json(401, {"error": "unauthorized"})
        parts = u.path.split("/")
        if len(parts) == 6 and parts[5] == "favorite":
            state["favorites"].discard(int(parts[4]))
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        return self.send_json(404, {"error": "not_found"})


class Storage(H):
    pass


def storage_get(self):
    """/storage/<file>: the model bytes. Rejects requests carrying Authorization,
    like a signed storage URL would."""
    if "Authorization" in self.headers:
        return self.send_json(400, {"error": "InvalidArgument: only one auth mechanism allowed"})
    name = self.path.split("/")[-1]
    if MODEL_FILE and name.endswith(".nam") and os.path.exists(MODEL_FILE):
        with open(MODEL_FILE, "rb") as f:
            return self.send_bytes(f.read(), "application/octet-stream")
    body = json.dumps({"version": "0.5.0", "architecture": "Linear", "config": {"receptive_field": 1, "bias": False},
                       "weights": [1.0], "sample_rate": 48000, "metadata": {"name": name}}).encode()
    if name.endswith(".wav"):
        body = b"RIFF" + struct.pack("<I", 36 + 8) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16) \
               + b"data" + struct.pack("<I", 8) + b"\x00" * 8
    return self.send_bytes(body, "application/octet-stream")


_orig_get = H.do_GET


def do_GET(self):
    if urlparse(self.path).path.startswith("/storage/"):
        state["requests"].append(("GET", urlparse(self.path).path, dict(self.headers)))
        return storage_get(self)
    return _orig_get(self)


H.do_GET = do_GET

if __name__ == "__main__":
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"mock tone3000 on {PORT}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
