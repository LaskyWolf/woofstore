#!/usr/bin/env python3
"""Draft one AppStream <component> for an AUR package (developer tool, never run by the app).

The output is a draft for a human to review and paste into metadata/xml/woofstore.xml.
Image URLs found upstream are listed only as XML comments: the tool never invents images
and never puts an unverified image into a <screenshot> element.
"""
import argparse
import json
import os
import re
import sys
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

USER_AGENT = "WoofStore-metadata-drafter"
TIMEOUT = 20
TEXT_CAP = 20_000  # characters of upstream text sent to the model
DOWNLOAD_CAP = 2_000_000  # bytes; protects against huge pages
NAME_RE = re.compile(r"^[a-zA-Z0-9][a-zA-Z0-9@._+\-]*$")
MODEL = "claude-opus-5-5"

# freedesktop.org Desktop Menu Specification: main + additional categories.
CATEGORIES = set("""
AudioVideo Audio Video Development Education Game Graphics Network Office Science Settings System Utility
Building Debugger IDE GUIDesigner Profiling RevisionControl Translation Calendar ContactManagement Database
Dictionary Chart Email Finance FlowChart PDA ProjectManagement Presentation Spreadsheet WordProcessor
2DGraphics VectorGraphics RasterGraphics 3DGraphics Scanning OCR Photography Publishing Viewer TextTools
DesktopSettings HardwareSettings Printing PackageManager Dialup InstantMessaging Chat IRCClient Feed
FileTransfer HamRadio News P2P RemoteAccess Telephony TelephonyTools VideoConference WebBrowser
WebDevelopment Midi Mixer Sequencer Tuner TV AudioVideoEditing Player Recorder DiscBurning ActionGame
AdventureGame ArcadeGame BoardGame BlocksGame CardGame KidsGame LogicGame RolePlaying Shooter Simulation
SportsGame StrategyGame Art Construction Music Languages ArtificialIntelligence Astronomy Biology Chemistry
ComputerScience DataVisualization Economy Electricity Geography Geology Geoscience History Humanities
ImageProcessing Literature Maps Math NumericalAnalysis MedicalSoftware Physics Robotics Spirituality Sports
ParallelComputing Amusement Archiving Compression Electronics Emulator Engineering FileTools FileManager
TerminalEmulator Filesystem Monitor Security Accessibility Calculator Clock TextEditor Documentation Adult
Core KDE GNOME XFCE DDE GTK Qt Motif Java ConsoleOnly
""".split())


def fetch(url, headers=None):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        return response.read(DOWNLOAD_CAP).decode("utf-8", errors="replace")


def aur_info(name):
    url = "https://aur.archlinux.org/rpc/v5/info?arg[]=" + urllib.parse.quote(name)
    results = json.loads(fetch(url)).get("results", [])
    if not results:
        sys.exit(f"error: AUR package '{name}' not found")
    return results[0], url


def readme_location(homepage):
    """Return (readme_url, extra_headers, base_for_relative_images) for known forges, else None."""
    parts = urllib.parse.urlparse(homepage)
    path = [p for p in parts.path.split("/") if p]
    if len(path) < 2:
        return None
    owner, repo = path[0], path[1].removesuffix(".git")
    if parts.netloc == "github.com":
        return (f"https://api.github.com/repos/{owner}/{repo}/readme",
                {"Accept": "application/vnd.github.raw"},
                f"https://raw.githubusercontent.com/{owner}/{repo}/HEAD/")
    if parts.netloc == "gitlab.com":
        base = f"https://gitlab.com/{owner}/{repo}/-/raw/HEAD/"
        return base + "README.md", {}, base
    if parts.netloc == "codeberg.org":
        base = f"https://codeberg.org/api/v1/repos/{owner}/{repo}/raw/"
        return base + "README.md", {}, base
    return None


class TextExtractor(HTMLParser):
    """Collect visible text, skipping script/style, and keep <img src> values."""

    def __init__(self):
        super().__init__()
        self.chunks, self.skip = [], 0

    def handle_starttag(self, tag, attrs):
        self.skip += tag in ("script", "style", "noscript")
        if tag == "img" and dict(attrs).get("src"):
            self.chunks.append(f'<img src="{dict(attrs)["src"]}">')  # kept so image search still sees it

    def handle_endtag(self, tag):
        self.skip -= tag in ("script", "style", "noscript") and self.skip > 0

    def handle_data(self, data):
        if not self.skip and data.strip():
            self.chunks.append(data.strip())


def upstream_text(homepage):
    """Return (text, image_base, source_url). Falls back from forge README to homepage HTML.

    A forge image_base ends with "/" and is the repo root; a homepage base is the page URL."""
    location = readme_location(homepage)
    if location:
        url, headers, base = location
        try:
            return fetch(url, headers)[:TEXT_CAP], base, url
        except OSError as error:
            print(f"warning: README fetch failed ({error}), using homepage", file=sys.stderr)
    parser = TextExtractor()
    parser.feed(fetch(homepage))
    return "\n".join(parser.chunks)[:TEXT_CAP], homepage, homepage


def candidate_images(text, base, limit=6):
    found = re.findall(r"!\[[^\]]*\]\(\s*<?([^)\s>]+)", text)
    found += re.findall(r"<img[^>]+src=[\"']([^\"']+)", text, re.IGNORECASE)
    images = []
    for raw in found:
        # In a README, "/assets/x.png" means the repo root, not the host root.
        repo_root = raw.startswith("/") and not raw.startswith("//") and base.endswith("/")
        url = base + raw[1:] if repo_root else urllib.parse.urljoin(base, raw)
        path = urllib.parse.urlparse(url).path.lower()
        # Badges and SVG logos are never screenshots.
        if not url.startswith(("https://", "http://")) or "shields.io" in url or "badge" in url.lower() or path.endswith(".svg"):
            continue
        if url not in images:
            images.append(url)
    return images[:limit]


def ai_draft(info, source_text):
    try:
        import anthropic
    except ImportError:
        sys.exit("error: the 'anthropic' package is missing. Install it with: python -m pip install --user anthropic")
    if not os.environ.get("ANTHROPIC_API_KEY"):
        sys.exit("error: ANTHROPIC_API_KEY is not set. Export it, or run with --no-ai for an AUR-only draft.")
    schema = {
        "type": "object",
        "properties": {
            "name": {"type": "string"},
            "summary": {"type": "string"},
            "description": {"type": "array", "items": {"type": "string"}},
            "categories": {"type": "array", "items": {"type": "string", "enum": sorted(CATEGORIES)}},
            "keywords": {"type": "array", "items": {"type": "string"}},
        },
        "required": ["name", "summary", "description", "categories", "keywords"],
        "additionalProperties": False,
    }
    prompt = (
        "Write AppStream metadata for a Linux app store listing. Use ONLY the source material below; "
        "do not invent features, platforms or claims it does not state. "
        "name: the display name. summary: one line, under 80 characters, no trailing period. "
        "description: 1-3 short plain-text paragraphs for end users (no markdown, no install instructions). "
        "categories: freedesktop.org categories that clearly apply. keywords: up to 8 search terms.\n\n"
        f"<aur_description>{info.get('Description') or ''}</aur_description>\n"
        f"<upstream_text>\n{source_text}\n</upstream_text>"
    )
    client = anthropic.Anthropic()
    response = client.beta.messages.create(
        model=MODEL,
        max_tokens=16000,
        betas=["server-side-fallback-2026-07-01"],
        fallbacks="default",  # a safety decline is retried server-side on a recommended model
        output_config={"effort": "low", "format": {"type": "json_schema", "schema": schema}},
        messages=[{"role": "user", "content": prompt}],
    )
    if response.stop_reason == "refusal":
        sys.exit("error: the model declined this request; use --no-ai")
    data = json.loads(next(block.text for block in response.content if block.type == "text"))
    data["categories"] = [c for c in data["categories"] if c in CATEGORIES]  # schema enum, checked again
    data["keywords"] = data["keywords"][:8]
    return data


def plain_draft(info):
    description = info.get("Description") or ""
    return {"name": info["Name"], "summary": description, "description": [description] if description else [],
            "categories": [], "keywords": (info.get("Keywords") or [])[:8]}


def component_xml(info, draft):
    component = ET.Element("component", type="desktop-application")
    ET.SubElement(component, "id").text = "aur." + info["Name"]
    ET.SubElement(component, "pkgname").text = info["Name"]
    ET.SubElement(component, "name").text = draft["name"]
    ET.SubElement(component, "summary").text = draft["summary"].strip().rstrip(".")
    if draft["description"]:
        description = ET.SubElement(component, "description")
        for paragraph in draft["description"]:
            ET.SubElement(description, "p").text = paragraph.strip()
    if draft["categories"]:
        categories = ET.SubElement(component, "categories")
        for category in draft["categories"]:
            ET.SubElement(categories, "category").text = category
    if draft["keywords"]:
        keywords = ET.SubElement(component, "keywords")
        for keyword in draft["keywords"]:
            ET.SubElement(keywords, "keyword").text = keyword
    if info.get("URL"):
        ET.SubElement(component, "url", type="homepage").text = info["URL"]
    ET.indent(component)
    return ET.tostring(component, encoding="unicode")


def comment(text):
    return "<!-- " + text.replace("--", "- -") + " -->"  # "--" is illegal inside XML comments


def render(info, draft, sources, images, used_ai):
    header = comment("AI draft: review every line before committing. Source: " + " ".join(sources)) if used_ai \
        else comment("Draft from AUR data only")
    lines = [header]
    if images:
        lines.append(comment("Candidate screenshots from upstream (verify, then add <screenshots> by hand):"))
        lines += [comment(url) for url in images]
    return "\n".join(lines + [component_xml(info, draft)]) + "\n"


def self_check():
    readme = "![ci](https://img.shields.io/x.png) ![shot](docs/shot.png) <img src='https://a.org/logo.svg'> <img src=\"/b.jpg\">"
    assert candidate_images(readme, "https://raw.githubusercontent.com/o/r/HEAD/") == [
        "https://raw.githubusercontent.com/o/r/HEAD/docs/shot.png", "https://raw.githubusercontent.com/o/r/HEAD/b.jpg"]
    info = {"Name": "demo", "Description": "A <tool> & more.", "URL": "https://x.org/?a=1&b=2"}
    out = render(info, plain_draft(info), [], ["https://x.org/a--b.png"], used_ai=False)
    root = ET.fromstring("<components>" + out + "</components>")
    assert root.find("component/summary").text == "A <tool> & more"
    assert not NAME_RE.match("../etc") and NAME_RE.match("python-foo@1.0+git")
    print("draft-metadata self-check: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", nargs="?", help="AUR package name, e.g. paru")
    parser.add_argument("--no-ai", action="store_true", help="skip Claude; draft from AUR data only")
    parser.add_argument("--out", help="write the XML to this file instead of stdout")
    parser.add_argument("--check", action="store_true", help="run the offline self-check and exit")
    args = parser.parse_args()
    if args.check:
        return self_check()
    if not args.package or not NAME_RE.match(args.package):
        parser.error("a valid AUR package name is required")
    info, aur_url = aur_info(args.package)
    sources, images, text = [aur_url], [], ""
    if info.get("URL"):
        try:
            text, base, source_url = upstream_text(info["URL"])
            sources.append(source_url)
            images = candidate_images(text, base)
        except OSError as error:
            print(f"warning: upstream fetch failed ({error}); using AUR data only", file=sys.stderr)
    draft = plain_draft(info) if args.no_ai else ai_draft(info, text)
    xml = render(info, draft, sources, images, used_ai=not args.no_ai)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(xml)
    else:
        sys.stdout.write(xml)


if __name__ == "__main__":
    main()
