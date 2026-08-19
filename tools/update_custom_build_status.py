import argparse
import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request


HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ATTEMPT_PATTERN = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
STATES = {"building", "ready", "failed"}


def updateStatus(baseUrl, token, combinationHash, attemptId, state):
    parsed = urllib.parse.urlsplit(baseUrl)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("status URL must be HTTPS without credentials")
    if parsed.query or parsed.fragment:
        raise ValueError("status URL cannot contain a query or fragment")
    if len(token) < 32:
        raise ValueError("upload token must contain at least 32 characters")
    if not HASH_PATTERN.fullmatch(combinationHash):
        raise ValueError("combination hash must be 64 lowercase hex characters")
    if not ATTEMPT_PATTERN.fullmatch(attemptId):
        raise ValueError("attempt id must be a lowercase UUIDv4")
    if state not in STATES:
        raise ValueError("invalid build state")
    request = urllib.request.Request(
        f"{baseUrl.rstrip('/')}/internal/v1/status/{combinationHash}",
        data=json.dumps({"state": state, "attempt_id": attemptId}).encode("utf-8"),
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "User-Agent": "OpenScale-Custom-Build/1.0",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        if response.status != 204:
            raise ValueError(f"status update failed: HTTP {response.status}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--combination-hash", required=True)
    parser.add_argument("--attempt-id", required=True)
    parser.add_argument("--state", choices=sorted(STATES), required=True)
    args = parser.parse_args()
    try:
        updateStatus(
            args.base_url,
            os.environ.get("CUSTOM_BUILD_UPLOAD_TOKEN", ""),
            args.combination_hash,
            args.attempt_id,
            args.state,
        )
    except (ValueError, urllib.error.HTTPError, urllib.error.URLError) as error:
        parser.exit(1, f"custom build status update failed: {error}\n")


if __name__ == "__main__":
    main()
