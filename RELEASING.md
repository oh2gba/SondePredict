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
- `src/app/data/launch-sites.json`: before a release, refresh the measured descents
  (`tools/validation/fit_descents.py tools/descent-fits.json`, about an hour, downloads SondeHub
  tracks) and then the station list (`tools/sondehub_sites.py`).

## Elsewhere

- **The update check's answer**: `static/sondepredict/version.json` in the oh2gba.eu project.
  On each release set `version` there and deploy that site, once the release's files are on
  GitHub.
- **The project's GitHub Pages site**, <https://oh2gba.github.io/SondePredict/> (live since 5.1.0):
  `docs/index.html` with `docs/site/` (style, icon, images made from app screenshots;
  `docs/.nojekyll`). GitHub Pages serves the `docs/` folder of `main`, so it updates with every
  push, and the manual and its PDF are on the site too. Preview locally:
  `python3 -m http.server 8765 --directory docs`. The repository's About box links to it.
- **oh2gba.eu**: the blog post *Sonde Predict, Revived* (published with 5.1.0;
  `content/posts/sonde-predict-revived.html` and `static/media/posts/sonde-predict-revived/` in
  the site's own project). For a release that changes what the post says, update it or write a
  new post. Unpublished drafts never go in `content/posts/` or `static/`: every build of the site
  publishes whatever is there, whoever runs it. That project builds with `build.py`, checks with
  `test.py` and uploads everything in `dist/` with `deploy.sh`, so check for other unpublished
  posts there before deploying. Its credentials stay there and are never copied into this
  repository.

## Release steps

1. John tests the AppImage (and the Windows build) built locally.
2. Bump the version in `CMakeLists.txt`, add the release to the metainfo, commit.
3. Push `main`, wait for the build workflow (Linux and Windows, with tests), then tag
   (`git tag -a vX.Y.Z`) and push the tag: the release workflow builds the downloads and creates
   the GitHub release with fixed file names. Add the release notes (`gh release edit vX.Y.Z
   --notes-file …`) and check the three `releases/latest/download/…` links.
4. Update the pages above that mention the version or new features, and the update check's
   answer last.
