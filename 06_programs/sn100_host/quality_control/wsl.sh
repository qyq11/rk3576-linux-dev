#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
directory=$root/temp/sn100_host/qc_local_20261025/page/wsl_release
mkdir -p "$directory"
cd "$directory"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$module_dir/quality_control.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
file "$directory/sn100_qc_local"
