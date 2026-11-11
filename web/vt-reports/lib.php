<?php
// ViewTouch web reporting (web/vt-reports): the code index.php, upload.php and make-token.php share.
// PHP 7.4 or newer; no database and nothing from other websites. The stores' Raspberry Pis send
// their reports here (vt-web-reports); they are kept as JSON files in a folder outside public_html.
if (!defined('VTR')) {
    http_response_code(404);
    exit;
}

// --- settings (config.php) ---------------------------------------------------------------------

function vtr_config(): array
{
    static $c = null;
    if ($c !== null) {
        return $c;
    }
    $file = __DIR__ . '/config.php';
    $c = is_file($file) ? (include $file) : [];
    if (!is_array($c)) {
        $c = [];
    }
    $c += [
        // Where the reports and the store tokens are kept: OUTSIDE public_html. The default is the
        // folder vt-reports-data in the account's home folder (two folders up from this one when it
        // is public_html/vt-reports).
        'data_dir' => dirname(__DIR__, 2) . '/vt-reports-data',
        'stores' => ['BHDAN' => 'BHDAN', 'HCA' => 'HCA'],   // code => name on the buttons
        'timezone' => 'America/New_York',
        'week_starts' => 0,          // 0 Sunday, 1 Monday... (Store Settings' week)
        'day_starts_hour' => 6,      // a business day counts from 6 AM, as on the Pi
        'stale_minutes' => 30,       // warn when a store hasn't sent for this long
        'max_upload_bytes' => 8 * 1024 * 1024,     // one upload, as sent (gzipped)
        'max_json_bytes' => 64 * 1024 * 1024,      // one upload, unzipped
        'require_https' => true,
        'require_login' => true,     // index.php shows nothing without the Basic Auth password
    ];
    date_default_timezone_set((string)$c['timezone']);
    return $c;
}

function vtr_data_dir(): string
{
    return rtrim((string)vtr_config()['data_dir'], '/');
}

// The data folder must not be one the web server shows: refuse to run if it is inside the
// website's folder (a setup mistake), and keep a "deny all" in it anyway.
function vtr_check_data_dir(): ?string
{
    $dir = vtr_data_dir();
    if (!is_dir($dir) && !@mkdir($dir, 0700, true)) {
        return 'The data folder ' . $dir . ' does not exist and cannot be made. Set data_dir in config.php.';
    }
    $real = realpath($dir);
    $root = isset($_SERVER['DOCUMENT_ROOT']) ? realpath((string)$_SERVER['DOCUMENT_ROOT']) : false;
    if ($real !== false && $root !== false && $root !== '' && strpos($real . '/', rtrim($root, '/') . '/') === 0) {
        return 'The data folder ' . $dir . ' is inside the website (public_html). Move it out and set data_dir '
            . 'in config.php (see README.md).';
    }
    if (!is_file($dir . '/.htaccess')) {
        @file_put_contents($dir . '/.htaccess', "Require all denied\n");
    }
    return null;
}

function vtr_is_https(): bool
{
    if (!empty($_SERVER['HTTPS']) && strtolower((string)$_SERVER['HTTPS']) !== 'off') {
        return true;
    }
    return isset($_SERVER['SERVER_PORT']) && (string)$_SERVER['SERVER_PORT'] === '443';
}

function vtr_login_user(): string
{
    foreach (['REMOTE_USER', 'REDIRECT_REMOTE_USER', 'PHP_AUTH_USER', 'AUTH_USER'] as $k) {
        if (!empty($_SERVER[$k])) {
            return (string)$_SERVER[$k];
        }
    }
    return '';
}

function vtr_headers(string $type = 'text/html; charset=utf-8'): void
{
    header('Content-Type: ' . $type);
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: same-origin');
    header('X-Frame-Options: DENY');
    header("Content-Security-Policy: default-src 'self'; img-src 'self' data:; frame-ancestors 'none'; form-action 'self'");
    header('Cache-Control: no-store, private');
}

function h($s): string
{
    return htmlspecialchars((string)$s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

// --- store tokens (tokens.json in the data folder: only their SHA-256) -------------------------

function vtr_tokens_file(): string
{
    return vtr_data_dir() . '/tokens.json';
}

function vtr_tokens(): array
{
    $f = vtr_tokens_file();
    if (!is_file($f)) {
        return [];
    }
    $t = json_decode((string)file_get_contents($f), true);
    return is_array($t) ? $t : [];
}

function vtr_valid_store(string $code): bool
{
    return (bool)preg_match('/^[A-Z0-9]{2,12}$/', $code);
}

// A new token for `store` (the old one stops working). Returns the token: shown once, never kept.
function vtr_make_token(string $store): string
{
    $token = $store . '-' . bin2hex(random_bytes(24));
    $tokens = vtr_tokens();
    $tokens[$store] = ['sha256' => hash('sha256', $token), 'made' => date('c')];
    vtr_write_file(vtr_tokens_file(), json_encode($tokens, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n");
    return $token;
}

// The store a token belongs to, or '' (every stored hash is compared, in constant time).
function vtr_store_of_token(string $token): string
{
    $found = '';
    if ($token === '' || strlen($token) > 200) {
        return '';
    }
    $hash = hash('sha256', $token);
    foreach (vtr_tokens() as $store => $t) {
        if (is_array($t) && isset($t['sha256']) && hash_equals((string)$t['sha256'], $hash) && vtr_valid_store((string)$store)) {
            $found = (string)$store;
        }
    }
    return $found;
}

// Written beside, then renamed into place: a reader never sees half a file.
function vtr_write_file(string $file, string $data): bool
{
    $dir = dirname($file);
    if (!is_dir($dir) && !@mkdir($dir, 0700, true)) {
        return false;
    }
    $tmp = $file . '.' . bin2hex(random_bytes(4)) . '.part';
    if (@file_put_contents($tmp, $data, LOCK_EX) === false) {
        return false;
    }
    @chmod($tmp, 0600);
    if (!@rename($tmp, $file)) {
        @unlink($tmp);
        return false;
    }
    return true;
}

// --- the reports --------------------------------------------------------------------------------

// Every report the Reports screen lists (the Forecast looks ahead, so it isn't sent), in its order.
// additive: per-day numbers that may be added up over days and stores.
function vtr_reports(): array
{
    return [
        'sales' => ['Sales', 'Sales', true],
        'items' => ['Items', 'Sales', true],
        'categories' => ['Categories', 'Sales', true],
        'hourly' => ['By Hour', 'Sales', true],
        'servers' => ['Servers', 'Staff', true],
        'employee_items' => ['By Employee', 'Staff', true],
        'tips' => ['Tips', 'Staff', true],
        'labor' => ['Labor', 'Staff', false],
        'timeclock' => ['Time Clock', 'Staff', false],
        'drawer' => ['Drawer', 'Cash', false],
        'expenses' => ['Expenses', 'Cash', true],
        'deposit' => ['Deposit', 'Cash', true],
        'purchases' => ['Purchases', 'Stock', false],
        'stock' => ['Stock', 'Stock', false],
        'audit' => ['Audit', 'Checks', false],
        'exceptions' => ['Exceptions', 'Checks', false],
        'changes' => ['Changes', 'Checks', false],
        'customers' => ['Customers', 'Customers', false],
        'royalty' => ['Royalty', 'Accounting', true],
        'accounting' => ['Accounting', 'Accounting', true],
        'accounts' => ['Gift Cards: Summary', 'Gift Cards', false],
        'gc_activated' => ['Gift Cards: Activated Cards', 'Gift Cards', false],
        'gc_balances' => ['Gift Cards: Active Balances', 'Gift Cards', false],
        'gc_inactive' => ['Gift Cards: Inactive Cards', 'Gift Cards', false],
        'gc_tx' => ['Gift Cards: Transactions', 'Gift Cards', false],
        'kitchen' => ['Kitchen', 'Kitchen', false],
        'foodcost' => ['Food Cost', 'Kitchen', false],
        'engineering' => ['Menu Mix', 'Kitchen', false],
        'turns' => ['Turns', 'Kitchen', false],
        'checklists' => ['Checklists', 'Other', false],
        'drivers' => ['Drivers', 'Other', false],
    ];
}

function vtr_valid_report(string $id): bool
{
    return (bool)preg_match('/^[a-z0-9_]{1,40}$/', $id);
}

function vtr_valid_date(string $d): bool
{
    return (bool)preg_match('/^(\d{4})-(\d{2})-(\d{2})$/', $d, $m) && checkdate((int)$m[2], (int)$m[3], (int)$m[1]);
}

// The stores to show: config.php's, then any others that have sent reports.
function vtr_stores(): array
{
    $stores = [];
    foreach ((array)vtr_config()['stores'] as $code => $name) {
        if (vtr_valid_store((string)$code)) {
            $stores[(string)$code] = (string)$name;
        }
    }
    foreach (glob(vtr_data_dir() . '/*', GLOB_ONLYDIR) ?: [] as $d) {
        $code = basename($d);
        if (vtr_valid_store($code) && !isset($stores[$code])) {
            $stores[$code] = $code;
        }
    }
    return $stores;
}

function vtr_status(string $store): array
{
    $f = vtr_data_dir() . '/' . $store . '/_status.json';
    $s = is_file($f) ? json_decode((string)file_get_contents($f), true) : null;
    return is_array($s) ? $s : [];
}

// One store's report for one business date: each business day's file (usually one), by day.
function vtr_load(string $store, string $report, string $date): array
{
    if (!vtr_valid_store($store) || !vtr_valid_report($report) || !vtr_valid_date($date)) {
        return [];
    }
    $out = [];
    foreach (glob(vtr_data_dir() . "/$store/$report/{$date}_*.json") ?: [] as $f) {
        $j = json_decode((string)file_get_contents($f), true);
        if (is_array($j) && isset($j['report']['rows'])) {
            $out[(int)($j['day']['id'] ?? 0)] = $j;
        }
    }
    ksort($out);
    return array_values($out);
}

// --- dates -------------------------------------------------------------------------------------

function vtr_today(): DateTimeImmutable
{
    $h = (int)vtr_config()['day_starts_hour'];
    return (new DateTimeImmutable('now'))->modify("-$h hours")->setTime(0, 0);
}

// [from, to, label] for a period: today, yesterday, week, lastweek, month, lastmonth, day (date), range.
function vtr_period(string $period, string $date, string $from, string $to): array
{
    $today = vtr_today();
    $ws = (int)vtr_config()['week_starts'];
    $weekStart = $today->modify('-' . (((int)$today->format('w') - $ws + 7) % 7) . ' days');
    switch ($period) {
        case 'yesterday':
            $d = $today->modify('-1 day');
            return [$d, $d, 'Yesterday'];
        case 'week':
            return [$weekStart, $today, 'This week'];
        case 'lastweek':
            return [$weekStart->modify('-7 days'), $weekStart->modify('-1 day'), 'Last week'];
        case 'month':
            return [$today->modify('first day of this month'), $today, 'This month'];
        case 'lastmonth':
            $first = $today->modify('first day of last month');
            return [$first, $first->modify('last day of this month'), 'Last month'];
        case 'day':
            if (vtr_valid_date($date)) {
                $d = new DateTimeImmutable($date);
                return [$d, $d, ''];
            }
            break;
        case 'range':
            if (vtr_valid_date($from) && vtr_valid_date($to)) {
                $a = new DateTimeImmutable($from);
                $b = new DateTimeImmutable($to);
                if ($b < $a) {
                    [$a, $b] = [$b, $a];
                }
                if ($a->diff($b)->days > 400) {
                    $a = $b->modify('-400 days');
                }
                return [$a, $b, ''];
            }
            break;
    }
    return [$today, $today, 'Today'];
}

function vtr_dates(DateTimeImmutable $from, DateTimeImmutable $to): array
{
    $out = [];
    for ($d = $from; $d <= $to; $d = $d->modify('+1 day')) {
        $out[] = $d->format('Y-m-d');
    }
    return $out;
}

function vtr_nice_date(string $date): string
{
    return (new DateTimeImmutable($date))->format('D M j, Y');
}

function vtr_clock(int $ms): string
{
    return $ms > 0 ? date('g:i A', intdiv($ms, 1000)) : '';
}

function vtr_ago(int $seconds): string
{
    if ($seconds < 90) {
        return 'just now';
    }
    if ($seconds < 5400) {
        return round($seconds / 60) . ' minutes ago';
    }
    if ($seconds < 172800) {
        return round($seconds / 3600) . ' hours ago';
    }
    return round($seconds / 86400) . ' days ago';
}

// --- adding reports up (several days, several stores) ------------------------------------------

// "$1,234.56", "-$3.00", "($3.00)", "12", "3.5" -> [value, money, decimals, commas]; else null.
function vtr_number(string $s): ?array
{
    $t = trim($s);
    if ($t === '') {
        return null;
    }
    $neg = false;
    if (preg_match('/^\((.*)\)$/', $t, $m)) {
        $neg = true;
        $t = trim($m[1]);
    }
    if ($t !== '' && $t[0] === '-') {
        $neg = !$neg;
        $t = substr($t, 1);
    }
    $money = false;
    if ($t !== '' && $t[0] === '$') {
        $money = true;
        $t = substr($t, 1);
        if ($t !== '' && $t[0] === '-') {
            $neg = !$neg;
            $t = substr($t, 1);
        }
    }
    if (!preg_match('/^(\d{1,3}(,\d{3})+|\d+)(\.(\d+))?$/', $t, $m)) {
        return null;
    }
    $v = (float)str_replace(',', '', $t);
    return [$neg ? -$v : $v, $money, isset($m[4]) ? strlen($m[4]) : 0, strpos($t, ',') !== false];
}

function vtr_format_number(float $v, bool $money, int $dec, bool $commas): string
{
    if ($money) {
        return ($v < -0.004 ? '-' : '') . '$' . number_format(abs($v), 2);
    }
    return number_format($v, $dec, '.', $commas || abs($v) >= 10000 ? ',' : '');
}

// Columns that are never added up: shares, averages, rates, prices.
function vtr_column_adds(string $header): bool
{
    $h = strtolower($header);
    foreach (['%', 'avg', 'average', 'per ', 'longest', 'rate', 'price', 'each', 'share', 'margin', 'cost/'] as $w) {
        if (strpos($h, $w) !== false) {
            return false;
        }
    }
    return true;
}

// Several reports of the same kind as one: rows matched by section, kind and first cell (in the
// order they come), numbers added up; a share, an average or a time is left blank (it can't be
// added), as is a column whose text differs. The report payloads: [['report' => [...]], ...].
function vtr_combine(array $files): array
{
    $columns = [];
    $title = '';
    foreach ($files as $f) {
        if (!$columns && !empty($f['report']['columns'])) {
            $columns = $f['report']['columns'];
        }
        if ($title === '' && !empty($f['report']['title'])) {
            $title = $f['report']['title'];
        }
    }
    $order = [];   // keys in order
    $rows = [];    // key => [kind, label, cells state]
    foreach ($files as $f) {
        $section = '';
        $seen = [];
        $pos = -1;
        foreach ((array)($f['report']['rows'] ?? []) as $row) {
            $kind = (string)($row['kind'] ?? 'line');
            $cells = array_map('strval', (array)($row['cells'] ?? []));
            $label = $cells[0] ?? '';
            if ($kind === 'section') {
                $section = $label;
            }
            $key = $kind . "\x1f" . $section . "\x1f" . $label;
            $seen[$key] = ($seen[$key] ?? 0) + 1;
            $key .= "\x1f" . $seen[$key];
            if (!isset($rows[$key])) {
                array_splice($order, $pos + 1, 0, [$key]);
                $rows[$key] = ['kind' => $kind, 'label' => $label, 'acc' => []];
            }
            $pos = array_search($key, $order, true);
            if ($kind === 'section' || $kind === 'note') {
                continue;
            }
            for ($i = 1; $i < count($cells); $i++) {
                $a = $rows[$key]['acc'][$i] ?? ['sum' => 0.0, 'nums' => 0, 'money' => false, 'dec' => 0, 'commas' => false,
                                                  'text' => null, 'bad' => false];
                $c = trim($cells[$i]);
                if ($c !== '') {
                    $n = vtr_number($c);
                    if (!vtr_column_adds((string)($columns[$i] ?? ''))) {
                        $a['bad'] = true;
                    } elseif ($n !== null) {
                        $a['sum'] += $n[0];
                        $a['nums']++;
                        $a['money'] = $a['money'] || $n[1];
                        $a['dec'] = max($a['dec'], $n[2]);
                        $a['commas'] = $a['commas'] || $n[3];
                    } elseif (strpos($c, '%') !== false || preg_match('/^\d+:\d\d/', $c)) {
                        $a['bad'] = true;   // a share or a time
                    } elseif ($a['text'] === null) {
                        $a['text'] = $c;
                    } elseif ($a['text'] !== $c) {
                        $a['bad'] = true;
                    }
                }
                $rows[$key]['acc'][$i] = $a;
            }
        }
    }
    $out = [];
    foreach ($order as $key) {
        $r = $rows[$key];
        $cells = [$r['label']];
        $n = $r['acc'] ? max(array_keys($r['acc'])) : 0;
        for ($i = 1; $i <= $n; $i++) {
            $a = $r['acc'][$i] ?? null;
            if ($a === null || $a['bad'] || ($a['nums'] > 0 && $a['text'] !== null)) {
                $cells[] = '';
            } elseif ($a['nums'] > 0) {
                $cells[] = vtr_format_number($a['sum'], $a['money'], $a['dec'], $a['commas']);
            } else {
                $cells[] = (string)$a['text'];
            }
        }
        $out[] = ['kind' => $r['kind'], 'cells' => $cells];
    }
    return ['title' => $title, 'columns' => $columns, 'rows' => $out];
}

// --- showing a report --------------------------------------------------------------------------

function vtr_table(array $report): string
{
    $columns = (array)($report['columns'] ?? []);
    $rows = (array)($report['rows'] ?? []);
    $n = max(1, count($columns));
    foreach ($rows as $r) {
        $n = max($n, count((array)($r['cells'] ?? [])));
    }
    $o = '<div class="scroll"><table class="report">';
    if (array_filter($columns, function ($c) { return trim((string)$c) !== ''; })) {
        $o .= '<thead><tr>';
        for ($i = 0; $i < $n; $i++) {
            $o .= '<th' . ($i ? ' class="num"' : '') . '>' . h($columns[$i] ?? '') . '</th>';
        }
        $o .= '</tr></thead>';
    }
    $o .= '<tbody>';
    if (!$rows) {
        $o .= '<tr class="note"><td colspan="' . $n . '">Nothing to show.</td></tr>';
    }
    foreach ($rows as $r) {
        $kind = (string)($r['kind'] ?? 'line');
        $cells = (array)($r['cells'] ?? []);
        if ($kind === 'section') {
            $o .= '<tr class="section"><th colspan="' . $n . '">' . h($cells[0] ?? '') . '</th></tr>';
        } elseif ($kind === 'note') {
            $o .= '<tr class="note"><td colspan="' . $n . '">' . h(implode(' ', $cells)) . '</td></tr>';
        } else {
            $o .= '<tr class="' . ($kind === 'total' ? 'total' : 'line') . '">';
            for ($i = 0; $i < $n; $i++) {
                $o .= '<td' . ($i ? ' class="num"' : '') . '>' . h($cells[$i] ?? '') . '</td>';
            }
            $o .= '</tr>';
        }
    }
    return $o . '</tbody></table></div>';
}

// "Mon Nov 9, 2026 · closed 9:05 PM" / "· still open, as of 12:30 PM".
function vtr_day_line(array $file): string
{
    $day = (array)($file['day'] ?? []);
    $s = vtr_nice_date((string)($day['date'] ?? date('Y-m-d')));
    if (!empty($day['open'])) {
        $s .= ' · still open, as of ' . vtr_clock((int)($file['generatedAtMs'] ?? 0));
    } else {
        $s .= ' · opened ' . vtr_clock((int)($day['openedAtMs'] ?? 0)) . ', closed ' . vtr_clock((int)($day['closedAtMs'] ?? 0));
    }
    return $s;
}

// A cell for a spreadsheet: one starting like a formula is kept as text.
function vtr_csv_cell(string $s): string
{
    if ($s !== '' && (strpos('=@', $s[0]) !== false || (strpos('+-', $s[0]) !== false && !preg_match('/^[+-]\$?[\d.,]+$/', $s)))) {
        return "'" . $s;
    }
    return $s;
}
