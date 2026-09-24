import json
import re
from html.parser import HTMLParser
from pathlib import Path
import subprocess
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "docs" / "custom-build"
TARGET = ROOT / "plugins" / "default-web-apps" / "assets" / "shared"


PAGES = {
    "plugins/default-web-apps/assets/index.html": "shared/theme",
    "plugins/default-web-apps/assets/Weigh_Save/weigh_save.html": "../shared/theme",
    "plugins/quality-control-assistant/assets/index.html": "/shared/theme",
    "plugins/default-web-apps/assets/dosing_assistant/dosing_assistant.html": "../shared/theme",
}


def cssDeclarations(source, selector):
    match = re.search(rf"(?m)^\s*{re.escape(selector)}\s*\{{([^{{}}]*)\}}", source)
    assert match, selector
    return {
        name: re.sub(r"\s+", " ", value.strip())
        for name, value in re.findall(r"([\w-]+)\s*:\s*([^;]+);", match.group(1))
    }


def toggleSvg(source):
    match = re.search(r'<svg aria-hidden="true" viewBox="0 0 68 32">.*?</svg>', source, re.S)
    assert match
    root = ElementTree.fromstring(match.group())

    def signature(node):
        return node.tag, tuple(sorted(node.attrib.items())), tuple(signature(child) for child in node)

    return signature(root)


class PageReferences(HTMLParser):
    def __init__(self):
        super().__init__()
        self.scripts = []
        self.stylesheets = []
        self.slots = 0
        self.brandMarks = 0
        self.meta = {}

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "script" and "src" in attributes:
            self.scripts.append(attributes["src"])
        if tag == "link" and attributes.get("rel") == "stylesheet":
            self.stylesheets.append(attributes["href"])
        if "data-hds-theme-toggle" in attributes:
            self.slots += 1
        if "brand-mark" in attributes.get("class", "").split():
            self.brandMarks += 1
        if tag == "meta" and "name" in attributes:
            self.meta[attributes["name"]] = attributes.get("content")


NODE_CHECK = r"""
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[1], 'utf8');

function launch(initial, darkSystem = false, storageFailure = false) {
  const values = new Map(Object.entries(initial));
  const listeners = {};
  const slot = {replaceChildren(button) { this.button = button; }};
  const root = {dataset: {}};
  const meta = {content: ''};
  const system = {matches: darkSystem, addEventListener(name, listener) { listeners.system = listener; }};
  const document = {
    documentElement: root,
    readyState: 'complete',
    querySelector() { return meta; },
    querySelectorAll() { return [slot]; },
    createElement() {
      return {
        attributes: {},
        setAttribute(name, value) { this.attributes[name] = value; },
        addEventListener(name, listener) { this.click = listener; }
      };
    }
  };
  const localStorage = {
    getItem(key) { if (storageFailure) throw Error('denied'); return values.get(key) ?? null; },
    setItem(key, value) { if (storageFailure) throw Error('denied'); values.set(key, value); }
  };
  const window = {addEventListener(name, listener) { listeners.storage = listener; }};
  vm.runInNewContext(source, {document, localStorage, matchMedia: () => system, window});
  return {values, listeners, slot, root, meta, system};
}

const key = 'hds-web-theme-v1';
const legacy = 'hds-custom-build-theme-v1';
const saved = preference => JSON.stringify({version: 1, preference});

const defaultPage = launch({});
assert.equal(defaultPage.root.dataset.themePreference, 'system');
assert.equal(defaultPage.root.dataset.theme, 'light');
assert.equal(defaultPage.meta.content, '#0d6b4f');
assert.equal(defaultPage.slot.button.attributes['aria-pressed'], 'false');
assert.match(defaultPage.slot.button.innerHTML, /theme-toggle-moon/);
defaultPage.system.matches = true;
defaultPage.listeners.system();
assert.equal(defaultPage.root.dataset.theme, 'dark');
assert.equal(defaultPage.meta.content, '#111413');
defaultPage.slot.button.click();
assert.equal(defaultPage.root.dataset.themePreference, 'light');
assert.equal(defaultPage.values.get(key), saved('light'));
defaultPage.system.matches = false;
defaultPage.listeners.system();
assert.equal(defaultPage.root.dataset.theme, 'light');

const migrated = launch({[legacy]: saved('dark')});
assert.equal(migrated.root.dataset.theme, 'dark');
assert.equal(migrated.values.get(key), saved('dark'));
assert.equal(migrated.values.get(legacy), saved('dark'));

const preferred = launch({[key]: saved('light'), [legacy]: saved('dark')}, true);
assert.equal(preferred.root.dataset.theme, 'light');
assert.equal(preferred.values.get(key), saved('light'));

const fallback = launch({[key]: 'bad json', [legacy]: saved('dark')});
assert.equal(fallback.root.dataset.theme, 'dark');
assert.equal(fallback.values.get(key), saved('dark'));

const blocked = launch({}, true, true);
assert.equal(blocked.root.dataset.theme, 'dark');
blocked.slot.button.click();
assert.equal(blocked.root.dataset.theme, 'light');
"""


def main():
    manifest = json.loads((ROOT / "plugins/default-web-apps/plugin.json").read_text(encoding="utf-8"))
    assets = {(asset["source"], asset["target"]) for asset in manifest["assets"]}
    for name in ("theme.css", "theme.js"):
        assert (f"assets/shared/{name}", f"shared/{name}") in assets
        assert (TARGET / name).is_file()

    referenceHtml = (REFERENCE / "index.html").read_text(encoding="utf-8")
    referenceCss = (REFERENCE / "styles.css").read_text(encoding="utf-8")
    referenceJs = (REFERENCE / "app.js").read_text(encoding="utf-8")
    themeCss = (TARGET / "theme.css").read_text(encoding="utf-8")
    themeJs = (TARGET / "theme.js").read_text(encoding="utf-8")
    for selector in (":root", 'html[data-theme="dark"]', ".brand-mark", ".brand-mark::after"):
        assert cssDeclarations(referenceCss, selector) == cssDeclarations(themeCss, selector), selector
    assert cssDeclarations(referenceCss, ".brand-mark::before")["background"] == "white"
    assert cssDeclarations(themeCss, ".brand-mark::before")["background"] == "var(--surface)"
    assert 'html[data-theme="dark"] .brand-mark::before { background: var(--surface); }' in referenceCss
    assert toggleSvg(referenceHtml) == toggleSvg(themeJs)
    assert 'id="theme-toggle"' in referenceHtml
    assert "hds-custom-build-theme-v1" in referenceHtml
    assert "hds-custom-build-theme-v1" in referenceJs
    assert "theme.css" not in referenceHtml and "theme.js" not in referenceHtml

    for path, prefix in PAGES.items():
        source = (ROOT / path).read_text(encoding="utf-8")
        references = PageReferences()
        references.feed(source)
        assert references.slots == 1, path
        assert references.brandMarks == int(path == "plugins/default-web-apps/assets/index.html"), path
        assert references.meta["color-scheme"] == "light dark", path
        assert references.meta["theme-color"] == "#0d6b4f", path
        assert f"{prefix}.js?v=1" in references.scripts, path
        assert f"{prefix}.css?v=1" in references.stylesheets, path
        assert "theme-toggle-track" not in source, path
        for extension in ("css", "js"):
            assetRoot = TARGET.parent if prefix.startswith("/") else (ROOT / path).parent
            assert (assetRoot / f"{prefix.lstrip('/')}.{extension}").is_file(), path

    dashboard = (ROOT / "plugins/default-web-apps/assets/index.html").read_text(encoding="utf-8")
    runtimeColors = re.findall(r"\.style\.color\s*=\s*(['\"])(.*?)\1", dashboard)
    assert {color for _, color in runtimeColors} <= {
        "var(--green-dark)", "var(--danger)", "var(--muted)"
    }

    appCss = (TARGET / "app.css").read_text(encoding="utf-8")
    assert "background: var(--canvas)" in appCss
    assert ".bg-purple-400 {\n  background-color: var(--green);" in appCss
    assert ".dosing-settings {" in appCss and ".assistant-form-grid {" in appCss
    webserver = (ROOT / "include/webserver.h").read_text(encoding="utf-8")
    inlinePage = webserver.split("HDS_WIFI_SETUP_PAGE[]", 1)[1].split(")html\";", 1)[0]
    assert "theme.css" not in inlinePage and "theme.js" not in inlinePage
    assert "`docs/AI_WEB_UI_NOTES.md`" in (ROOT / "docs/AI_REPO_MAP.md").read_text(encoding="utf-8")

    subprocess.run(["node", "-e", NODE_CHECK, str(TARGET / "theme.js")], cwd=ROOT, check=True)
    print("web theme contract tests passed")


if __name__ == "__main__":
    main()
