# Website

The project site, https://naysurgithub.github.io/SlopTube/, is published by GitHub Pages from
`.github/workflows/pages.yml` whenever `site/`, `docs/`, `tools/site/` or the logo change on
`main` (or by hand from the Actions tab). It is built from the repository, so nothing is written
twice:

| Part | Source |
| --- | --- |
| Landing page | `site/index.html`, `site/style.css` |
| Download button | `site/site.js` asks the GitHub API for the latest release and links its APK, with the version and size; without JavaScript, or when the API is unreachable, it opens the releases page |
| Logo, screenshots | `android/logo.svg`, `docs/screenshots/` |
| Documentation | Every page of `docs/`, rendered from its Markdown with `site/doc.html` as the frame; links between documents become links between pages |

## Building it locally

```sh
pip install markdown==3.7
python3 tools/site/build.py _site
python3 -m http.server --directory _site
```

The version of `markdown` is pinned in the workflow. The build replaces its output directory and
refuses to write into the repository itself or its sources.

The site needs no change when a release is published: the download button always finds the
newest one.
