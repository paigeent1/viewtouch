# User manual

`manual.html` is the manual; the PDF, the screenshots and their zip are made from it and not kept in git.

```sh
# 1. Screenshots of every screen (a demo-filled store), into shots/
VTM_SHOTS=$PWD/docs/manual/shots QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl build/tests/vtm_tests "[manual]"
VTM_SHOTS=/tmp/extra QT_QPA_PLATFORM=offscreen build/tests/vtm_tests "[display][ui]"   # customer display (m70-m73)
# 2. The PDF
cd docs/manual && soffice --headless --convert-to "pdf:writer_web_pdf_Export" manual.html
```

The PDF is published on the `modern-manual-<version>` GitHub release (linked from the main README). The README's screenshots, in `docs/screenshots/`, are small JPEGs made from these:

```sh
magick docs/manual/shots/m02-tables.png -resize 1200x -strip -quality 82 docs/screenshots/floor-plan.jpg
```

## Editions (rebuilding the manual as ViewTouch changes)

Every rebuild is an edition named by its date, **Ver. YYMMDD**, with the program build it describes on the cover
("Effective with paigeent1 ver 0.9.NNN", NNN = `git rev-list --count HEAD` of the commit it describes, the number
ViewTouch shows). Editions are never overwritten: each PDF is kept as
`editions/ViewTouch User Manual Ver YYMMDD.pdf` here and in Dropbox > POS project > Manuals.

Last rebuilt: **Ver. 261110** (text; pictures and PDF still to come), from the development notes through 2026-11-09 18:24 ET (commit `f3b7d43`, ver 0.9.505).
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
