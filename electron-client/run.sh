#!/usr/bin/env bash
# Скрипт запуска Electron-клиента Wiretalk

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Поиск доступного бинарника electron в системе
ELECTRON_BIN=""
for bin in electron43 electron42 electron; do
    if command -v "$bin" >/dev/null 2>&1; then
        ELECTRON_BIN="$bin"
        break
    fi
done

if [ -z "$ELECTRON_BIN" ]; then
    echo "Ошибка: Electron не найден в системе (попробуйте: sudo pacman -S electron43 или yay -S electron)"
    exit 1
fi

echo "Запуск Wiretalk Electron клиента через $ELECTRON_BIN..."
exec "$ELECTRON_BIN" "$DIR" "$@"
