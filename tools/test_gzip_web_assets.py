import gzip
import importlib.util
import os
import runpy
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = PROJECT_ROOT / "gzip_web_assets.py"
SPEC = importlib.util.spec_from_file_location("gzip_web_assets", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
syncGzipAssets = MODULE.syncGzipAssets


class FakeEnvironment:
    def __init__(self, clean=False, integrationDump=False):
        self.clean = clean
        self.integrationDump = integrationDump
        self.preActions = []

    def IsCleanTarget(self):
        return self.clean

    def IsIntegrationDump(self):
        return self.integrationDump

    def AddPreAction(self, target, callback):
        self.preActions.append((target, callback))


class GzipWebAssetsTest(unittest.TestCase):
    def testSyncsDeterministicSmallerAssets(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            root = Path(temporaryDirectory)
            assets = {
                "index.html": b"<main>scale</main>" * 400,
                "modules/app.js": b"export const weight = 0;\n" * 400,
                "styles/app.css": b".weight{font-variant-numeric:tabular-nums}\n" * 400,
                "icons/scale.svg": b"<svg><path d='M0 0h10v10z'/></svg>" * 400,
            }
            for relativePath, content in assets.items():
                path = root / relativePath
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)

            unsupported = root / "photo.png"
            unsupported.write_bytes(b"png" * 400)
            unsupportedGzip = root / "photo.png.gz"
            unsupportedGzip.write_bytes(b"keep")
            tiny = root / "tiny.js"
            tiny.write_bytes(b"x")

            written, removed = syncGzipAssets(root)
            self.assertEqual((written, removed), (4, 0))
            self.assertEqual(unsupportedGzip.read_bytes(), b"keep")
            self.assertFalse((root / "tiny.js.gz").exists())

            firstOutputs = {}
            for relativePath, content in assets.items():
                gzipPath = root / (relativePath + ".gz")
                compressed = gzipPath.read_bytes()
                firstOutputs[relativePath] = compressed
                self.assertEqual(gzip.decompress(compressed), content)
                self.assertEqual(compressed[3], 0)
                self.assertEqual(compressed[4:8], b"\x00\x00\x00\x00")
                self.assertEqual(compressed[8], 2)
                self.assertLess(len(compressed), len(content))
                os.utime(gzipPath, ns=(1_000_000_000, 1_000_000_000))

            self.assertEqual(syncGzipAssets(root), (0, 0))
            for relativePath in assets:
                gzipPath = root / (relativePath + ".gz")
                self.assertEqual(gzipPath.stat().st_mtime_ns, 1_000_000_000)

            for relativePath, expected in firstOutputs.items():
                gzipPath = root / (relativePath + ".gz")
                gzipPath.unlink()
                syncGzipAssets(root)
                self.assertEqual(gzipPath.read_bytes(), expected)

            changedSource = root / "modules/app.js"
            changedSource.write_bytes(changedSource.read_bytes() + b"changed\n" * 200)
            originalOutput = firstOutputs["modules/app.js"]
            syncGzipAssets(root)
            self.assertNotEqual((root / "modules/app.js.gz").read_bytes(), originalOutput)
            self.assertEqual(
                gzip.decompress((root / "modules/app.js.gz").read_bytes()),
                changedSource.read_bytes(),
            )

            orphanSource = root / "styles/app.css"
            orphanGzip = root / "styles/app.css.gz"
            orphanSource.unlink()
            self.assertTrue(orphanGzip.exists())
            self.assertEqual(syncGzipAssets(root), (0, 1))
            self.assertFalse(orphanGzip.exists())

    def testRegistersLittleFsPreAction(self):
        environment = FakeEnvironment()
        runpy.run_path(
            str(MODULE_PATH),
            init_globals={"Import": lambda name: None, "env": environment},
            run_name="platformio_gzip_web_assets",
        )
        self.assertEqual(len(environment.preActions), 1)
        target, callback = environment.preActions[0]
        self.assertEqual(target, "$BUILD_DIR/littlefs.bin")
        self.assertEqual(callback.__name__, "platformioSyncGzipAssets")

    def testSkipsCleanAndIntegrationTargets(self):
        for environment in (
            FakeEnvironment(clean=True),
            FakeEnvironment(integrationDump=True),
        ):
            runpy.run_path(
                str(MODULE_PATH),
                init_globals={"Import": lambda name: None, "env": environment},
                run_name="platformio_gzip_web_assets",
            )
            self.assertEqual(environment.preActions, [])


if __name__ == "__main__":
    unittest.main()
