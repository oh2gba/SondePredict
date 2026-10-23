# Releasing and keeping the pages up to date

Sonde Predict is described in several places. When a feature changes or a release ships, go
through this whole list, so no page tells an old story. Nothing is pushed, tagged, deployed or
published before John has tested the build and said so.

## In this repository

- `README.md`: what it does, downloads, wind data, licence and credits.
- `docs/manual/user-manual.md`, then the screenshots in `docs/manual/images/`
  (`SP_TEST_SCREENSHOTS=<dir> tst_settingsdialog screenshots`, `tst_helpwindow screenshot`,
  `SP_LIGHT=1` with `SP_SCREENSHOT` for the main window) and `docs/manual/user-manual.pdf`
  (written by `tst_helpwindow screenshot` into the same folder).
- `PLAN.md`: what was done and why.
- `tools/validation/README.md`: results whenever the defaults or wind sources change.
- `packaging/linux/io.github.sondepredict.SondePredict.metainfo.xml`: description and release notes.
- `VERSION`, and the version in `CMakeLists.txt`, only when releasing.
- `src/app/data/launch-sites.json`: refresh with `tools/sondehub_sites.py` before a release.

## Elsewhere

- **The update check's answer**: `static/sondepredict/version.json` in the oh2gba.eu project.
  On each release set `version` there and deploy that site, once the release's files are on
  GitHub.
- **The project's GitHub Pages site**, <https://oh2gba.github.io/SondePredict/>: `docs/index.html`
  with `docs/site/` (style, icon, images made from app screenshots; `docs/.nojekyll`). GitHub
  Pages serves the `docs/` folder of `main`, so the manual and its PDF are on the site too.
  Preview locally: `python3 -m http.server 8765 --directory docs`.
- **oh2gba.eu**: the blog post *Sonde Predict, Revived*, kept as a draft in
  `drafts/sonde-predict-revived/` of the site's own project (its README says how to publish it).
  Drafts must stay out of `content/posts/` and `static/`: every build of the site publishes
  whatever is there, whoever runs it. That project builds with `build.py`, checks with `test.py` and
  uploads everything in `dist/` with `deploy.sh`, so check for other unpublished posts there
  before deploying. Its credentials stay there and are never copied into this repository.

## Release steps

1. John tests the AppImage (and the Windows build) built locally.
2. Bump `VERSION`, update the metainfo release notes, commit.
3. Push and tag; check that the GitHub Actions builds pass and the release has its files.
4. Update the pages above that mention the version or new features, and the update check's
   answer last.
5. First time only: turn on GitHub Pages (branch `main`, folder `/docs`) and make the project
   page easy to find from GitHub: `gh repo edit oh2gba/SondePredict --homepage
   https://oh2gba.github.io/SondePredict/` puts it in the About box at the top right of the
   repository, next to the README's link at the very top; add topics too
   (`gh repo edit oh2gba/SondePredict --add-topic radiosonde --add-topic weather-balloon
   --add-topic ham-radio --add-topic sondehub --add-topic qt`).
