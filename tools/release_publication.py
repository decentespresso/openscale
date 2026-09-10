import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from generate_release_manifest import RELEASE_VERSION_RE, STABLE_VERSION_RE, sign_manifest_from_environment, version_key
from verify_release_assets import fileRecord, requireEqual, signedJson, verifyAssets


ROOT = Path(__file__).resolve().parents[1]
KEY_DIR = ROOT / "keys/ota"
EVIDENCE_NAMES = ("release-evidence.json", "release-evidence.sig")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(command, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, timeout=120).stdout.strip()


def releaseNames(tag):
    require(RELEASE_VERSION_RE.fullmatch(tag) is not None, "Invalid release tag")
    return (f"HDS_FW_{tag}.zip", "firmware.bin", "littlefs.bin", "dependencies.txt", "manifest.json", "manifest.sig")


def verifyCandidate(tag, commit):
    releaseNames(tag)
    require(re.fullmatch(r"[0-9a-f]{40}", commit) is not None, "Expected a full candidate SHA")
    run(["git", "fetch", "--force", "origin", "main:refs/remotes/origin/main", f"refs/tags/{tag}:refs/tags/{tag}"])
    actual = run(["git", "rev-parse", "--verify", "--end-of-options", f"refs/tags/{tag}^{{commit}}"])
    requireEqual(actual, commit, "Tag does not match the approved commit")
    run(["git", "merge-base", "--is-ancestor", commit, "origin/main"])


def githubJson(repository, resource):
    return json.loads(run(["gh", "api", f"repos/{repository}/{resource}"]))


def latestStable(repository):
    releases = json.loads(run(["gh", "release", "list", "--repo", repository,
                               "--exclude-drafts", "--exclude-pre-releases", "--limit", "1", "--json", "tagName"]))
    return releases[0]["tagName"] if releases else ""


def downloadAssets(repository, tag, names, directory):
    releaseNames(tag)
    for attempt in range(3):
        try:
            with tempfile.TemporaryDirectory() as stagingDirectory:
                staging = Path(stagingDirectory)
                command = ["gh", "release", "download", tag, "--repo", repository, "--dir", str(staging)]
                for name in names:
                    command.extend(["--pattern", name])
                run(command)
                requireEqual(sorted(path.name for path in staging.iterdir()), sorted(names), "Incomplete asset download")
                for name in names:
                    require(fileRecord(staging / name)["size"] > 0, f"Empty asset: {name}")
                directory.mkdir(parents=True, exist_ok=True)
                for name in names:
                    shutil.copyfile(staging / name, directory / name)
            return
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, ValueError):
            if attempt == 2:
                raise
            time.sleep(5)


def previousCatalog(directory, tag):
    if not tag:
        return {}
    require(STABLE_VERSION_RE.fullmatch(tag) is not None, "Previous tag must be stable")
    previous = signedJson(directory / "manifest.json", directory / "manifest.sig", KEY_DIR)
    requireEqual(previous.get("version"), tag.removeprefix("v"), "Wrong previous stable catalog")
    entries = previous.get("releases", [previous])
    require(isinstance(entries, list) and any(isinstance(entry, dict) and entry.get("version") == previous["version"]
            for entry in entries), "Previous catalog omits its own release")
    return previous


def verifyPreviousVersion(tag, previousTag):
    if previousTag:
        require(version_key(tag.split("-", 1)[0]) > version_key(previousTag), "Candidate must be newer than previous stable")


def inventory(directory, tag):
    return {name: fileRecord(directory / name) for name in releaseNames(tag)}


def prepareEvidence(args):
    verifyCandidate(args.tag, args.commit)
    requireEqual(run(["git", "rev-parse", "HEAD"]), args.commit, "Build checkout differs from candidate")
    requireEqual(latestStable(args.repository), args.previous_tag, "Previous stable changed during preparation")
    verifyPreviousVersion(args.tag, args.previous_tag)
    previous = previousCatalog(args.previous_directory, args.previous_tag)
    verifyAssets(args.directory, args.expected_dir, args.build_dir, KEY_DIR, args.tag, previous)
    evidence = {
        "schema": 1, "tag": args.tag, "commit": args.commit, "previous_tag": args.previous_tag,
        "repository": args.repository, "artifacts": inventory(args.directory, args.tag),
        "run_id": int(os.environ["GITHUB_RUN_ID"]), "run_attempt": int(os.environ["GITHUB_RUN_ATTEMPT"]),
        "workflow_commit": os.environ["GITHUB_WORKFLOW_SHA"],
    }
    path = args.directory / EVIDENCE_NAMES[0]
    path.write_text(json.dumps(evidence, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    require(sign_manifest_from_environment(path, args.directory / EVIDENCE_NAMES[1]), "Missing signing key")
    verifyEvidence(args.directory, args, fileRecord(path)["sha256"])


def verifyEvidence(directory, args, digest):
    require(re.fullmatch(r"[0-9a-f]{64}", digest) is not None, "Expected an approved SHA-256 digest")
    path = directory / EVIDENCE_NAMES[0]
    requireEqual(fileRecord(path)["sha256"], digest, "Approved evidence digest differs")
    evidence = signedJson(path, directory / EVIDENCE_NAMES[1], KEY_DIR)
    requireEqual((evidence.get("schema"), evidence.get("tag"), evidence.get("commit"), evidence.get("repository")),
                 (1, args.tag, args.commit, args.repository), "Evidence identity differs")
    requireEqual(evidence.get("artifacts"), inventory(directory, args.tag), "Assets differ from signed evidence")
    for field in ("run_id", "run_attempt"):
        require(type(evidence.get(field)) is int and evidence[field] > 0, "Invalid preparation run")
    require(isinstance(evidence.get("workflow_commit"), str) and
            re.fullmatch(r"[0-9a-f]{40}", evidence["workflow_commit"]) is not None, "Invalid workflow SHA")
    previous = evidence.get("previous_tag")
    require(isinstance(previous, str) and (not previous or STABLE_VERSION_RE.fullmatch(previous)), "Invalid previous tag")
    return evidence


def draftMetadata(args):
    release = githubJson(args.repository, f"releases/tags/{args.tag}")
    require(release.get("draft") is True, "Release is not a draft")
    requireEqual(release.get("tag_name"), args.tag, "Draft tag differs")
    require(release.get("prerelease") is ("-" in args.tag), "Draft classification differs")
    requireEqual(sorted(asset["name"] for asset in release["assets"]),
                 sorted((*releaseNames(args.tag), *EVIDENCE_NAMES)), "Draft asset set differs")
    return release


def assetSnapshot(release):
    return sorted(tuple(asset.get(field) for field in ("name", "id", "size", "digest", "updated_at"))
                  for asset in release["assets"])


def validateRun(evidence, workflowRun):
    expected = {
        "id": evidence["run_id"], "run_attempt": evidence["run_attempt"],
        "event": "workflow_dispatch", "head_branch": "main", "head_sha": evidence["workflow_commit"],
        "path": ".github/workflows/release.yml", "status": "completed", "conclusion": "success",
    }
    require(all(workflowRun.get(key) == value for key, value in expected.items()), "Preparation run did not succeed for this evidence")


def verifyDraft(args, publish=False):
    verifyCandidate(args.tag, args.commit)
    release = draftMetadata(args)
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        directory = Path(temporaryDirectory)
        downloadAssets(args.repository, args.tag, (*releaseNames(args.tag), *EVIDENCE_NAMES), directory)
        digest = args.evidence_sha256 if publish else fileRecord(args.directory / EVIDENCE_NAMES[0])["sha256"]
        evidence = verifyEvidence(directory, args, digest)
        requireEqual(latestStable(args.repository), evidence["previous_tag"], "Previous stable changed")
        verifyPreviousVersion(args.tag, evidence["previous_tag"])
        previousDir = directory / "previous"
        if evidence["previous_tag"]:
            downloadAssets(args.repository, evidence["previous_tag"], ("manifest.json", "manifest.sig"), previousDir)
        previous = previousCatalog(previousDir, evidence["previous_tag"])
        verifyAssets(directory, None, None if publish else args.build_dir, KEY_DIR, args.tag, previous)
        if not publish:
            for name in (*releaseNames(args.tag), *EVIDENCE_NAMES):
                requireEqual(fileRecord(directory / name), fileRecord(args.directory / name), f"Uploaded bytes differ: {name}")
            summary = f"Verified draft {args.tag} at {args.commit}.\n\nEvidence SHA-256: `{digest}`\n\nTest these exact binaries, then approve a separate Publish firmware run.\n"
            print(summary)
            if os.environ.get("GITHUB_STEP_SUMMARY"):
                with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as handle:
                    handle.write(summary)
            return
        workflowRun = githubJson(args.repository, f"actions/runs/{evidence['run_id']}")
        validateRun(evidence, workflowRun)
        verifyCandidate(args.tag, args.commit)
        current = draftMetadata(args)
        requireEqual(current["id"], release["id"], "Draft was replaced")
        requireEqual(assetSnapshot(current), assetSnapshot(release), "Draft assets changed during verification")
        requireEqual(latestStable(args.repository), evidence["previous_tag"], "Previous stable changed during verification")
        run(["gh", "release", "edit", args.tag, "--repo", args.repository, "--draft=false",
             "--latest=false" if "-" in args.tag else "--latest=true"])
        print(f"Published verified release {args.tag} at {args.commit}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("candidate", "evidence", "verify-draft", "publish"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", "decentespresso/openscale"))
    parser.add_argument("--previous-tag", default="")
    parser.add_argument("--directory", type=Path, default=Path("downloaded-release"))
    parser.add_argument("--expected-dir", type=Path, default=Path("release-files"))
    parser.add_argument("--build-dir", type=Path, default=Path(".pio.nosync/build/esp32s3"))
    parser.add_argument("--previous-directory", type=Path, default=Path("previous-release"))
    parser.add_argument("--evidence-sha256", default="")
    args = parser.parse_args()
    releaseNames(args.tag)
    require(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None, "Expected a full candidate SHA")
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository) is not None, "Invalid repository")
    if args.command == "candidate":
        verifyCandidate(args.tag, args.commit)
    elif args.command == "evidence":
        prepareEvidence(args)
    else:
        verifyDraft(args, publish=args.command == "publish")


if __name__ == "__main__":
    main()
