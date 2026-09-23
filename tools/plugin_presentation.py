import hashlib
from pathlib import Path
import shutil


IMAGE_FORMATS = {".png": "PNG", ".jpg": "JPEG", ".jpeg": "JPEG", ".webp": "WEBP"}
PRESENTATION_KEYS = {"image", "image_alt", "handbook"}
MAX_IMAGE_BYTES = 500 * 1024
MAX_IMAGE_EDGE = 4096
MAX_IMAGE_PIXELS = 16_000_000
MAX_HANDBOOK_BYTES = 1024 * 1024


def pluginFile(pluginDir, value, name, safeRelativePath):
    relative = safeRelativePath(value, name)
    source = pluginDir.joinpath(*relative.parts).resolve()
    if pluginDir.resolve() not in source.parents or not source.is_file():
        raise ValueError(f"missing plugin presentation file: {name}")
    return relative, source


def previewMetadata(pluginId, pluginDir, presentation, safeRelativePath):
    relative, source = pluginFile(
        pluginDir, presentation["image"], f"{pluginId}.presentation.image", safeRelativePath
    )
    expectedFormat = IMAGE_FORMATS.get(relative.suffix.lower())
    if expectedFormat is None or source.stat().st_size > MAX_IMAGE_BYTES:
        raise ValueError(f"invalid plugin preview: {pluginId}")
    alt = presentation.get("image_alt")
    if not isinstance(alt, str) or not alt.strip() or len(alt) > 240:
        raise ValueError(f"invalid plugin preview alt text: {pluginId}")
    try:
        from PIL import Image

        with Image.open(source) as picture:
            width, height = picture.size
            if picture.format != expectedFormat or width > MAX_IMAGE_EDGE or height > MAX_IMAGE_EDGE or width * height > MAX_IMAGE_PIXELS:
                raise ValueError(f"invalid plugin preview dimensions or format: {pluginId}")
            picture.verify()
    except (OSError, SyntaxError) as error:
        raise ValueError(f"invalid plugin preview content: {pluginId}") from error
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    published = Path("plugin-media") / pluginId / f"{digest}{relative.suffix.lower()}"
    return {"image": published.as_posix(), "image_alt": alt}, source, published


def presentationMetadata(pluginId, manifest, pluginDir, safeRelativePath):
    presentation = manifest.get("presentation")
    if presentation is None:
        return None, []
    if not isinstance(presentation, dict) or not presentation or set(presentation) - PRESENTATION_KEYS:
        raise ValueError(f"invalid plugin presentation: {pluginId}")
    if "image_alt" in presentation and "image" not in presentation:
        raise ValueError(f"preview alt text has no image: {pluginId}")
    metadata = {}
    files = []
    if "image" in presentation:
        imageMetadata, source, published = previewMetadata(
            pluginId, pluginDir, presentation, safeRelativePath
        )
        metadata.update(imageMetadata)
        files.append((source, published))
    if "handbook" in presentation:
        relative, source = pluginFile(
            pluginDir, presentation["handbook"], f"{pluginId}.presentation.handbook", safeRelativePath
        )
        if relative.suffix.lower() != ".md" or source.stat().st_size > MAX_HANDBOOK_BYTES:
            raise ValueError(f"invalid plugin handbook: {pluginId}")
        contents = source.read_bytes()
        try:
            contents.decode("utf-8")
        except UnicodeError as error:
            raise ValueError(f"invalid plugin handbook text: {pluginId}") from error
        digest = hashlib.sha256(contents).hexdigest()
        published = Path("plugin-media") / pluginId / f"{digest}.md"
        metadata["handbook"] = published.as_posix()
        files.append((source, published))
    return metadata, files


def publishPresentationFiles(files, outputDir):
    expected = {outputDir / relative.relative_to("plugin-media"): source for source, relative in files}
    for target, source in expected.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != source.read_bytes():
            shutil.copy2(source, target)
    if not outputDir.is_dir():
        return
    for path in sorted(outputDir.rglob("*"), reverse=True):
        if path.is_file() and path not in expected:
            path.unlink()
        elif path.is_dir() and not any(path.iterdir()):
            path.rmdir()
