#!/usr/bin/env bash
# The Claude Label Utility is now the ViewTouch Label Utility (vt_label_utility.py).
# Kept so the old command still works; it runs the new installer.
exec bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/install_label_utility.sh" "$@"
