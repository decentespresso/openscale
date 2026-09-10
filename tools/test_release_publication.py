import copy
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

from generate_release_manifest import build_catalog_manifest, build_manifest, sign_manifest, write_manifest
import release_publication as release
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
                           ("littlefs.bin", b"filesystem"), ("bootloader.bin", b"bootloader"),
                           ("partitions.bin", b"partitions"), ("dependencies.txt", b"dependencies")):
            (self.directory / name).write_bytes(data)
        self.previous = build_manifest(self.directory, "v3.1.13", self.args.repository, "hds", "3.0.0")
        self.signJson(self.previous, self.previousDir, "manifest")
        self.manifest = build_catalog_manifest(
            build_manifest(self.directory, self.args.tag, self.args.repository, "hds", "3.0.0"),
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
        with patch.object(release, "verifyCandidate"), patch.object(release, "downloadAssets", side_effect=download), \
             patch.object(release, "githubJson", side_effect=responses), patch.object(release, "latestStable", side_effect=latest), \
             patch.object(release, "run") as command, patch.object(release, "sign_manifest_from_environment") as sign, \
             contextlib.redirect_stdout(io.StringIO()):
            if mode != "success":
                with self.assertRaises(ValueError):
                    release.verifyDraft(self.args, publish=publish)
                command.assert_not_called()
            else:
                release.verifyDraft(self.args, publish=publish)
                if publish:
                    command.assert_called_once_with(["gh", "release", "edit", self.args.tag, "--repo", self.args.repository,
                                                    "--draft=false", "--latest=false" if "-" in self.args.tag else "--latest=true"])
                else:
                    command.assert_not_called()
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
                     "changed_assets", "replaced_draft", "changed_latest", "not_draft", "extra_asset", *release.releaseNames(self.args.tag)):
            with self.subTest(mode=mode):
                self.createAssets()
                self.publicationHarness(mode)


if __name__ == "__main__":
    unittest.main()
