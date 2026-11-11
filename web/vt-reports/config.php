<?php
// ViewTouch web reporting: this website's settings. No passwords or tokens go here (the store
// tokens are kept, as hashes, in the data folder; the page's password in the .htpasswd file).
// Anything left out takes the default shown in lib.php (vtr_config).
if (!defined('VTR')) {
    http_response_code(404);
    exit;
}
return [
    // Where the stores' reports and tokens are kept: a folder OUTSIDE public_html. Left out, it is
    // vt-reports-data in the account's home folder (e.g. /home/<cPanel user>/vt-reports-data).
    // Two websites on the same account show the same reports when both name the same folder:
    // 'data_dir' => '/home/<cPanel user>/vt-reports-data',

    // The stores, in the order of the buttons: code (as in its token) => name on the button.
    'stores' => ['BHDAN' => 'BHDAN', 'HCA' => 'HCA'],

    'timezone' => 'America/New_York',
    'week_starts' => 0,       // 0 = Sunday (as Store Settings' week)
    'stale_minutes' => 30,    // "hasn't sent" warning after this many minutes
];
