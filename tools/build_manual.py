"""Build docs/Voice-Buddy-Manual.pdf from docs/MANUAL.md.

  python tools/build_manual.py

Converts the Markdown to a print-styled HTML page (screenshots from docs/screens)
and prints it to PDF with Microsoft Edge in headless mode. Needs: pip install markdown
"""
import os, subprocess, sys
import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "MANUAL.md")
HTML = os.path.join(ROOT, "docs", "manual.print.html")
PDF = os.path.join(ROOT, "docs", "Voice-Buddy-Manual.pdf")
EDGE = [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"]

CSS = """
@page { size: Letter; margin: 16mm 15mm 18mm; }
* { box-sizing: border-box; }
body { font-family: "Segoe UI", system-ui, sans-serif; font-size: 10.5pt; line-height: 1.5; color: #1d2330; }
h1 { font-size: 26pt; margin: 0 0 2pt; color: #2f6f8f; }
.byline { color: #5d6677; margin: 0 0 14pt; }
h2 { font-size: 15pt; color: #2f6f8f; border-bottom: 1.5pt solid #dcebf2; padding-bottom: 3pt;
     margin: 20pt 0 8pt; break-after: avoid; }
h2 { break-before: auto; }
p, li { orphans: 3; widows: 3; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0 10pt; font-size: 9.5pt; break-inside: auto; }
th { background: #dcebf2; text-align: left; }
th, td { border: 0.6pt solid #c9d3de; padding: 4pt 6pt; vertical-align: top; }
tr { break-inside: avoid; }
code { font-family: Consolas, monospace; font-size: 9pt; background: #eef1f5; padding: 0 2pt; border-radius: 2pt; }
.shots { display: flex; flex-wrap: wrap; gap: 8pt; margin: 8pt 0 10pt; }
.shots figure { margin: 0; break-inside: avoid; }
.shots img { display: block; border: 3pt solid #20242c; border-radius: 6pt; }
.shots img.device { width: 230pt; }
.shots img.phone { width: 150pt; border-width: 1pt; border-color: #c9d3de; }
.shots img.laptop { width: 100%; max-width: 480pt; border-width: 1pt; border-color: #c9d3de; }
.shots figcaption { font-size: 8.5pt; color: #5d6677; margin-top: 2pt; }
"""

def main():
    md = open(SRC, encoding="utf-8").read()
    body = markdown.markdown(md, extensions=["tables"])
    # group consecutive screenshots (<p><img ...></p>) into a flex row with captions
    import re
    def fig(m):
        alt, src = m.group(2), m.group(1)
        kind = "laptop" if "web-history" in src or "web-device" in src else ("phone" if "web-" in src else "device")
        return f'<figure><img class="{kind}" src="{src}" alt="{alt}"><figcaption>{alt}</figcaption></figure>'
    body = re.sub(r'<p><img alt="([^"]*)" src="([^"]*)" ?/?></p>', lambda m: fig(type("M", (), {"group": lambda s, i: (None, m.group(2), m.group(1))[i]})()), body)
    body = re.sub(r'((?:<figure>.*?</figure>\s*)+)', lambda m: f'<div class="shots">{m.group(1)}</div>', body, flags=re.S)
    # first paragraph after the title is the byline
    body = body.replace("</h1>\n<p>", '</h1>\n<p class="byline">', 1)
    html = f'<!doctype html><html><head><meta charset="utf-8"><title>Voice Buddy Parent Manual</title><style>{CSS}</style></head><body>{body}</body></html>'
    open(HTML, "w", encoding="utf-8").write(html)

    edge = next((p for p in EDGE if os.path.exists(p)), None)
    if not edge:
        sys.exit("Microsoft Edge not found; open docs/manual.print.html and print to PDF instead.")
    url = "file:///" + HTML.replace("\\", "/")
    subprocess.run([edge, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={PDF}", url], check=True, capture_output=True, timeout=120)
    os.remove(HTML)
    print(f"wrote {PDF} ({os.path.getsize(PDF) // 1024} KB)")

if __name__ == "__main__":
    main()
