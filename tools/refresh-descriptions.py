"""Refresh the English preview descriptions from a Flathub AppStream snapshot."""
import gzip
import json
import sys
import xml.etree.ElementTree as ET
from urllib.parse import urlparse
from pathlib import Path
from datetime import datetime, timezone

LANG = "{http://www.w3.org/XML/1998/namespace}lang"


def description(component):
    variants = component.findall("description")
    selected = next((item for item in variants if not item.get(LANG)), None)
    if selected is None:
        selected = next((item for item in variants if item.get(LANG, "").startswith("en")), None)
    if selected is None:
        raise ValueError("No base or English description: " + component.findtext("id", "unknown"))
    return "\n\n".join(" ".join("".join(item.itertext()).split()) for item in selected.iter() if item.tag in ("p", "li"))


def latest_release(component):
    def release_date(item):
        if item.get("date"):
            return item.get("date")
        if item.get("timestamp"):
            return datetime.fromtimestamp(int(item.get("timestamp")), timezone.utc).date().isoformat()
        return ""
    releases = [item for item in component.findall("releases/release") if item.get("type", "stable") == "stable"]
    if not releases:
        return {}
    release = max(releases, key=release_date)
    try:
        notes = description(release)
    except ValueError:
        notes = ""
    return {"version": release.get("version", ""), "date": release_date(release), "notes": notes}


def related_metadata(app_id, components):
    links = [{"type": item.get("type", "homepage"), "url": item.text.strip()}
             for item in components[app_id].findall("url")
             if item.text and urlparse(item.text.strip()).scheme in ("https", "http")]
    links.append({"type": "manifest", "url": "https://github.com/flathub/" + app_id.removesuffix(".desktop")})
    addons = {}
    for component in components.values():
        if any(item.text in (app_id, app_id + ".desktop") for item in component.findall("extends")):
            addon_id = component.findtext("id")
            name = next((item.text for item in component.findall("name") if not item.get(LANG)), addon_id)
            summary = next((item.text for item in component.findall("summary") if not item.get(LANG)), "")
            addons[addon_id] = {"id": addon_id, "name": name, "summary": summary}
    return links, sorted(addons.values(), key=lambda item: item["name"].casefold())


if __name__ == "__main__":
    # The localized variant may come first; it must not hide the base text.
    sample = ET.fromstring('<component><description xml:lang="ar"><p>Localized</p></description><description><p>English</p></description></component>')
    assert description(sample) == "English"
    assert description(ET.fromstring("<component><description><p>First</p><p>Second</p><p>Last</p></description></component>")).endswith("Last")
    releases = ET.fromstring('<component><releases><release version="2" date="2026-10-12"><description><p>Current notes</p></description></release><release version="1" date="2026-10-01"><description><p>Older</p></description></release></releases></component>')
    assert latest_release(releases) == {"version": "2", "date": "2026-10-12", "notes": "Current notes"}
    assert latest_release(ET.fromstring('<component><releases><release version="1" timestamp="0"/></releases></component>')) == {"version": "1", "date": "1970-01-01", "notes": ""}
    assert latest_release(ET.fromstring('<component/>')) == {}
    links, addons = related_metadata("test.App", {"test.App": ET.fromstring('<component><url type="help">https://example.org/help</url><url>javascript:bad</url></component>'), "test.Addon": ET.fromstring('<component><id>test.Addon</id><extends>test.App</extends><name>Add-on</name><summary>Extra tools</summary></component>')})
    assert len(links) == 2 and addons == [{"id": "test.Addon", "name": "Add-on", "summary": "Extra tools"}]
    if sys.argv[1:] == ["--check"]:
        print("Description language check: PASS")
        sys.exit(0)
    if len(sys.argv) != 3:
        sys.exit("Usage: refresh-descriptions.py appstream.xml.gz assets/details.json (or --check)")
    with gzip.open(sys.argv[1]) as source:
        components = {item.findtext("id"): item for item in ET.parse(source).getroot().findall("component")}
    target = Path(sys.argv[2])
    data = json.loads(target.read_text())
    for app_id in list(data):
        _, addons = related_metadata(app_id, components)
        for addon in addons:
            data.setdefault(addon["id"], {"description": addon["summary"]})
    for app_id, info in data.items():
        try:
            info["description"] = description(components[app_id])
        except ValueError:
            info.setdefault("description", "")
        info["release"] = latest_release(components[app_id])
        info["links"], info["addons"] = related_metadata(app_id, components)
        info["categories"] = [item.text for item in components[app_id].findall("categories/category") if item.text]
    target.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
