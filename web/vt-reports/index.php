<?php
// ViewTouch web reporting: the reports page. Behind the website's password (Apache Basic Auth over
// HTTPS: cPanel "Directory Privacy", or the .htaccess lines; see README.md), it shows each store's
// reports as the Reports screen does, sent by the store's Pi every 15 minutes (vt-web-reports).
// ?store=BHDAN|HCA|all  &report=sales...  &period=today|yesterday|week|lastweek|month|lastmonth|day|range
// &date=YYYY-MM-DD (day)  &from=&to= (range)  &csv=1 (download)
define('VTR', 1);
require __DIR__ . '/lib.php';

$config = vtr_config();

function stop_page(int $code, string $title, string $text): void
{
    http_response_code($code);
    vtr_headers();
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">',
        '<title>', h($title), '</title><link rel="stylesheet" href="style.css"></head><body><main class="stop"><h1>', h($title),
        '</h1><p>', h($text), '</p></main></body></html>';
    exit;
}

if (!empty($config['require_https']) && !vtr_is_https()) {
    stop_page(403, 'Use https://', 'This page only opens over a secure connection: put https:// in front of the address.');
}
if (!empty($config['require_login']) && vtr_login_user() === '') {
    stop_page(401, 'Password protection is off',
        'The reports are not shown until this folder has a password (cPanel > Directory Privacy, or the .htaccess lines). See README.md.');
}
if ($problem = vtr_check_data_dir()) {
    stop_page(500, 'Not set up yet', $problem);
}

$stores = vtr_stores();
$reports = vtr_reports();
$q = function (string $k, string $default = ''): string {
    $v = $_GET[$k] ?? $default;
    return is_string($v) ? trim($v) : $default;
};
$store = $q('store', 'all');
if ($store !== 'all' && !isset($stores[$store])) {
    $store = 'all';
}
$report = $q('report', 'sales');
if (!isset($reports[$report])) {
    $report = 'sales';
}
$period = $q('period', 'today');
if (!in_array($period, ['today', 'yesterday', 'week', 'lastweek', 'month', 'lastmonth', 'day', 'range'], true)) {
    $period = 'today';
}
[$fromD, $toD, $periodLabel] = vtr_period($period, $q('date'), $q('from'), $q('to'));
$dates = vtr_dates($fromD, $toD);
$single = count($dates) === 1;
[$label, , $additive] = $reports[$report];
$params = ['store' => $store, 'report' => $report, 'period' => $period];
if ($period === 'day') {
    $params['date'] = $dates[0];
} elseif ($period === 'range') {
    $params += ['from' => $dates[0], 'to' => end($dates)];
}
$link = function (array $over) use ($params): string {
    return '?' . http_build_query(array_filter(array_merge($params, $over), function ($v) { return $v !== null; }));
};

$storeName = function (string $code) use ($stores): string {
    $s = vtr_status($code);
    $name = trim((string)($s['storeName'] ?? ''));
    return $name !== '' && $name !== $code ? "$name ($code)" : ($stores[$code] ?? $code);
};

// What to show: blocks of [heading, subheading, report, open].
$show = $store === 'all' ? array_keys($stores) : [$store];
$blocks = [];
$everyFile = [];
$perStore = [];
foreach ($show as $code) {
    $files = [];
    foreach ($dates as $d) {
        foreach (vtr_load($code, $report, $d) as $f) {
            $files[] = $f;
        }
    }
    $perStore[$code] = $files;
    $everyFile = array_merge($everyFile, $files);
}
$sendsFrom = array_filter($perStore);
if ($store === 'all' && $additive && count($sendsFrom) > 1) {
    $blocks[] = ['All stores together', count($everyFile) . ' report' . (count($everyFile) === 1 ? '' : 's') . ' added up (shares and averages left blank)',
                 vtr_combine($everyFile), true, 'combined'];
}
foreach ($perStore as $code => $files) {
    if (!$files) {
        $blocks[] = [$storeName($code), 'No ' . $label . ' report for ' . ($single ? vtr_nice_date($dates[0]) : 'these days')
                     . ' yet.', null, true, 'empty'];
    } elseif (count($files) === 1) {
        $blocks[] = [$storeName($code), vtr_day_line($files[0]), $files[0]['report'], true, ''];
    } else {
        if ($additive) {
            $blocks[] = [$storeName($code), count($files) . ' business days added up (shares and averages left blank)', vtr_combine($files), true, 'combined'];
        } else {
            $blocks[] = [$storeName($code), count($files) . ' business days, each below', null, true, 'empty'];
        }
        foreach ($files as $f) {
            $blocks[] = [$storeName($code), vtr_day_line($f), $f['report'], !$additive, 'day'];
        }
    }
}
$periodText = $single ? vtr_nice_date($dates[0])
                      : (new DateTimeImmutable($dates[0]))->format('M j') . ' – ' . $toD->format('M j, Y');
if ($periodLabel !== '') {
    $periodText = $periodLabel . ' · ' . $periodText;
}

// --- CSV --------------------------------------------------------------------------------------
if ($q('csv') === '1') {
    vtr_headers('text/csv; charset=utf-8');
    $name = 'viewtouch-' . $report . '-' . strtolower($store) . '-' . $dates[0] . ($single ? '' : '_' . end($dates)) . '.csv';
    header('Content-Disposition: attachment; filename="' . $name . '"');
    $out = fopen('php://output', 'w');
    $put = function (array $cells) use ($out) {
        fputcsv($out, array_map(function ($c) { return vtr_csv_cell((string)$c); }, $cells), ',', '"', '\\');
    };
    $put([$label, $periodText]);
    foreach ($blocks as [$heading, $sub, $rep]) {
        $put([]);
        $put([$heading, $sub]);
        if (!$rep) {
            continue;
        }
        if (array_filter((array)($rep['columns'] ?? []))) {
            $put((array)$rep['columns']);
        }
        foreach ((array)$rep['rows'] as $row) {
            $put((array)($row['cells'] ?? []));
        }
    }
    fclose($out);
    exit;
}

// --- the page ---------------------------------------------------------------------------------
vtr_headers();
$now = time();
$stale = (int)$config['stale_minutes'] * 60;
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title><?= h($label) ?> · Store Reports</title>
<link rel="stylesheet" href="style.css">
<script src="app.js" defer></script>
</head>
<body>
<header class="top">
  <h1>Store Reports</h1>
  <ul class="updated">
<?php foreach ($stores as $code => $name):
    $s = vtr_status($code);
    $last = (int)($s['lastUpload'] ?? 0);
    $late = $last === 0 || $now - $last > $stale; ?>
    <li class="<?= $late ? 'late' : 'ok' ?>"><strong><?= h($code) ?></strong>
      <?php if ($last === 0): ?>has never sent its reports
      <?php else: ?>last updated <?= h(date('M j, g:i A', $last)) ?> (<?= h(vtr_ago($now - $last)) ?>)<?php endif; ?>
      <?php if ($late && $last !== 0): ?><span class="warn">⚠ hasn't sent in over <?= (int)$config['stale_minutes'] ?> minutes: is its Pi on and online?</span><?php endif; ?>
    </li>
<?php endforeach; ?>
  </ul>
</header>

<nav class="controls" aria-label="Choose the report">
  <div class="buttons stores">
<?php foreach (array_merge(array_keys($stores), ['all']) as $code): ?>
    <a class="btn<?= $code === $store ? ' on' : '' ?>" href="<?= h($link(['store' => $code])) ?>"><?= h($code === 'all' ? 'All stores' : $stores[$code]) ?></a>
<?php endforeach; ?>
  </div>

  <form method="get" class="pick" id="pick">
    <input type="hidden" name="store" value="<?= h($store) ?>">
<?php foreach (['period', 'date', 'from', 'to'] as $k): if (isset($params[$k])): ?>
    <input type="hidden" name="<?= h($k) ?>" value="<?= h($params[$k]) ?>">
<?php endif; endforeach; ?>
    <label for="report">Report</label>
    <select name="report" id="report" class="autosubmit">
<?php $group = null; foreach ($reports as $id => [$name, $g]):
    if ($g !== $group): if ($group !== null): ?>      </optgroup>
<?php endif; $group = $g; ?>      <optgroup label="<?= h($g) ?>">
<?php endif; ?>
        <option value="<?= h($id) ?>"<?= $id === $report ? ' selected' : '' ?>><?= h($name) ?></option>
<?php endforeach; ?>      </optgroup>
    </select>
    <noscript><button class="btn" type="submit">Show</button></noscript>
  </form>

  <div class="buttons periods">
<?php foreach (['today' => 'Today', 'yesterday' => 'Yesterday', 'week' => 'This week', 'lastweek' => 'Last week',
               'month' => 'This month', 'lastmonth' => 'Last month'] as $p => $name): ?>
    <a class="btn<?= $p === $period ? ' on' : '' ?>" href="<?= h($link(['period' => $p, 'date' => null, 'from' => null, 'to' => null])) ?>"><?= h($name) ?></a>
<?php endforeach; ?>
  </div>

  <form method="get" class="dates">
    <input type="hidden" name="store" value="<?= h($store) ?>">
    <input type="hidden" name="report" value="<?= h($report) ?>">
    <input type="hidden" name="period" value="range">
    <label>From <input type="date" name="from" value="<?= h($dates[0]) ?>" required></label>
    <label>To <input type="date" name="to" value="<?= h(end($dates)) ?>" required></label>
    <button class="btn" type="submit">Show these days</button>
  </form>
</nav>

<main>
  <div class="title">
    <h2><?= h($label) ?> <span class="period"><?= h($periodText) ?></span></h2>
    <div class="buttons actions">
<?php if ($single): $d = new DateTimeImmutable($dates[0]); ?>
      <a class="btn" href="<?= h($link(['period' => 'day', 'date' => $d->modify('-1 day')->format('Y-m-d'), 'from' => null, 'to' => null])) ?>">◀ Day before</a>
      <a class="btn" href="<?= h($link(['period' => 'day', 'date' => $d->modify('+1 day')->format('Y-m-d'), 'from' => null, 'to' => null])) ?>">Day after ▶</a>
<?php endif; ?>
      <button class="btn print" type="button">Print</button>
      <a class="btn" href="<?= h($link(['csv' => '1'])) ?>">Download CSV</a>
    </div>
  </div>
<?php if (!$additive && !$single): ?>
  <p class="hint">This report isn't added up over several days: each business day is shown on its own.</p>
<?php endif; ?>

<?php foreach ($blocks as [$heading, $sub, $rep, $open, $kind]):
    if ($kind === 'day' && !$single): ?>
  <details class="block day"<?= $open ? ' open' : '' ?>>
    <summary><strong><?= h($heading) ?></strong> · <?= h($sub) ?></summary>
    <?= vtr_table($rep) ?>
  </details>
<?php else: ?>
  <section class="block <?= h($kind) ?>">
    <h3><?= h($heading) ?></h3>
    <p class="sub"><?= h($sub) ?></p>
<?php if ($rep): ?>
    <?= vtr_table($rep) ?>
<?php endif; ?>
  </section>
<?php endif; endforeach; ?>
  <p class="foot">The same reports as the Reports screen at each store (End of Day's for a closed day; the day still open as of its last update). Gift card PINs and card numbers are never sent here.</p>
</main>
</body>
</html>
