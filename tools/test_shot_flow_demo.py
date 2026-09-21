import json
from pathlib import Path
import tempfile

import build_shot_flow_demo


ROOT = Path(__file__).resolve().parents[1]


def main():
    plugin = json.loads(
        (ROOT / "plugins/default-web-apps/plugin.json").read_text(encoding="utf-8")
    )
    sources = {asset["source"] for asset in plugin["assets"]}
    production_html = (
        ROOT / "plugins/default-web-apps/assets/shot_flow/shot_flow.html"
    ).read_text(encoding="utf-8")
    assert not any("demo" in source.lower() for source in sources)
    assert "demo-websocket.js" not in production_html
    workflow = (ROOT / ".github/workflows/pages.yml").read_text(encoding="utf-8")
    assert "actions/jekyll-build-pages@v1" in workflow
    assert "python tools/build_shot_flow_demo.py _pages_source" in workflow
    assert "source: ./_pages_source" in workflow
    assert "test ! -d _site/plugins" in workflow
    assert "_site --overlay" not in workflow
    assert '"tools/shot_flow_demo/**"' in workflow

    with tempfile.TemporaryDirectory() as directory:
        output = build_shot_flow_demo.build(Path(directory) / "site")
        demo_html = (output / "shot-flow/shot_flow.html").read_text(encoding="utf-8")
        assert demo_html.count('src="demo-websocket.js"') == 1
        assert demo_html.index('src="demo-websocket.js"') < demo_html.index(
            'src="../shared/reconnecting-websocket.js"'
        )
        assert (output / "shot-flow/demo-websocket.js").is_file()
        assert (output / "shot-flow/main.js").is_file()
        assert (output / "shared/reconnecting-websocket.js").is_file()
        assert not (output / "shot-flow-demo").exists()

        overlay = Path(directory) / "overlay"
        overlay.mkdir()
        marker = overlay / "jekyll-output.txt"
        marker.write_text("preserved", encoding="utf-8")
        build_shot_flow_demo.build(overlay, overlay=True)
        assert marker.read_text(encoding="utf-8") == "preserved"
        assert (overlay / "shot-flow/demo-websocket.js").is_file()

    print("Shot Flow demo isolation tests passed")


if __name__ == "__main__":
    main()