#!/usr/bin/env bash
# Удобный скрипт запуска Electron-клиента из корня cpp-messenger

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
exec "$DIR/electron-client/run.sh" "$@"
