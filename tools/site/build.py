#!/usr/bin/env python3
"""Builds the GitHub Pages site into a directory: the landing page of site/, the logo, the
screenshots, and every page of docs/ rendered from its Markdown, so the documentation is written
once, in docs/.

Usage: python3 tools/site/build.py <output directory>
Requires the `markdown` package (pinned in .github/workflows/pages.yml).
"""

import html
import re
import shutil
import sys
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "site"
DOCS = ROOT / "docs"
REPOSITORY = "https://github.com/NaySurGithub/SlopTube"
# The documentation's own index; it becomes docs/index.html.
DOCS_INDEX = "README.md"
# Links between documents point at .md files; on the site they are the rendered pages.
MD_LINK = re.compile(r'href="([^":#?]+)\.md(#[^"]*)?"')


def page_name(markdown_name: str) -> str:
    return "index" if markdown_name == DOCS_INDEX else markdown_name.removesuffix(".md")


def title_of(text: str, fallback: str) -> str:
    match = re.search(r"^# (.+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else fallback


def navigation(pages: list[tuple[str, str]], current: str) -> str:
    """Links to every document, in the order the documentation index lists them."""
    items = []
    for name, title in pages:
        active = ' aria-current="page"' if name == current else ""
        items.append(f'<li><a href="{name}.html"{active}>{html.escape(title)}</a></li>')
    return "\n".join(items)


def ordered_documents() -> list[Path]:
    index = (DOCS / DOCS_INDEX).read_text(encoding="utf-8")
    listed = re.findall(r"\]\(([\w-]+\.md)\)", index)
    files = [DOCS / DOCS_INDEX] + [DOCS / name for name in dict.fromkeys(listed)]
    # Anything not listed still gets a page, after the listed ones.
    files += sorted(path for path in DOCS.glob("*.md") if path not in files)
    return [path for path in files if path.exists()]


def render_docs(output: Path) -> None:
    template = (SITE / "doc.html").read_text(encoding="utf-8")
    documents = ordered_documents()
    sources = {path: path.read_text(encoding="utf-8") for path in documents}
    pages = [(page_name(path.name), title_of(text, path.stem)) for path, text in sources.items()]
    target = output / "docs"
    target.mkdir(parents=True, exist_ok=True)
    for path, text in sources.items():
        name = page_name(path.name)
        body = markdown.markdown(
            text,
            extensions=["tables", "fenced_code", "toc", "sane_lists"],
            extension_configs={"toc": {"permalink": "#", "permalink_title": "Link here"}},
        )
        body = MD_LINK.sub(
            lambda match: f'href="{page_name(match.group(1) + ".md")}.html{match.group(2) or ""}"',
            body,
        )
        page = (
            template.replace("{{title}}", html.escape(title_of(text, path.stem)))
            .replace("{{navigation}}", navigation(pages, name))
            .replace("{{source}}", f"{REPOSITORY}/blob/main/docs/{path.name}")
            .replace("{{content}}", body)
        )
        (target / f"{name}.html").write_text(page, encoding="utf-8")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = Path(sys.argv[1]).resolve()
    # The output is replaced whole: never the repository, its sources, or a folder holding them.
    if output in (ROOT, SITE, DOCS) or output in ROOT.parents:
        sys.exit(f"refusing to replace {output}: pick a build directory such as _site")
    shutil.rmtree(output, ignore_errors=True)
    shutil.copytree(SITE, output, ignore=shutil.ignore_patterns("doc.html"))
    shutil.copy(ROOT / "android" / "logo.svg", output / "logo.svg")
    shutil.copytree(DOCS / "screenshots", output / "screenshots")
    render_docs(output)
    # Served as plain files: no Jekyll processing on GitHub's side.
    (output / ".nojekyll").touch()
    print(f"site built in {output}")


if __name__ == "__main__":
    main()
