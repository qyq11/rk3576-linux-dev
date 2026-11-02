#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
name=$1; [[ $name =~ ^[a-z0-9_]+$ ]]
result=$here/$name; mkdir -p "$result"
if [[ -f $result/remote_path ]]; then run=$(cat "$result/remote_path"); else cp "$here/remote_path" "$result/remote_path"; run=$(cat "$here/remote_path"); fi
[[ $run =~ ^/tmp/sn100-loopback-20261030\.[a-zA-Z0-9]{6}$ ]]
host=lckfb@192.168.100.123; opts=(-o BatchMode=yes -o ConnectTimeout=8)
for file in app.log runtime.txt dependencies.txt arguments.txt resources.tsv kms.json product_pid exit_code product_sockets.txt runner.sh; do
    if ssh -T "${opts[@]}" "$host" "test -f '$run/$file'"; then
        scp -B "${opts[@]}" "$host:$run/$file" "$result/$file"
    else printf '%s\n' "$file" >> "$result/not_created.txt"; fi
done
