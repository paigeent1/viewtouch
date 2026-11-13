# User manual

`manual.html` is the manual; the PDF, the screenshots and their zip are made from it and not kept in git.

```sh
# 1. Every picture the manual uses (a demo-filled store), into shots/ (from modern/, with a Qt build in build/)
bash tools/manual_shots.sh build
# 2. The PDF
cd docs/manual && soffice --headless --convert-to "pdf:writer_web_pdf_Export" manual.html
```

`tools/manual_shots.sh` runs the `[manual]` tests (m01-m84, already under the manual's names; OpenGL, under
`xvfb-run` when there is no screen), then every other test plus the hidden `[hostShots]` and `[phoneFloors]`, whose
pictures have their own names: its `MAP` table names them for the manual (`51-find-item.png` is `m100-find-item.png`),
`m99-settle-layouts` is the five `50-settle-*` layouts three across, and the `lu-*` pictures are copied from
`docs/label-utility/`. A picture no test here makes any more (m122, m130 bar/patio, m146-m148) is taken from upstream
ViewTouch's committed copy at a pinned commit. A new picture in the manual needs a line in `MAP` (or a `[manual]` shot
under its name). It ends by listing any picture still missing. The "Modern manual" workflow runs it and the PDF.

The PDF is published on the `modern-manual-<version>` GitHub release (linked from the main README). The README's screenshots, in `docs/screenshots/`, are small JPEGs made from these:

```sh
magick docs/manual/shots/m02-tables.png -resize 1200x -strip -quality 82 docs/screenshots/floor-plan.jpg
```

## Editions (rebuilding the manual as ViewTouch changes)

Every rebuild is an edition named by its date, **Ver. YYMMDD**, with the program build it describes on the cover
("Effective with paigeent1 ver 0.9.NNN", NNN = `git rev-list --count HEAD` of the commit it describes, the number
ViewTouch shows). Editions are never overwritten: each PDF is kept as
`editions/ViewTouch User Manual Ver YYMMDD.pdf` here and in Dropbox > POS project > Manuals.

Last rebuilt: **Ver. 261110** (text 2026-11-10; the store's pictures are in as of 2026-11-13, effective with ver 0.9.511), from the development notes through 2026-11-09 18:24 ET (commit `f3b7d43`, ver 0.9.505). Pictures of this store (from the 2026-11-13 `vt-manual-shots` run and phone photos, names hidden) are kept in git in `img/`, as small PNG/JPEG files; the demo pictures in `shots/` are still made by the test run above. Marks still open: see the `<!-- SHOT: ... -->` lines.
The next rebuild starts from the notes after that point.

Steps:
1. Inventory: read every development-notes entry since "Last rebuilt" against `manual.html`; list what users need
   (new, changed, now wrong), with on-screen names checked in the source. Leave out internal changes, anything legal
   and any camera/video work.
2. Update the chapters and add new ones; keep the style (short sentences, `.tip`, `.warn`); "subscription", never
   "member". New pictures are marked `<!-- SHOT: ... -->`.
3. Pictures of THIS store (not the demo restaurant): on the store's Pi run `vt-manual-shots` (a scrubbed copy of the
   store, every page, to Google Drive "POS project tmp/manual-shots/<date>/"); fit them in place of the SHOT marks.
4. Cover: Ver. YYMMDD and "Effective with paigeent1 ver 0.9.NNN"; update "Last rebuilt" above.
5. PDF: `soffice --headless --convert-to "pdf:writer_web_pdf_Export" manual.html`, check it, save it as the edition.
