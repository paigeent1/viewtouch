<?php
// ViewTouch web reporting: where the stores' Pis send their reports (vt-web-reports, every 15
// minutes). Not behind the page's password: each store has a token of its own instead (the
// X-VT-Token header; make-token.php makes them). The store is the token's, never the request's.
// The body: {"format": "viewtouch-upload/1", "files": [<one report file>, ...]}, gzipped or not.
// Each report is kept as <data folder>/<store>/<report>/<YYYY-MM-DD>_<day id>.json.
define('VTR', 1);
require __DIR__ . '/lib.php';

function answer(int $code, array $body): void
{
    http_response_code($code);
    vtr_headers('application/json; charset=utf-8');
    echo json_encode($body, JSON_UNESCAPED_SLASHES), "\n";
    exit;
}

$config = vtr_config();
if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    header('Allow: POST');
    answer(405, ['ok' => false, 'error' => 'POST only']);
}
if (!empty($config['require_https']) && !vtr_is_https()) {
    answer(403, ['ok' => false, 'error' => 'HTTPS only']);
}
if ($problem = vtr_check_data_dir()) {
    error_log('vt-reports: ' . $problem);
    answer(500, ['ok' => false, 'error' => 'The website is not set up: see its error log']);
}

$token = (string)($_SERVER['HTTP_X_VT_TOKEN'] ?? '');
if ($token === '' && preg_match('/^Bearer\s+(\S+)$/', (string)($_SERVER['HTTP_AUTHORIZATION'] ?? ''), $m)) {
    $token = $m[1];
}
$store = vtr_store_of_token($token);
if ($store === '') {
    usleep(500000);   // slows guessing (a token has 192 random bits anyway)
    answer(403, ['ok' => false, 'error' => 'Unknown store token']);
}

$max = (int)$config['max_upload_bytes'];
$length = (int)($_SERVER['CONTENT_LENGTH'] ?? 0);
if ($length > $max) {
    answer(413, ['ok' => false, 'error' => "Too big (at most $max bytes)"]);
}
$in = fopen('php://input', 'rb');
$body = $in ? (string)stream_get_contents($in, $max + 1) : '';
if (strlen($body) > $max) {
    answer(413, ['ok' => false, 'error' => "Too big (at most $max bytes)"]);
}
if (strncmp($body, "\x1f\x8b", 2) === 0) {
    $body = @gzdecode($body, (int)$config['max_json_bytes'] + 1);
    if ($body === false || strlen($body) > (int)$config['max_json_bytes']) {
        answer(413, ['ok' => false, 'error' => 'Not gzip, or too big unzipped']);
    }
}
$upload = json_decode($body, true, 64);
if (!is_array($upload) || ($upload['format'] ?? '') !== 'viewtouch-upload/1' || !isset($upload['files']) || !is_array($upload['files'])) {
    answer(400, ['ok' => false, 'error' => 'Not a ViewTouch report upload (JSON)']);
}

$dir = vtr_data_dir() . '/' . $store;
$saved = 0;
$rejected = 0;
$storeName = '';
$checkPrefix = '';
foreach ($upload['files'] as $f) {
    if (!is_array($f) || !isset($f['report'], $f['day']) || !is_array($f['report']) || !is_array($f['day'])) {
        $rejected++;
        continue;
    }
    $id = is_string($f['report']['id'] ?? null) ? $f['report']['id'] : '';
    $date = is_string($f['day']['date'] ?? null) ? $f['day']['date'] : '';
    $day = $f['day']['id'] ?? null;
    if (($f['format'] ?? '') !== 'viewtouch-report/1' || !vtr_valid_report($id) || !vtr_valid_date($date)
        || !is_int($day) || $day < 0 || !isset($f['report']['rows']) || !is_array($f['report']['rows'])) {
        $rejected++;
        continue;
    }
    // A day kept under another date before (its date can move while it is open): only this one stays.
    foreach (glob("$dir/$id/*_$day.json") ?: [] as $old) {
        if (basename($old) !== "{$date}_$day.json") {
            @unlink($old);
        }
    }
    $f['receivedAt'] = date('c');
    if (!vtr_write_file("$dir/$id/{$date}_$day.json", json_encode($f, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE))) {
        answer(500, ['ok' => false, 'error' => 'Cannot write the reports (disk full, or the data folder not writable?)']);
    }
    $saved++;
    if (is_string($f['store']['name'] ?? null)) {
        $storeName = substr($f['store']['name'], 0, 100);
    }
    if (is_string($f['store']['checkPrefix'] ?? null)) {
        $checkPrefix = substr($f['store']['checkPrefix'], 0, 20);
    }
}
$status = vtr_status($store);
$status = array_merge($status, [
    'lastUpload' => time(),
    'lastFiles' => $saved,
    'storeName' => $storeName !== '' ? $storeName : ($status['storeName'] ?? ''),
    'checkPrefix' => $checkPrefix !== '' ? $checkPrefix : ($status['checkPrefix'] ?? ''),
]);
vtr_write_file("$dir/_status.json", json_encode($status, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE));
answer(200, ['ok' => true, 'store' => $store, 'saved' => $saved, 'rejected' => $rejected]);
