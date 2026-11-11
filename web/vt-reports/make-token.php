<?php
// ViewTouch web reporting: makes a store's upload token (each store's Pi sends with its own).
//   php make-token.php BHDAN        on the server (cPanel > Terminal, or SSH)
// or open make-token.php in the browser (it is behind the reports password) and type the store code.
// The token is shown once; only its SHA-256 is kept (tokens.json in the data folder). Making a new
// token for a store stops its old one.
define('VTR', 1);
require __DIR__ . '/lib.php';

$config = vtr_config();

if (PHP_SAPI === 'cli') {
    $store = strtoupper(trim((string)($argv[1] ?? '')));
    if (!vtr_valid_store($store)) {
        fwrite(STDERR, "Usage: php make-token.php <store code>   e.g. php make-token.php BHDAN\n"
                       . "(2 to 12 capital letters or digits)\n");
        exit(1);
    }
    if ($problem = vtr_check_data_dir()) {
        fwrite(STDERR, $problem . "\n");
        exit(1);
    }
    $token = vtr_make_token($store);
    echo "Token for $store (shown only now; the old one, if any, no longer works):\n\n  $token\n\n",
         "On the $store Pi:\n  vt-web-reports --setup https://<this website>/vt-reports/upload.php $token; beep3\n";
    exit(0);
}

vtr_headers();
if ((!empty($config['require_https']) && !vtr_is_https()) || (!empty($config['require_login']) && vtr_login_user() === '')) {
    http_response_code(403);
    echo '<!doctype html><meta charset="utf-8"><p>Open this page with https:// after the reports password is set up.</p>';
    exit;
}
$problem = vtr_check_data_dir();
$token = '';
$store = '';
$error = '';
if (!$problem && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST') {
    // Only from this page (another website can't make one in the owner's name).
    $origin = (string)($_SERVER['HTTP_ORIGIN'] ?? $_SERVER['HTTP_REFERER'] ?? '');
    $host = (string)($_SERVER['HTTP_HOST'] ?? '');
    $store = strtoupper(trim((string)($_POST['store'] ?? '')));
    if ($origin === '' || parse_url($origin, PHP_URL_HOST) !== parse_url('http://' . $host, PHP_URL_HOST)) {
        $error = 'Please use the form on this page.';
    } elseif (!vtr_valid_store($store)) {
        $error = 'A store code is 2 to 12 capital letters or digits, like BHDAN.';
    } else {
        $token = vtr_make_token($store);
    }
}
$upload = 'https://' . ($_SERVER['HTTP_HOST'] ?? 'your-website') . rtrim(dirname((string)($_SERVER['SCRIPT_NAME'] ?? '/vt-reports/x')), '/') . '/upload.php';
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>Store token · Store Reports</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="top"><h1>Store upload token</h1></header>
<main>
<?php if ($problem): ?>
  <section class="block"><p><?= h($problem) ?></p></section>
<?php elseif ($token !== ''): ?>
  <section class="block combined">
    <h3>Token for <?= h($store) ?></h3>
    <p class="sub">Shown only now. Its old token, if it had one, no longer works.</p>
    <p>On the <?= h($store) ?> Pi, paste:</p>
    <pre class="scroll"><code>vt-web-reports --setup <?= h($upload) ?> <?= h($token) ?>; beep3</code></pre>
  </section>
<?php else: ?>
  <section class="block">
<?php if ($error): ?>    <p class="warn"><?= h($error) ?></p>
<?php endif; ?>
    <form method="post" class="pick">
      <label for="store">Store code</label>
      <input id="store" name="store" required pattern="[A-Za-z0-9]{2,12}" placeholder="BHDAN">
      <button class="btn on" type="submit">Make its token</button>
    </form>
    <p class="sub">Each store's Pi gets its own token. A new one for a store stops its old one.</p>
  </section>
<?php endif; ?>
  <p><a class="btn" href="./">Back to the reports</a></p>
</main>
</body>
</html>
