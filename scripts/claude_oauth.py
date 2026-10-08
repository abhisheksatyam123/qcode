#!/usr/bin/env python3
"""Claude Code OAuth helper for qcode.

Manages Anthropic Claude Code subscription credentials (~/.claude/.credentials.json).
Usage:
    python3 scripts/claude_oauth.py --status
    python3 scripts/claude_oauth.py --token
    python3 scripts/claude_oauth.py --refresh
    python3 scripts/claude_oauth.py --login
"""

import argparse
import base64
import hashlib
import http.server
import json
import os
import secrets
import sys
import time
import urllib.parse
import urllib.request
import webbrowser

CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
AUTHORIZE_URL = "https://claude.com/cai/oauth/authorize"
TOKEN_URL = "https://platform.claude.com/v1/oauth/token"
SUCCESS_URL = "https://platform.claude.com/oauth/code/success?app=claude-code"
SCOPES = [
    "user:profile",
    "user:inference",
    "user:sessions:claude_code",
    "user:mcp_servers",
    "user:file_upload",
]

CREDENTIALS_PATH = os.path.expanduser("~/.claude/.credentials.json")
REFRESH_BUFFER_MS = 5 * 60 * 1000  # 5 minutes


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def load_credentials() -> dict:
    if not os.path.exists(CREDENTIALS_PATH):
        return {}
    try:
        with open(CREDENTIALS_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        print(f"Error reading {CREDENTIALS_PATH}: {e}", file=sys.stderr)
        return {}


def save_credentials(oauth_data: dict):
    os.makedirs(os.path.dirname(CREDENTIALS_PATH), exist_ok=True)
    existing = load_credentials()
    existing["claudeAiOauth"] = oauth_data
    tmp_path = CREDENTIALS_PATH + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(existing, f, indent=2)
    os.chmod(tmp_path, 0o600)
    os.replace(tmp_path, CREDENTIALS_PATH)


def is_expired(expires_at_ms: int) -> bool:
    if expires_at_ms <= 0:
        return False
    return (int(time.time() * 1000) + REFRESH_BUFFER_MS) >= expires_at_ms


def refresh_token(refresh_tok: str) -> dict:
    data = json.dumps({
        "grant_type": "refresh_token",
        "refresh_token": refresh_tok,
        "client_id": CLIENT_ID,
        "scope": " ".join(SCOPES),
    }).encode("utf-8")
    req = urllib.request.Request(
        TOKEN_URL,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))

    now_ms = int(time.time() * 1000)
    expires_in = res.get("expires_in", 3600)
    expires_at = now_ms + (expires_in * 1000)

    creds = load_credentials().get("claudeAiOauth", {})
    creds["accessToken"] = res["access_token"]
    creds["refreshToken"] = res.get("refresh_token", refresh_tok)
    creds["expiresAt"] = expires_at

    save_credentials(creds)
    return creds


def get_token(force_refresh: bool = False) -> str:
    creds = load_credentials().get("claudeAiOauth", {})
    access = creds.get("accessToken", "")
    refresh_tok = creds.get("refreshToken", "")
    expires_at = creds.get("expiresAt", 0)

    if not access or not refresh_tok:
        print("No Claude Code credentials found. Run with --login first.", file=sys.stderr)
        sys.exit(1)

    if force_refresh or is_expired(expires_at):
        updated = refresh_token(refresh_tok)
        return updated.get("accessToken", "")
    return access


def login():
    verifier = b64url(secrets.token_bytes(32))
    challenge = b64url(hashlib.sha256(verifier.encode("ascii")).digest())
    state = b64url(secrets.token_bytes(32))

    captured_code = []

    class CallbackHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            params = urllib.parse.parse_qs(parsed.query)
            if params.get("state", [""])[0] == state and "code" in params:
                captured_code.append(params["code"][0])
                self.send_response(302)
                self.send_header("Location", SUCCESS_URL)
                self.end_headers()
            else:
                self.send_response(400)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"Invalid state or missing code")

        def log_message(self, format, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), CallbackHandler)
    port = server.server_address[1]
    redirect_uri = f"http://localhost:{port}/callback"

    params = {
        "code": "true",
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "scope": " ".join(SCOPES),
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        "state": state,
    }
    auth_url = f"{AUTHORIZE_URL}?{urllib.parse.urlencode(params)}"

    print(f"Opening browser for Claude login:\n  {auth_url}\n")
    webbrowser.open(auth_url)

    server.handle_request()
    server.server_close()

    if not captured_code:
        print("Login failed: no code received.", file=sys.stderr)
        sys.exit(1)

    code = captured_code[0]
    token_req = json.dumps({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": redirect_uri,
        "client_id": CLIENT_ID,
        "code_verifier": verifier,
        "state": state,
    }).encode("utf-8")

    req = urllib.request.Request(
        TOKEN_URL,
        data=token_req,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))

    now_ms = int(time.time() * 1000)
    expires_in = res.get("expires_in", 3600)
    oauth_data = {
        "accessToken": res["access_token"],
        "refreshToken": res.get("refresh_token", ""),
        "expiresAt": now_ms + (expires_in * 1000),
        "scopes": SCOPES,
    }
    save_credentials(oauth_data)
    print("Login successful! Credentials saved to ~/.claude/.credentials.json")


def show_status():
    creds = load_credentials().get("claudeAiOauth", {})
    if not creds:
        print(f"Status: Not logged in (no claudeAiOauth in {CREDENTIALS_PATH})")
        return
    expires_at = creds.get("expiresAt", 0)
    now_ms = int(time.time() * 1000)
    rem_sec = (expires_at - now_ms) / 1000
    expired = is_expired(expires_at)
    access_tok = creds.get("accessToken", "")

    print(f"Claude Code Credentials ({CREDENTIALS_PATH}):")
    print(f"  Access Token:  {access_tok[:12]}... (length: {len(access_tok)})")
    print(f"  Subscription:  {creds.get('subscriptionType', 'unknown')}")
    print(f"  Rate Limit:    {creds.get('rateLimitTier', 'unknown')}")
    print(f"  Expires:       {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(expires_at / 1000))} ({rem_sec/3600:.1f} hours remaining)")
    print(f"  Needs Refresh: {'Yes' if expired else 'No'}")


def main():
    parser = argparse.ArgumentParser(description="Claude Code OAuth helper for qcode")
    parser.add_argument("--login", action="store_true", help="Run PKCE OAuth login in browser")
    parser.add_argument("--refresh", action="store_true", help="Force refresh token")
    parser.add_argument("--token", action="store_true", help="Print valid access token")
    parser.add_argument("--status", action="store_true", help="Show credential status")

    args = parser.parse_args()
    if args.login:
        login()
    elif args.refresh:
        refresh_token(load_credentials().get("claudeAiOauth", {}).get("refreshToken", ""))
        print("Token refreshed successfully.")
    elif args.token:
        print(get_token())
    else:
        show_status()


if __name__ == "__main__":
    main()
