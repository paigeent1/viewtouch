#!/usr/bin/env bash
# Turns on the repository's Git hooks (.githooks/) in this clone, so every commit must be logged in
# modern/vt-modern-development-notes.md. Run once per clone (Pi, laptops, AI workspaces).
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
git config core.hooksPath .githooks
chmod +x .githooks/* 2>/dev/null || true
echo "Git hooks enabled: commits now require an entry in modern/vt-modern-development-notes.md"
