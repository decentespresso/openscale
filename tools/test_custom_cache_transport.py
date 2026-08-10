import hashlib
import io
import json
from pathlib import Path
import tempfile
import urllib.error
from unittest.mock import patch

import build_custom_firmware as customRunner
import configure_custom_build as customBuild
import publish_custom_build as publisher
import update_custom_build_status as statusUpdater


class Response(io.BytesIO):
    status = 201

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class StatusResponse(Response):
    status = 204


def createEntry(root):
    identity = {"schema": 1, "base_source": "1" * 40}
    combinationHash = customBuild.combinationHash(identity)
    entry = root / combinationHash
    entry.mkdir()
    for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"):
        (entry / name).write_bytes(name.encode("ascii"))
    (entry / "dependencies.txt").write_text("dependencies", encoding="utf-8")
    manifest = {
        "combination_hash": combinationHash,
        "combination_input": identity,
        "binaries": {
            name: customBuild.fileMetadata(entry / name)
            for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin")
        },
        "dependencies": customBuild.fileMetadata(entry / "dependencies.txt"),
    }
    (entry / "build-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return entry, manifest


def main():
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        entry, manifest = createEntry(Path(temporaryDirectory))
        baseUrl = "https://openscale-custom-builds.odevstudio.workers.dev"
        with patch.object(customRunner.urllib.request, "urlopen", return_value=Response(json.dumps(manifest).encode("utf-8"))):
            assert customRunner.remoteCacheHit(
                baseUrl, entry.name, manifest["combination_input"]
            )
        missing = urllib.error.HTTPError("missing", 404, "Not Found", {}, None)
        with patch.object(customRunner.urllib.request, "urlopen", side_effect=missing):
            assert not customRunner.remoteCacheHit(
                baseUrl, entry.name, manifest["combination_input"]
            )
        requests = []

        def accept(request, timeout):
            requests.append(request)
            return Response()

        with patch.object(publisher.urllib.request, "urlopen", side_effect=accept):
            assert publisher.publishEntry(entry, baseUrl, "x" * 32) == entry.name
        assert [Path(request.full_url).name for request in requests] == [
            "firmware.bin",
            "bootloader.bin",
            "partitions.bin",
            "littlefs.bin",
            "dependencies.txt",
            "build-manifest.json",
        ]
        for request in requests:
            assert request.get_header("Authorization") == f"Bearer {'x' * 32}"
            assert request.get_header("X-openscale-sha256") == hashlib.sha256(request.data).hexdigest()
        with patch.object(
            statusUpdater.urllib.request, "urlopen", return_value=StatusResponse()
        ) as updateRequest:
            statusUpdater.updateStatus(baseUrl, "x" * 32, entry.name, "building")
        request = updateRequest.call_args.args[0]
        assert request.full_url.endswith(f"/internal/v1/status/{entry.name}")
        assert json.loads(request.data) == {"state": "building"}
        assert request.get_header("Authorization") == f"Bearer {'x' * 32}"
    print("custom cache transport tests passed")


if __name__ == "__main__":
    main()
