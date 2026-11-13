#!/usr/bin/env bash
# Makes every picture the user manual (modern/docs/manual/manual.html) uses into modern/docs/manual/shots/.
# Used by the "Modern manual" GitHub workflow; anyone with a Qt build of ViewTouch can run the same:
#
#   bash modern/tools/manual_shots.sh [build-dir]      # build-dir: default modern/build (or $BUILD_DIR)
#
# 1. "[manual]" tests (a demo-filled store): the m01-m84 pictures, already under the manual's names.
#    OpenGL is needed: on a machine with no screen it runs under xvfb-run (Mesa) when that is installed.
# 2. Every other test, with the hidden "[hostShots]" and "[phoneFloors]": the UI tests save their own pictures
#    under their test names ("51-find-item.png"); MAP below gives each its manual name ("m100-find-item.jpg").
# 3. The Label Utility pictures (lu-*) come from modern/docs/label-utility/ (committed).
# 4. A picture no test here makes any more is taken from upstream ViewTouch's committed copy (UPSTREAM_COMMIT),
#    the picture the manual was written with. MANUAL_SHOTS_UPSTREAM=0 turns that off.
# Ends by listing any <img> of the manual still missing (exit 1 if any).
# Options: MANUAL_SHOTS_RUN=0 skips the tests (only names/copies what a previous run left in $RAW).
set -u
MODERN="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${BUILD_DIR:-$MODERN/build}}"
case "$BUILD" in /*) ;; *) BUILD="$PWD/$BUILD" ;; esac
TESTS="$BUILD/tests/vtm_tests"
MANUAL="$MODERN/docs/manual"
OUT="$MANUAL/shots"
RAW="${RAW:-$BUILD/manual-test-shots}"   # the tests' own pictures, before naming
UPSTREAM_REPO=https://github.com/ViewTouch/viewtouch.git
UPSTREAM_COMMIT=d8174ba8237fb739276ff9ff03d2c6bde9e814b4
mkdir -p "$OUT" "$RAW"

if command -v magick >/dev/null; then IM=(magick); MONTAGE=(magick montage)
elif command -v convert >/dev/null; then IM=(convert); MONTAGE=(montage)
else echo "ImageMagick (magick or convert) is needed."; exit 2; fi

# --- 1 and 2: the tests ---------------------------------------------------------------------------
if [ "${MANUAL_SHOTS_RUN:-1}" != 0 ]; then
    [ -x "$TESTS" ] || { echo "No test program at $TESTS (build ViewTouch first, or give the build folder)."; exit 2; }
    if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then
        # OpenGL (Mesa's software renderer) on a virtual X screen: offscreen has no OpenGL on a server.
        GL=(env QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl LIBGL_ALWAYS_SOFTWARE=1
            xvfb-run -a -s "-screen 0 1920x1200x24")
    else
        GL=(env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl)
    fi
    echo "== [manual] (the demo store's screens)"
    VTM_SHOTS="$OUT" "${GL[@]}" "$TESTS" "[manual]" | tail -n 3
    echo "== every other test (their own pictures, into $RAW)"
    VTM_SHOTS="$RAW" QT_QPA_PLATFORM=offscreen timeout "${TESTS_TIMEOUT:-2400}" "$TESTS" | tail -n 3
    VTM_SHOTS="$RAW" QT_QPA_PLATFORM=offscreen "$TESTS" "[hostShots],[phoneFloors]" | tail -n 3
fi

# Copy (or convert, when the extension differs) $1 to shots/$2.
put() {
    local src="$1" dst="$OUT/$2"
    [ -f "$src" ] || return 1
    if [ "${src##*.}" = "${dst##*.}" ]; then cp "$src" "$dst"
    elif [ "${dst##*.}" = jpg ]; then "${IM[@]}" "$src" -strip -quality 85 "$dst"
    else "${IM[@]}" "$src" "$dst"; fi
}

# --- the manual's name <- the test's picture (in $RAW, .png) ------------------------------------------
MAP='
m64-kiosk-portrait-attract.png   p1-attract
m65-kiosk-portrait-menu.png      p4-menu
m66-kiosk-portrait-choices.png   p3-choices
m67-kiosk-portrait-easy-reach.png p5-easy-reach
m70-display-welcome.png          10-display-welcome
m71-display-order.png            11-display-order
m72-display-tip.png              12-display-tip
m73-display-thanks.png           13-display-thanks
m74-on-screen-keyboard.png       kb-1-numbers
m75-host-seat.png                9-host-seat
m85-kiosk-look.png               34-kiosk-look
m86-kiosk-look-menu.png          35-kiosk-look-menu
m87-weigh.png                    36-weigh
m88-login-logo.png               37-login-logo
m89-picture-field.png            40-picture-field
m90-screen-saver-logo.png        38-screen-saver-logo
m91-receipt-logo.png             41-receipt-logo-dots
m92-menu-grid.png                42-menu-grid
m93-store-font.png               43-store-font
m94-looks.png                    45-looks-gallery
m95-look-daylight.png            44-look-light-order
m96-look-cafe.png                44-look-cafe-order
m97-setup-welcome.png            46-setup-welcome
m97-setup-logo.png               46-setup-logo
m97-setup-look.png               46-setup-look
m97-setup-staff.png              46-setup-staff
m98-layouts.png                  47-layout-gallery
m100-find-item.png               51-find-item
m101-left-handed.png             53-left-handed
m102-table-checks.png            55-table-checks
m103-quantity.png                57-quantity
m104-table-tools.png             58-table-tools
m105-move-line.png               59-move-line
m106-explain.png                 63-explain
m107-undo.png                    60-undo
m108-popular.png                 62-popular
m109-manager-approval.png        64-manager-approval
m110-custom-discount.png         65-custom-discount
m111-time-clock.png              66-time-clock
m112-eod-clocked-in.png          68-end-of-day-clocked-in
m113-stock-left.png              69-stock-left
m114-dashboard.png               70-dashboard
m115-up-for-grabs.png            73-up-for-grabs
m117-checklists.png              74-checklists
m118-kitchen-late.png            75-kitchen-late
m119-fire-in.png                 76-fire-in
m120-arrange-menu.png            78-arrange-menu
m121-preview.png                 79-preview
m123-seating.jpg                 host-picked
m123-seating-phone.jpg           host-phone-picked
m124-swipe.jpg                   phone-0-swipe
m124-same.jpg                    phone-1-same
m124-deliveries.jpg              phone-2-deliveries
m125-qualifier-hold.jpg          qual-1-hold
m125-qualifier-chosen.jpg        qual-2-chosen
m126-card-reader.jpg             card-1-waiting
m127-counter-reader.jpg          counter-1-waiting
m128-find-refund.jpg             find-1-check
m128-refund-sheet.jpg            find-2-refund
m130-floor-dining.jpg            floor-dining
m131-floor-editing.jpg           floor-wall-turned
m132-host-free.jpg               host-free-next
m132-phone-floor.jpg             phone-tables
m132-phone-dining.jpg            phone-floor-dining
m133-coupon.jpg                  coupon-sheet
m133-often-added.jpg             order-goes-with
m134-kitchen-continued.jpg       kitchen-continued
m134-tab-hold.jpg                tab-hold-waiting
m135-forecast.jpg                forecast
m136-settings-search.jpg         settings-search
m136-tip-pool.jpg                settings-tip-pool
m137-phone-version.jpg           phone-version-edit
m137-phone-differences.jpg       phone-version-differences
m138-drawer-count.jpg            drawer-count
m138-drawer-count-phone.jpg      drawer-count-phone
m139-void-reason.jpg             66-void-reason
m140-changes.jpg                 changes-report
m141-stock-count.jpg             stock-count
m141-stock-report.jpg            stock-count-report
m142-waste.jpg                   waste-logged
m143-prep-labels.jpg             prep-labels
m144-store-check.jpg             store-check
m145-high-contrast.jpg           high-contrast
m149-phone-setup-guide.jpg       setup-phone-0
m149-phone-setup-colors.jpg      setup-phone-2
'
# The manual may ask for .png or .jpg: the name in MAP without its extension is matched to the manual's.
wanted() { grep -o '<img[^>]*src="shots/[^"]*"' "$MANUAL/manual.html" | sed 's/.*src="shots\///;s/"$//' | sort -u; }
WANTED="$(wanted)"
want_as() { printf '%s\n' "$WANTED" | grep -E "^${1%.*}\.(png|jpg)$" | head -n 1; }

while read -r dst src; do
    [ -n "${dst:-}" ] || continue
    name="$(want_as "$dst")"; [ -n "$name" ] || name="$dst"
    put "$RAW/$src.png" "$name" || echo "  (no $src.png from the tests, for $name)"
done <<< "$MAP"

# m99: the five Pay screen layouts, three across.
settle=("$RAW"/50-settle-*.png)
if [ -f "${settle[0]}" ]; then
    name="$(want_as m99-settle-layouts.jpg)"; [ -n "$name" ] || name=m99-settle-layouts.jpg
    "${MONTAGE[@]}" "${settle[@]}" -tile 3x -geometry 640x360+6+6 -background white "$OUT/$name"
fi

# A [manual] picture saved as .png that the manual wants as .jpg (m34b-about).
for name in $WANTED; do
    [ -f "$OUT/$name" ] || put "$OUT/${name%.*}.png" "$name" 2>/dev/null || true
done

# --- 3: the Label Utility (committed pictures) --------------------------------------------------------
for name in $WANTED; do
    case "$name" in
        lu-sample-*) put "$MODERN/docs/label-utility/img/${name#lu-sample-}" "$name" ;;
        lu-*)        put "$MODERN/docs/label-utility/screens/${name#lu-}" "$name" ;;
    esac
done

# --- 4: what no test makes any more: upstream's committed picture ------------------------------------
missing() { for name in $WANTED; do [ -f "$OUT/$name" ] || echo "$name"; done; }
if [ "${MANUAL_SHOTS_UPSTREAM:-1}" != 0 ] && [ -n "$(missing)" ]; then
    UP="$(mktemp -d)"
    if git -C "$UP" init -q && git -C "$UP" fetch -q --depth=1 --filter=blob:none "$UPSTREAM_REPO" "$UPSTREAM_COMMIT"; then
        for name in $(missing); do
            if git -C "$UP" show "$UPSTREAM_COMMIT:modern/docs/manual/shots/${name%.*}.jpg" > "$UP/pic.jpg" 2>/dev/null; then
                put "$UP/pic.jpg" "$name" && echo "  $name: upstream ViewTouch's picture (no test makes it here)"
            fi
        done
    fi
    rm -rf "$UP"
fi

left="$(missing)"
echo "== $(printf '%s\n' "$WANTED" | wc -l) pictures in the manual; $(printf '%s' "$left" | grep -c .) missing"
if [ -n "$left" ]; then echo "Missing:"; printf '%s\n' "$left"; exit 1; fi
