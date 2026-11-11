// ViewTouch web reporting: a new report as soon as it is picked, and the Print button.
// The page works without it (a Show button appears instead).
document.addEventListener('DOMContentLoaded', function () {
  document.querySelectorAll('select.autosubmit').forEach(function (s) {
    s.addEventListener('change', function () { s.form.submit(); });
  });
  document.querySelectorAll('button.print').forEach(function (b) {
    b.addEventListener('click', function () { window.print(); });
  });
});
