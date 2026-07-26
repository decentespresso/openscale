import gzip
import io
from pathlib import Path


COMPRESSIBLE_SUFFIXES = frozenset({".html", ".js", ".css", ".svg"})


def gzipBytes(content):
    output = io.BytesIO()
    with gzip.GzipFile(
        filename="",
        mode="wb",
        compresslevel=9,
        fileobj=output,
        mtime=0,
    ) as archive:
        archive.write(content)
    return output.getvalue()


def syncGzipAssets(dataDir):
    root = Path(dataDir)
    if not root.is_dir():
        raise FileNotFoundError(root)

    written = 0
    removed = 0
    for sourcePath in sorted(root.rglob("*")):
        if not sourcePath.is_file() or sourcePath.suffix.lower() not in COMPRESSIBLE_SUFFIXES:
            continue
        gzipPath = sourcePath.with_name(sourcePath.name + ".gz")
        compressed = gzipBytes(sourcePath.read_bytes())
        if len(compressed) >= sourcePath.stat().st_size:
            if gzipPath.exists():
                gzipPath.unlink()
                removed += 1
            continue
        if gzipPath.exists() and gzipPath.read_bytes() == compressed:
            continue
        temporaryPath = gzipPath.with_name("." + gzipPath.name + ".tmp")
        temporaryPath.write_bytes(compressed)
        temporaryPath.replace(gzipPath)
        written += 1

    for gzipPath in sorted(root.rglob("*.gz")):
        sourcePath = gzipPath.with_suffix("")
        if sourcePath.suffix.lower() in COMPRESSIBLE_SUFFIXES and not sourcePath.is_file():
            gzipPath.unlink()
            removed += 1

    return written, removed


def platformioSyncGzipAssets(source, target, env):
    written, removed = syncGzipAssets(env.subst("$PROJECT_DATA_DIR"))
    print("[gzip-assets] wrote={}, removed={}".format(written, removed))


try:
    Import("env")
except NameError:
    pass
else:
    if not env.IsCleanTarget() and not env.IsIntegrationDump():
        env.AddPreAction("$BUILD_DIR/littlefs.bin", platformioSyncGzipAssets)


if __name__ == "__main__":
    projectDir = Path(__file__).resolve().parent
    written, removed = syncGzipAssets(projectDir / "web_apps")
    print("[gzip-assets] wrote={}, removed={}".format(written, removed))
