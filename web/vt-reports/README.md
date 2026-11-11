# Store reports on the website (vt-reports)

A password-protected page on the store's website that shows every ViewTouch report (the same ones as
Manager → Reports) for each store, updated every 15 minutes. One login sees every store.

How it works: the website can't reach into the stores (they sit behind their routers), so each
store's Pi **sends** its reports to the website every 15 minutes (`vt-web-reports`). The website keeps
them as files in a folder that is **not** part of the website, and shows them on `index.php` behind a
password.

- **Plain web hosting is enough:** Apache with PHP 7.4 or newer (InMotion's cPanel has both) and SSL
  (https). No database, nothing from other websites.
- **What's in the folder:**
  - `index.php`: the reports page.
  - `upload.php`: where the Pis send. It isn't behind the password; each store has a token of its own instead.
  - `make-token.php`: makes a store's token.
  - `config.php`: the settings. No secrets go in it.
  - `lib.php`, `style.css`, `app.js`, `.htaccess`: the rest of the page.
  - `tests/`: tests, for the developers. You can leave it out.
- **Nothing private is stored in the folder:**
  - The store tokens are kept only as fingerprints (hashes), in the data folder.
  - The password is kept in the password file, outside `public_html`.
  - Gift card PINs, card numbers and ID details never leave the Pi.

The steps below use **hatcityale.com** as the main site. The Pis send to it, and
**buttheadstobacco.com** shows the same reports. Do them once.

---

## 1. Put the folder on the website (cPanel File Manager)

1. On your computer, open https://github.com/paigeent1/viewtouch, then **Code → Download ZIP**, and unzip it.
   The folder you need is `viewtouch-main/web/vt-reports`.
2. Make a zip of just that `vt-reports` folder (right-click → *Compress* / *Send to → Compressed folder*).
3. Log in to cPanel for hatcityale.com. Open **Files → File Manager**, then click **Settings** (top right),
   tick **Show Hidden Files (dotfiles)** and **Save**.
4. Open `public_html`. Click **Upload** and upload `vt-reports.zip`.
5. Back in `public_html`, right-click `vt-reports.zip`, then **Extract** to `/public_html`. You now have
   `public_html/vt-reports/` with `index.php`, `.htaccess` and the rest in it. Delete the zip.

(With SFTP instead, such as FileZilla: copy the `vt-reports` folder into `public_html`, hidden `.htaccess` included.)

## 2. Make the data folder (outside the website)

1. In File Manager, go to your **home folder**: the top of the tree, `/home/<your cPanel user>`. It is
   above `public_html`, **not** inside it.
2. Click **+ Folder** and name it `vt-reports-data`.
3. Right-click it, then **Change Permissions**, and set it to **700**.

That's the default place `config.php` looks: `/home/<cPanel user>/vt-reports-data`. If you put it
anywhere else, open `public_html/vt-reports/config.php` (right-click → **Edit**) and set the line
`'data_dir' => '/home/<cPanel user>/somewhere-else',` (take the `//` off its front). The page refuses
to run if the data folder is inside `public_html`.

## 3. Put the password on the page

**The easy way: cPanel Directory Privacy.**

1. In cPanel, open **Files → Directory Privacy**.
2. Click through `public_html` to `vt-reports`, then **Edit**.
3. Tick **Password protect this directory**, type the name *Store Reports*, and **Save**.
4. Under *Create User*, type a login name and a strong password, then **Save**. That one login sees both stores.

cPanel keeps the password file outside `public_html` and adds its lines to `vt-reports/.htaccess`. The
`.htaccess` that came with the folder already lets the Pis' uploads (`upload.php`) through without
the password: they use their token instead.

**Or by hand** (if you have SSH or cPanel → Terminal):

```
mkdir -p ~/.htpasswds && htpasswd -c ~/.htpasswds/vt-reports owner
```

Then edit `public_html/vt-reports/.htaccess`: take the `#` off the four lines under *The password*,
and put your cPanel user name in the `AuthUserFile` line.

Check it: open https://hatcityale.com/vt-reports/ and it should ask for the login. The page also
refuses to show reports if the password isn't on. It says *Password protection is off*.

Rarely, a server asks for the login but doesn't tell PHP who logged in, so the page still says
*Password protection is off*. Only in that case, add `'require_login' => false,` to `config.php`. The
login prompt still protects the page.

## 4. Make a token for each store

1. Open https://hatcityale.com/vt-reports/make-token.php and log in.
2. Type `BHDAN` and touch **Make its token**.
3. It shows the exact command for that store's Pi. Copy it somewhere safe for the next step.
4. Do the same for `HCA`.

A token is shown only once. If one is lost, make a new one for that store: the old one stops working.
In cPanel → Terminal you can also run `cd ~/public_html/vt-reports && php make-token.php BHDAN`.

## 5. On each store's Pi: one command

First make sure the Pi has the ViewTouch update with `--export-reports`:

```
cd ~/viewtouch-main && git pull --ff-only myfork main && vt-server; beep3
```

Then paste the command the token page gave, on that store's Pi (BHDAN's token on BHDAN, HCA's on HCA):

```
vt-web-reports --setup https://hatcityale.com/vt-reports/upload.php BHDAN-xxxxxxxx; beep3
```

It saves the address and token on the Pi only, in a file only you can read. Then it starts sending
every 15 minutes, sends the last 60 days right away, and says *It works*.
`vt-web-reports --status; beep3` shows when it last worked.

## 6. Open the reports

https://hatcityale.com/vt-reports/ shows the reports. At the top you see each store's *last updated*
time. A store that hasn't sent for over 30 minutes gets a warning.

---

## The same reports on buttheadstobacco.com

**When both websites are on the same InMotion account** (one cPanel), use the same data folder. The Pis
send to hatcityale.com only.

1. Do step 1 again for buttheadstobacco.com: upload and extract into **its** folder. cPanel → Domains
   shows where that is, for example `/home/<cPanel user>/buttheadstobacco.com` or
   `public_html/buttheadstobacco.com`.
2. Edit **that** copy's `config.php` and set
   `'data_dir' => '/home/<cPanel user>/vt-reports-data',`: the same folder as step 2.
3. Do step 3 (Directory Privacy) on that `vt-reports` folder too, with the same login name and password.

Nothing else is needed. Both addresses show the same reports.

**When the two websites are on different accounts or servers**, each website gets its own copy:

1. Do steps 1 to 4 on each website. Each has its own data folder and its own tokens.
2. On each Pi, set up the first website as in step 5, then add the second with that website's token:

   ```
   vt-web-reports --add https://buttheadstobacco.com/vt-reports/upload.php BHDAN-yyyyyyyy; beep3
   ```

## Settings (config.php)

| Setting | Default | What it does |
|---|---|---|
| `data_dir` | `/home/<cPanel user>/vt-reports-data` | Where the reports and token fingerprints are kept (outside `public_html`). |
| `stores` | `BHDAN`, `HCA` | The store buttons, in order. A store that sends reports shows up even if it isn't listed here. |
| `timezone` | `America/New_York` | The stores' clock. |
| `week_starts` | `0` (Sunday) | For *This week* and *Last week*, as Store Settings' pay week. |
| `stale_minutes` | `30` | When a store's *last updated* turns into a warning. |

## Good to know

- **Numbers match the register.** A closed day shows the reports saved at End of Day. The day still
  open shows them as of the last update.
- **A business day is dated by its End of Day**, as on the Pi: a day closed at 2 AM Saturday is
  Saturday's. The day still open shows under today's date until its End of Day.
- **Several days, or All stores:** these reports are added up: Sales, Items, Categories, By Hour,
  Servers, By Employee, Tips, Expenses, Deposit, Royalty and Accounting. Shares (%) and averages are
  left blank in a total. The other reports (Time Clock, Labor, Drawer, Audit, Gift Cards…) show each
  day on its own.
- **Not sent:** the Forecast, which looks ahead. See it at the store.
- **If a Pi is off or offline,** nothing is lost. It sends again on the next run, and the last 60 days go again every night.
- **Reports are kept on the website** until you delete them. Each store's folder is
  `vt-reports-data/<store>/<report>/<date>_<day>.json`.

## For developers

- Test on any computer with PHP: `bash web/vt-reports/tests/run.sh`. It runs the site under `php -S`
  with made-up reports and also tests `scripts/bin/vt-web-reports` with a stand-in for `vtmodern`.
- On the Pi, `vtmodern --export-reports <dir> [--from YYYY-MM-DD] [--to YYYY-MM-DD]` writes the files
  (`modern/storage/report_export.cpp`).
  - One upload is `{"format": "viewtouch-upload/1", "files": [ ... ]}`, gzipped, with the `X-VT-Token` header.
  - Each file is `{"format": "viewtouch-report/1", "report": {id, title, subtitle, columns, rows: [{kind, cells}]}, "store": {name, checkPrefix}, "day": {id, date, open, openedAt, closedAt, ...}, "generatedAt": ...}`.
