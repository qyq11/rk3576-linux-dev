#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
printf 'ORIGINAL_CHANNEL_COUNTS\n'
runuser -u lckfb -- sqlite3 -readonly "$base/runtime/data/db/systemInfo/channel.db" 'SELECT count(*),min(channel_num),max(channel_num) FROM test_channel; SELECT count(*) FROM warm_channel; SELECT count(*) FROM cache_channel;'
printf 'DIRECTORY_WRITE_PERMISSIONS\n'
runuser -u lckfb -- sh -c 'for p in /home/rkshare/programs/sn100_shell /home/rkshare/programs/sn100_shell/runtime/data /home/rkshare/programs/sn100_shell/runtime/data/db/userSet /home/rkshare/programs/sn100_shell/runtime/data/result /home/rkshare/programs/sn100_shell/runtime/data/project; do test -w "$p"; printf "WRITABLE=%s\n" "$p"; done'
printf 'SAMPLE_CONFIG_PERMISSION\n'
runuser -u lckfb -- sh -c 'test -r /home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini; test -r /userdata/shared/theme_config.ini'
printf 'CHANNEL_AND_PERMISSIONS_READONLY_COMPLETE=1\n'
