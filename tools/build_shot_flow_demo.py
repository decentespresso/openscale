from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "plugins" / "default-web-apps" / "assets"
DEMO = ROOT / "tools" / "shot_flow_demo"


def build(output, overlay=False):
    output = Path(output)
    shot_output = output / "shot-flow"
    shared_output = output / "shared"
    if not overlay:
        if output.exists():
            shutil.rmtree(output)
        shutil.copytree(ROOT / "docs", output, ignore=shutil.ignore_patterns("shot-flow-demo"))
    else:
        output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SOURCE / "shot_flow", shot_output)
    shared_output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SOURCE / "shared" / "reconnecting-websocket.js", shared_output)
    shutil.copy2(DEMO / "demo-websocket.js", shot_output)

    html_path = shot_output / "shot_flow.html"
    html = html_path.read_text(encoding="utf-8")
    marker = '<script src="../shared/reconnecting-websocket.js"></script>'
    if html.count(marker) != 1:
        raise ValueError("Shot Flow reconnecting WebSocket script marker changed")
    html_path.write_text(
        html.replace(marker, '<script src="demo-websocket.js"></script>' + marker),
        encoding="utf-8",
    )
    return output


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--overlay"):
        raise SystemExit("usage: build_shot_flow_demo.py OUTPUT_DIRECTORY [--overlay]")
    build(sys.argv[1], overlay="--overlay" in sys.argv)