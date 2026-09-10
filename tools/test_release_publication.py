import copy
import contextlib
import csv
import io
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

from generate_release_manifest import DEFAULT_FS_PARTITION_SIZE, build_catalog_manifest, build_manifest, detect_pcb_version, sign_manifest, write_manifest
import release_publication as release
import generate_release_manifest as manifestTool
from verify_release_assets import fileRecord, verifyAssets


class PublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory()
        cls.keyDir = Path(cls.keys.name)
        cls.private = cls.keyDir / "private.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                        "-out", str(cls.private)], check=True, capture_output=True)
        public = cls.keyDir / "hds_ota_manifest_public_key_1.pem"
        subprocess.run(["openssl", "pkey", "-in", str(cls.private), "-pubout", "-out", str(public)],
                       check=True, capture_output=True)
        for index in (2, 3):
            shutil.copyfile(public, cls.keyDir / f"hds_ota_manifest_public_key_{index}.pem")

    @classmethod
    def tearDownClass(cls):
        cls.keys.cleanup()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.previousDir = self.directory / "previous"
        self.previousDir.mkdir()
        self.args = SimpleNamespace(tag="v3.1.14", commit="a" * 40, repository="decentespresso/openscale",
                                    previous_tag="v3.1.13", directory=self.directory, expected_dir=self.directory,
                                    build_dir=self.directory, previous_directory=self.previousDir, evidence_sha256="")
        self.enterContext(patch.object(release, "KEY_DIR", self.keyDir))
        self.enterContext(patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": "", "HDS_OTA_SIGNING_KEY_FILE": ""}))
        self.createAssets()

    def signJson(self, value, directory, name):
        write_manifest(value, directory / f"{name}.json")
        sign_manifest(directory / f"{name}.json", directory / f"{name}.sig", self.private)

    def createAssets(self):
        for name, data in (("firmware.bin", b"FW: " + self.args.tag.removeprefix("v").encode() + b"\0"),
                           ("littlefs.bin", bytes(DEFAULT_FS_PARTITION_SIZE)), ("bootloader.bin", b"bootloader"),
                           ("partitions.bin", b"partitions"), ("dependencies.txt", b"dependencies")):
            (self.directory / name).write_bytes(data)
        self.previous = build_manifest(self.directory, "v3.1.13", self.args.repository, "hds", "3.0.0")
        self.signJson(self.previous, self.previousDir, "manifest")
        self.manifest = build_catalog_manifest(
            build_manifest(self.directory, self.args.tag, self.args.repository, "hds", "3.0.0",
                           pcb=detect_pcb_version(release.ROOT / "include/config.h"), forward_recovery=1),
            [self.previous], "v3.1.13")
        self.signJson(self.manifest, self.directory, "manifest")
        with zipfile.ZipFile(self.directory / f"HDS_FW_{self.args.tag}.zip", "w") as archive:
            for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"):
                archive.write(self.directory / name, name)
        self.evidence = {
            "schema": 1, "tag": self.args.tag, "commit": self.args.commit, "repository": self.args.repository,
            "previous_tag": "v3.1.13", "run_id": 123, "run_attempt": 1, "workflow_commit": "b" * 40,
            "artifacts": release.inventory(self.directory, self.args.tag),
        }
        self.signEvidence()

    def signEvidence(self):
        self.signJson(self.evidence, self.directory, "release-evidence")
        self.args.evidence_sha256 = fileRecord(self.directory / "release-evidence.json")["sha256"]

    def testPreviousCatalogHasExpectedIdentity(self):
        self.assertEqual(release.previousCatalog(self.previousDir, "v3.1.13"), self.previous)
        with self.assertRaisesRegex(ValueError, "Wrong previous"):
            release.previousCatalog(self.previousDir, "v3.1.12")
        (self.previousDir / "manifest.sig").write_bytes(b"invalid")
        with self.assertRaisesRegex(ValueError, "signature"):
            release.previousCatalog(self.previousDir, "v3.1.13")

    def testEntireCompatibleHistoryIsPreserved(self):
        verifyAssets(self.directory, None, self.directory, self.keyDir, self.args.tag, self.previous)
        self.assertEqual(self.manifest["releases"][1], self.previous)
        for entries in (self.manifest["releases"][:1], [self.manifest["releases"][0], {**self.previous, "min_from": "3.1.0"}]):
            self.signJson({**self.manifest, "releases": entries}, self.directory, "manifest")
            with self.assertRaisesRegex(ValueError, "history"):
                verifyAssets(self.directory, None, None, self.keyDir, self.args.tag, self.previous)

    def testPreviousCatalogMustContainItsOwnRelease(self):
        self.signJson({**self.previous, "releases": []}, self.previousDir, "manifest")
        with self.assertRaisesRegex(ValueError, "omits its own"):
            release.previousCatalog(self.previousDir, "v3.1.13")

    def testResignedIncompatibleManifestsAreRejectedBeforeEvidence(self):
        changes = {
            "model": "wrong", "pcb": "PCB: 7.2", "chip": "esp32", "environment": "wrong",
            "partition_schema": "wrong", "flash_size": 16777216, "app_partition_min_size": 8388608,
            "fs_partition_label": "wrong", "fs_partition_size": 1, "fs_schema": 2,
            "min_from": "3.1.14", "forward_recovery": 0,
            "release_notes_url": "https://example.invalid/notes",
            "firmware.url": "https://github.com/decentespresso/openscale/releases/download/v3.1.13/firmware.bin",
            "littlefs.url": "https://example.invalid/littlefs.bin",
            "firmware.size": 8388608, "littlefs.size": 1,
        }
        original = copy.deepcopy(self.manifest)
        for field, value in changes.items():
            with self.subTest(field=field):
                current = {key: copy.deepcopy(item) for key, item in original.items() if key != "releases"}
                if "." in field:
                    asset, key = field.split(".")
                    current[asset][key] = value
                else:
                    current[field] = value
                manifest = build_catalog_manifest(current, [self.previous], "v3.1.13")
                self.signJson(manifest, self.directory, "manifest")
                with patch.object(release, "verifyCandidate"), patch.object(release, "run", return_value=self.args.commit), \
                     patch.object(release, "latestStable", return_value="v3.1.13"), \
                     patch.object(release, "sign_manifest_from_environment") as sign:
                    with self.assertRaises(ValueError):
                        release.prepareEvidence(self.args)
                    sign.assert_not_called()

    def testReleaseDefaultsMatchFirmwareAndPartitionLayout(self):
        source = (release.ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
        for name in ("CHIP", "ENVIRONMENT", "PARTITION_SCHEMA", "FLASH_SIZE", "APP_PARTITION_MIN_SIZE",
                     "FS_PARTITION_LABEL", "FS_PARTITION_SIZE", "FS_SCHEMA"):
            value = getattr(manifestTool, "DEFAULT_" + name)
            literal = json.dumps(value)
            self.assertRegex(source, rf'\bHDS_OTA_{name}\s*=\s*{re.escape(literal)};')
        lines = (release.ROOT / "partitions/default_8MB.csv").read_text().splitlines()
        rows = [tuple(field.strip() for field in row) for row in csv.reader(line for line in lines if not line.startswith("#"))]
        self.assertEqual(min(int(row[4], 0) for row in rows if row[1] == "app"), manifestTool.DEFAULT_APP_PARTITION_MIN_SIZE)
        filesystem = next(row for row in rows if row[0] == manifestTool.DEFAULT_FS_PARTITION_LABEL)
        self.assertEqual(int(filesystem[4], 0), manifestTool.DEFAULT_FS_PARTITION_SIZE)
        self.assertEqual(max(int(row[3], 0) + int(row[4], 0) for row in rows), manifestTool.DEFAULT_FLASH_SIZE)

    def testMinimumSourceIncludesPreviousStable(self):
        for minimum in ("", "3.0.0", "3.1.13", "3.1.14", "invalid", None):
            with self.subTest(minimum=minimum):
                current = {key: value for key, value in self.manifest.items() if key != "releases"}
                current["min_from"] = minimum
                self.signJson(build_catalog_manifest(current, [self.previous], "v3.1.13"), self.directory, "manifest")
                if minimum in ("", "3.0.0", "3.1.13"):
                    verifyAssets(self.directory, None, self.directory, self.keyDir, self.args.tag, self.previous)
                else:
                    with self.assertRaisesRegex(ValueError, "min_from"):
                        verifyAssets(self.directory, None, None, self.keyDir, self.args.tag, self.previous)

    def testCandidateCannotReplaceANewerOrEqualStable(self):
        release.verifyPreviousVersion("v3.1.14", "v3.1.13")
        release.verifyPreviousVersion("v3.1.14-rc.1", "v3.1.13")
        release.verifyPreviousVersion("v3.1.14", "")
        for tag in ("v3.1.12", "v3.1.13", "v3.1.13-rc.1"):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "newer"):
                release.verifyPreviousVersion(tag, "v3.1.13")

    def testEvidenceIdentityCannotBeSubstituted(self):
        for field, value in (("tag", "v3.1.15"), ("commit", "c" * 40),
                             ("repository", "other/repository"), ("schema", 2)):
            with self.subTest(field=field):
                self.createAssets()
                self.evidence[field] = value
                self.signEvidence()
                with self.assertRaisesRegex(ValueError, "identity"):
                    release.verifyEvidence(self.directory, self.args, self.args.evidence_sha256)

    def testDownloadRetriesUseFreshCompletePairs(self):
        for mode in ("both_fail", "missing_json", "missing_sig", "retry"):
            with self.subTest(mode=mode):
                attempts = []
                def download(command):
                    directory = Path(command[command.index("--dir") + 1])
                    attempts.append(directory)
                    if mode == "both_fail" or (mode == "retry" and len(attempts) == 1):
                        raise subprocess.CalledProcessError(1, command)
                    for name in ("manifest.json", "manifest.sig"):
                        if mode != "missing_" + name.split(".")[1]:
                            shutil.copyfile(self.previousDir / name, directory / name)
                    return ""
                with patch.object(release, "run", side_effect=download), patch.object(release.time, "sleep"):
                    if mode == "retry":
                        release.downloadAssets(self.args.repository, "v3.1.13", ("manifest.json", "manifest.sig"), self.directory / mode)
                        self.assertEqual(len(attempts), 2)
                    else:
                        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                            release.downloadAssets(self.args.repository, "v3.1.13", ("manifest.json", "manifest.sig"), self.directory / mode)
                        self.assertEqual(len(attempts), 3)
                self.assertEqual(len(set(attempts)), len(attempts))

    def testCandidateRejectsMovedOrMalformedTags(self):
        for tag, commit in (("--bad", self.args.commit), (self.args.tag, "main")):
            with patch.object(release, "run") as command, self.assertRaises(ValueError):
                release.verifyCandidate(tag, commit)
            command.assert_not_called()
        with patch.object(release, "run", side_effect=["", "c" * 40]), self.assertRaisesRegex(ValueError, "approved commit"):
            release.verifyCandidate(self.args.tag, self.args.commit)

    def testEvidencePreparationUsesVerifiedBuildAndRealSignature(self):
        environment = {"HDS_OTA_SIGNING_KEY_PEM": self.private.read_text(), "GITHUB_RUN_ID": "123",
                       "GITHUB_RUN_ATTEMPT": "1", "GITHUB_WORKFLOW_SHA": "b" * 40}
        with patch.dict(os.environ, environment), patch.object(release, "verifyCandidate"), \
             patch.object(release, "latestStable", return_value="v3.1.13"), \
             patch.object(release, "run", return_value=self.args.commit):
            release.prepareEvidence(self.args)
        digest = fileRecord(self.directory / "release-evidence.json")["sha256"]
        self.assertEqual(release.verifyEvidence(self.directory, self.args, digest), self.evidence)

    def testDraftLookupUsesReleaseIdWhenTagEndpointIsUnavailable(self):
        for tag in ("v3.1.14", "v3.1.14-preview.4"):
            with self.subTest(tag=tag):
                args = SimpleNamespace(tag=tag, repository=self.args.repository)
                draft = {"id": 42, "draft": True, "prerelease": "-" in tag, "tag_name": tag,
                         "assets": [{"name": name} for name in (*release.releaseNames(tag), *release.EVIDENCE_NAMES)]}
                lookup = ["gh", "release", "view", tag, "--repo", args.repository, "--json", "databaseId"]
                fetch = ["gh", "api", f"repos/{args.repository}/releases/42"]
                def github(command):
                    if command == lookup:
                        return json.dumps({"databaseId": 42})
                    if command == fetch:
                        return json.dumps(draft)
                    raise subprocess.CalledProcessError(1, command, stderr="HTTP 404")
                with patch.object(release, "run", side_effect=github) as command:
                    self.assertEqual(release.draftMetadata(args), draft)
                    self.assertEqual([call.args[0] for call in command.call_args_list], [lookup, fetch])

    def testDraftLookupFailsClosed(self):
        for identity in ({}, {"databaseId": None}, {"databaseId": True}, {"databaseId": 0},
                         {"databaseId": -1}, {"databaseId": "42"}):
            with self.subTest(identity=identity), patch.object(release, "run", return_value=json.dumps(identity)), \
                 patch.object(release, "githubJson") as github:
                with self.assertRaisesRegex(ValueError, "release ID"):
                    release.draftMetadata(self.args)
                github.assert_not_called()
        with patch.object(release, "run", side_effect=subprocess.CalledProcessError(1, ["gh"])), \
             patch.object(release, "githubJson") as github:
            with self.assertRaises(subprocess.CalledProcessError):
                release.draftMetadata(self.args)
            github.assert_not_called()

    def publicationHarness(self, mode="success", publish=True):
        names = (*release.releaseNames(self.args.tag), *release.EVIDENCE_NAMES)
        draft = {"id": 42, "draft": True, "prerelease": "-" in self.args.tag, "tag_name": self.args.tag,
                 "assets": [{"id": index, "name": name, **fileRecord(self.directory / name)} for index, name in enumerate(names)]}
        current = copy.deepcopy(draft)
        if mode == "changed_assets":
            current["assets"][0]["id"] += 100
        if mode == "replaced_draft":
            current["id"] += 1
        if mode == "not_draft":
            draft["draft"] = False
        if mode == "wrong_tag":
            draft["tag_name"] = "v3.1.15"
        if mode == "wrong_classification":
            draft["prerelease"] = not draft["prerelease"]
        if mode == "extra_asset":
            draft["assets"].append({"id": 99, "name": "unexpected"})
        if mode == "bad_digest":
            self.args.evidence_sha256 = "0" * 64
        if mode == "bad_signature":
            (self.directory / "release-evidence.sig").write_bytes(b"invalid")
        workflowRun = {"id": 123, "run_attempt": 1, "event": "workflow_dispatch", "head_branch": "main",
                       "head_sha": "b" * 40, "path": ".github/workflows/release.yml", "status": "completed", "conclusion": "success"}
        for field in ("conclusion", "status", "head_sha", "path", "run_attempt"):
            if mode == field:
                workflowRun[field] = "wrong"
        def download(repository, tag, requested, directory):
            source = self.previousDir if tag == "v3.1.13" else self.directory
            directory.mkdir(parents=True, exist_ok=True)
            for name in requested:
                shutil.copyfile(source / name, directory / name)
            if mode in release.releaseNames(self.args.tag) and tag == self.args.tag:
                (directory / mode).write_bytes(b"changed")
        responses = [draft, workflowRun, current] if publish else [draft]
        latest = ["v3.1.13", "v3.1.14" if mode == "changed_latest" else "v3.1.13"]
        lookupIds = iter((draft["id"], current["id"]))
        def run(command):
            return json.dumps({"databaseId": next(lookupIds)}) if command[:3] == ["gh", "release", "view"] else ""
        with patch.object(release, "verifyCandidate"), patch.object(release, "downloadAssets", side_effect=download), \
             patch.object(release, "githubJson", side_effect=responses), patch.object(release, "latestStable", side_effect=latest), \
             patch.object(release, "run", side_effect=run) as command, \
             patch.object(release, "sign_manifest_from_environment") as sign, \
             contextlib.redirect_stdout(io.StringIO()):
            if mode != "success":
                with self.assertRaises(ValueError):
                    release.verifyDraft(self.args, publish=publish)
            else:
                release.verifyDraft(self.args, publish=publish)
            lookup = ["gh", "release", "view", self.args.tag, "--repo", self.args.repository, "--json", "databaseId"]
            writes = [call.args[0] for call in command.call_args_list if call.args[0] != lookup]
            expected = [["gh", "release", "edit", self.args.tag, "--repo", self.args.repository,
                         "--verify-tag", "--draft=false", "--latest=false" if "-" in self.args.tag else "--latest=true"]]
            self.assertEqual(writes, expected if publish and mode == "success" else [])
            sign.assert_not_called()

    def testPreparationNeverPublishes(self):
        self.publicationHarness(publish=False)

    def testPublishApprovesExistingBytesWithoutSigning(self):
        self.publicationHarness()
        self.args.tag = "v3.1.14-rc.1"
        self.createAssets()
        self.publicationHarness()

    def testFailuresNeverPublish(self):
        for mode in ("conclusion", "status", "head_sha", "path", "run_attempt", "bad_digest", "bad_signature",
                     "changed_assets", "replaced_draft", "changed_latest", "not_draft", "wrong_tag",
                     "wrong_classification", "extra_asset", *release.releaseNames(self.args.tag)):
            with self.subTest(mode=mode):
                self.createAssets()
                self.publicationHarness(mode)


if __name__ == "__main__":
    unittest.main()
