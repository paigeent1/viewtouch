#!/bin/bash
# Tests for the web reporting page (web/vt-reports) and the Pi's vt-web-reports, on this computer:
#   bash web/vt-reports/tests/run.sh
# Needs php (7.4+), curl and gzip. A copy of the site runs under `php -S` with a throwaway data
# folder; the reports are made up (no store's data).
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="${VTR_KEEP:-$(mktemp -d)}"   # VTR_KEEP=<empty folder>: keep what the tests made, to look at
PORT="${VTR_TEST_PORT:-$((18000 + RANDOM % 2000))}"
URL="http://127.0.0.1:$PORT/vt-reports"
PASS=0 FAIL=0
cleanup() { [ -n "${SERVER:-}" ] && kill "$SERVER" 2>/dev/null; [ -n "${VTR_KEEP:-}" ] || rm -rf "$WORK"; }
trap cleanup EXIT
ok() { PASS=$((PASS + 1)); echo "ok   $*"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL $*"; }
check() { local what="$1"; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }
has() { grep -q -F -- "$2" "$1"; }

# The site, with test settings: no HTTPS or password here (php -S has neither).
mkdir -p "$WORK/site" "$WORK/data"
cp -r "$HERE" "$WORK/site/vt-reports"
rm -rf "$WORK/site/vt-reports/tests"
cat > "$WORK/site/vt-reports/config.php" <<EOF
<?php
if (!defined('VTR')) { exit; }
return ['data_dir' => '$WORK/data', 'stores' => ['BHDAN' => 'BHDAN', 'HCA' => 'HCA'],
        'require_https' => false, 'require_login' => false, 'max_upload_bytes' => 200000,
        'timezone' => 'America/New_York'];
EOF
php -d opcache.enable=0 -d opcache.enable_cli=0 -S 127.0.0.1:"$PORT" -t "$WORK/site" > "$WORK/server.log" 2>&1 &
SERVER=$!
for _ in $(seq 1 50); do curl -s -o /dev/null "$URL/index.php" && break; sleep 0.1; done

# Tokens, made the way the owner does.
T1=$(cd "$WORK/site/vt-reports" && php make-token.php BHDAN | sed -n 's/^  \(BHDAN-[0-9a-f]*\)$/\1/p')
T2=$(cd "$WORK/site/vt-reports" && php make-token.php HCA | sed -n 's/^  \(HCA-[0-9a-f]*\)$/\1/p')
check "make-token makes a token per store" test -n "$T1" -a -n "$T2"
check "only the token's hash is kept" sh -c "! grep -q '$T1' '$WORK/data/tokens.json' && grep -q sha256 '$WORK/data/tokens.json'"
check "make-token refuses a bad store code" sh -c "! (cd '$WORK/site/vt-reports' && php make-token.php '../x' >/dev/null 2>&1)"

# A made-up report file like vtmodern --export-reports writes. $1 id, $2 date, $3 day id, $4 net, $5 checks, $6 open
report() {
    cat <<EOF
{"format":"viewtouch-report/1","report":{"id":"$1","title":"Sales Summary","subtitle":"Test","columns":["","Amount","Share %"],
"rows":[{"kind":"section","cells":["Sales"]},{"kind":"line","cells":["Checks","$5",""]},
{"kind":"line","cells":["Food","\$1,000.00","40%"]},{"kind":"line","cells":["<script>alert(1)</script>","\$2.00","1%"]},
{"kind":"total","cells":["Net sales","$4","100%"]},{"kind":"note","cells":["Made-up numbers"]}]},
"store":{"name":"Test Shop","checkPrefix":"TS"},"day":{"id":$3,"date":"$2","open":$6,"openedAt":"","closedAt":"",
"openedAtMs":1773144000000,"closedAtMs":1773187200000},"generatedAt":"","generatedAtMs":1773190000000}
EOF
}
bundle() { printf '{"format":"viewtouch-upload/1","files":['; local sep=""; for f in "$@"; do printf '%s%s' "$sep" "$f"; sep=,; done; printf ']}'; }
post() {   # $1 token, $2 body file, $3 content type; prints the HTTP status
    curl -s -o "$WORK/answer" -w '%{http_code}' -H "X-VT-Token: $1" -H "Content-Type: $3" --data-binary @"$2" "$URL/upload.php"
}

TODAY=$(TZ=America/New_York php -r 'echo (new DateTime("now"))->modify("-6 hours")->format("Y-m-d");')
YDAY=$(TZ=America/New_York php -r 'echo (new DateTime("now"))->modify("-6 hours")->modify("-1 day")->format("Y-m-d");')
bundle "$(report sales "$YDAY" 40 '$1,200.50' 31 false)" "$(report sales "$TODAY" 41 '$300.25' 9 true)" \
       "$(report timeclock "$TODAY" 41 '$0.00' 0 true)" | gzip -c > "$WORK/b1.gz"
bundle "$(report sales "$TODAY" 7 '$99.75' 4 true)" > "$WORK/b2.json"

# --- upload.php ---
check "upload: good token, gzipped -> 200" test "$(post "$T1" "$WORK/b1.gz" application/gzip)" = 200
check "upload: says what it saved" has "$WORK/answer" '"saved":3'
check "upload: kept by store/report/day" test -f "$WORK/data/BHDAN/sales/${TODAY}_41.json" -a -f "$WORK/data/BHDAN/sales/${YDAY}_40.json"
check "upload: plain JSON with the other store's token -> its own store" test "$(post "$T2" "$WORK/b2.json" application/json)" = 200
check "upload: the store is the token's" test -f "$WORK/data/HCA/sales/${TODAY}_7.json" -a ! -f "$WORK/data/BHDAN/sales/${TODAY}_7.json"
check "upload: a wrong token -> 403" test "$(post "BHDAN-0000000000000000000000000000000000000000" "$WORK/b2.json" application/json)" = 403
check "upload: no token -> 403" test "$(curl -s -o /dev/null -w '%{http_code}' --data-binary @"$WORK/b2.json" "$URL/upload.php")" = 403
check "upload: GET -> 405" test "$(curl -s -o /dev/null -w '%{http_code}' "$URL/upload.php")" = 405
echo 'not json at all' > "$WORK/junk"
check "upload: not JSON -> 400" test "$(post "$T1" "$WORK/junk" application/json)" = 400
head -c 300000 /dev/urandom > "$WORK/big"
check "upload: too big -> 413" test "$(post "$T1" "$WORK/big" application/octet-stream)" = 413
bundle "$(report '../../evil' "$TODAY" 1 '$1.00' 1 true)" "$(report sales '2026-02-31' 1 '$1.00' 1 true)" \
       "$(report sales "$TODAY" '"x"' '$1.00' 1 true)" > "$WORK/b3.json"
check "upload: bad names, dates and days are refused" test "$(post "$T1" "$WORK/b3.json" application/json)" = 200
check "upload: ...none of them saved" has "$WORK/answer" '"saved":0,"rejected":3'
check "upload: nothing written outside the store folders" test ! -e "$WORK/evil" -a ! -e "$WORK/data/evil"
# A day that moved to another date keeps only its newest file.
bundle "$(report sales "$YDAY" 41 '$300.25' 9 true)" > "$WORK/b4.json"
post "$T1" "$WORK/b4.json" application/json >/dev/null
check "upload: a day's old date is removed" test -f "$WORK/data/BHDAN/sales/${YDAY}_41.json" -a ! -f "$WORK/data/BHDAN/sales/${TODAY}_41.json"
bundle "$(report sales "$TODAY" 41 '$300.25' 9 true)" > "$WORK/b4.json"
post "$T1" "$WORK/b4.json" application/json >/dev/null

# --- index.php ---
get() { curl -s -o "$WORK/page" -w '%{http_code}' "$URL/index.php?$1"; }
check "page: one store, today -> 200" test "$(get "store=BHDAN&report=sales&period=today")" = 200
check "page: shows its numbers" has "$WORK/page" '$300.25'
check "page: the store's name from Store Settings" has "$WORK/page" 'Test Shop (BHDAN)'
check "page: the day still open" has "$WORK/page" 'still open'
check "page: everything escaped" sh -c "has() { grep -q -F -- \"\$2\" \"\$1\"; }; has '$WORK/page' '&lt;script&gt;alert(1)&lt;/script&gt;' && ! has '$WORK/page' '<script>alert'"
check "page: yesterday" sh -c "[ \"\$(curl -s -o '$WORK/page' -w '%{http_code}' '$URL/index.php?store=BHDAN&report=sales&period=yesterday')\" = 200 ] && grep -q -F '\$1,200.50' '$WORK/page'"
get "store=all&report=sales&period=today" >/dev/null
check "page: all stores together adds up net sales" has "$WORK/page" '$400.00'
check "page: ...and the checks" has "$WORK/page" '>13<'
check "page: ...shares left blank, amounts added" has "$WORK/page" '$2,000.00'
check "page: ...and each store under it" sh -c "grep -q -F '\$99.75' '$WORK/page' && grep -q -F '\$300.25' '$WORK/page'"
get "store=BHDAN&report=sales&period=range&from=$YDAY&to=$TODAY" >/dev/null
check "page: two days added up" has "$WORK/page" '$1,500.75'
get "store=BHDAN&report=timeclock&period=range&from=$YDAY&to=$TODAY" >/dev/null
check "page: a report that isn't added up shows each day" has "$WORK/page" "each business day is shown on its own"
get "store=HCA&report=gc_tx&period=today" >/dev/null
check "page: a report not sent yet says so" has "$WORK/page" 'No Gift Cards: Transactions report'
check "page: every period button" sh -c "for p in today yesterday week lastweek month lastmonth; do grep -q \"period=\$p\" '$WORK/page' || exit 1; done"
check "page: a bad report or store falls back safely" test "$(get "store=../etc&report=%3Cx%3E&period=zzz")" = 200
check "page: a store that sent recently isn't late" sh -c "grep -A2 -F '<strong>BHDAN' '$WORK/page' | grep -q 'last updated'"
php -r '$f=$argv[1]; $s=json_decode(file_get_contents($f),true); $s["lastUpload"]=time()-3600; file_put_contents($f,json_encode($s));' "$WORK/data/HCA/_status.json"
get "store=all&report=sales&period=today" >/dev/null
check "page: a store that hasn't sent in 30 minutes is flagged" has "$WORK/page" "hasn't sent in over 30 minutes"

# --- CSV ---
curl -s -D "$WORK/headers" -o "$WORK/csv" "$URL/index.php?store=all&report=sales&period=today&csv=1"
check "csv: a download" grep -q -i 'content-disposition: attachment; filename="viewtouch-sales-all-' "$WORK/headers"
check "csv: the combined total" grep -q -F '"Net sales",$400.00' "$WORK/csv"
check "csv: the rows" grep -q -F 'Food,"$1,000.00",40%' "$WORK/csv"

# --- the password check (as on the website) ---
sed -i "s/'require_login' => false/'require_login' => true/" "$WORK/site/vt-reports/config.php"
check "page: no password set up -> nothing shown" sh -c "[ \"\$(curl -s -o '$WORK/page' -w '%{http_code}' '$URL/index.php')\" = 401 ] && ! grep -q -F '\$300.25' '$WORK/page'"
check "page: with a login it shows" test "$(curl -s -o /dev/null -w '%{http_code}' -u owner:x "$URL/index.php")" = 200
sed -i "s/'require_https' => false/'require_https' => true/" "$WORK/site/vt-reports/config.php"
check "upload: plain http refused when HTTPS is required" test "$(post "$T1" "$WORK/b2.json" application/json)" = 403
sed -i "s/'require_https' => true/'require_https' => false/; s/'require_login' => true/'require_login' => false/" "$WORK/site/vt-reports/config.php"

# --- the Pi's vt-web-reports, with a stand-in for vtmodern --export-reports ---
mkdir -p "$WORK/home/.config/viewtouch"
cat > "$WORK/fake-vtmodern" <<EOF
#!/bin/bash
# Stand-in: writes made-up report files like --export-reports. Args: --export-reports DIR --from A --to B
while [ \$# -gt 0 ]; do case "\$1" in --export-reports) out="\$2"; shift;; --from) from="\$2"; shift;; --to) to="\$2"; shift;; esac; shift; done
echo "\$from \$to" > "$WORK/fake-args"
mkdir -p "\$out/sales" "\$out/items"
echo '$(report sales "$TODAY" 42 '$55.55' 3 true | tr -d '\n')' > "\$out/sales/${TODAY}_42.json"
echo '$(report items "$TODAY" 42 '$55.55' 3 true | tr -d '\n')' > "\$out/items/${TODAY}_42.json"
echo '$(report sales "$YDAY" 41 '$300.25' 9 false | tr -d '\n')' > "\$out/sales/${YDAY}_41.json"
EOF
chmod +x "$WORK/fake-vtmodern"
printf 'VT_WEB_REPORTS_URL=%q\nVT_WEB_REPORTS_TOKEN=%q\n' "$URL/upload.php" "$T1" > "$WORK/home/.config/viewtouch/web-reports.conf"
runpi() { HOME="$WORK/home" VT_WEB_REPORTS_BIN="$WORK/fake-vtmodern" bash "$REPO/scripts/bin/vt-web-reports" "$@"; }
check "vt-web-reports --now sends" runpi --now
check "vt-web-reports: the first run sends 60 days" sh -c "[ \"\$(cut -d' ' -f1 '$WORK/fake-args')\" = \"\$(date -d '60 days ago' +%F)\" ]"
check "vt-web-reports: the files arrived" test -f "$WORK/data/BHDAN/items/${TODAY}_42.json"
runpi --now >/dev/null
check "vt-web-reports: later runs send the last days" sh -c "[ \"\$(cut -d' ' -f1 '$WORK/fake-args')\" = \"\$(date -d '2 days ago' +%F)\" ]"
check "vt-web-reports --status says when it worked" sh -c "HOME='$WORK/home' bash '$REPO/scripts/bin/vt-web-reports' --status | grep -q 'Last worked: 20'"
# A second website (vt-web-reports --add): here the same test site under the other store's token.
printf 'VT_WEB_REPORTS_URL_2=%q\nVT_WEB_REPORTS_TOKEN_2=%q\n' "$URL/upload.php" "$T2" >> "$WORK/home/.config/viewtouch/web-reports.conf"
rm -f "$WORK/data/HCA/items/${TODAY}_42.json"
check "vt-web-reports: sends to every website" sh -c "HOME='$WORK/home' VT_WEB_REPORTS_BIN='$WORK/fake-vtmodern' bash '$REPO/scripts/bin/vt-web-reports' --now >/dev/null && [ -f '$WORK/data/HCA/items/${TODAY}_42.json' ]"
printf 'VT_WEB_REPORTS_URL=%q\nVT_WEB_REPORTS_TOKEN=%q\n' "$URL/upload.php" "BHDAN-wrongwrongwrongwrongwrong" > "$WORK/home/.config/viewtouch/web-reports.conf"
check "vt-web-reports: a refused upload is an error (tried again next run)" sh -c "! HOME='$WORK/home' VT_WEB_REPORTS_BIN='$WORK/fake-vtmodern' bash '$REPO/scripts/bin/vt-web-reports' --now >/dev/null 2>&1"
check "vt-web-reports --status shows the problem" sh -c "HOME='$WORK/home' bash '$REPO/scripts/bin/vt-web-reports' --status | grep -q 'Last problem:.*403'"
check "vt-web-reports --setup wants https" sh -c "! HOME='$WORK/home' bash '$REPO/scripts/bin/vt-web-reports' --setup http://x/upload.php abcdefabcdefabcdef >/dev/null"
check "vt-web-reports: the settings file is private" sh -c "HOME='$WORK/home'; f=\$HOME/.config/viewtouch/web-reports.conf; chmod 600 \$f; [ \"\$(stat -c %a \$f)\" = 600 ]"

if grep -q -i -E 'fatal|warning|notice|deprecated' "$WORK/server.log"; then
    bad "PHP said:"; grep -i -E 'fatal|warning|notice|deprecated' "$WORK/server.log" | head -n 5
else
    ok "no PHP warnings"
fi
echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
