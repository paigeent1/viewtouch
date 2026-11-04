# Site starting points

Each Pi keeps its own hardware settings in `~/.config/viewtouch/` (not in Git): `site.conf` and
`tablets` (`vt-site`), `tvs.json` (`vt-directv`). Files here are starting points to copy, nothing reads them.

- `hca-tvs.json`: Hat City Ale House's DIRECTV receivers, with the keys the DIRECTV pages' buttons use
  (front, bar1, bar2, box72, vip). At HCA: `cp ~/viewtouch-main/scripts/sites/hca-tvs.json ~/.config/viewtouch/tvs.json`,
  then `vt-directv scan --save` adds any smart TVs.
