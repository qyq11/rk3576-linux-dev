#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink "$base/current") == releases/ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14 ]]
[[ $(sha256sum "$base/runtime/data/theme_config.ini" | cut -d' ' -f1) == 253324ec8bf648b128f1a085f3587b318018cb56eb0e0569c71408fe11cfbd76 ]]
[[ $(sha256sum "$base/runtime/data/config.ini" | cut -d' ' -f1) == 403348a29b6dafddf4f0511dfd4b8f33277bd56a30b7408e1b5f6e2f54de078e ]]
[[ $(sha256sum /userdata/shared/theme_config.ini | cut -d' ' -f1) == 029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7 ]]
[[ ! -e $base/instrument_configuration_file && ! -e $base/theme_configuration_file ]]
install -o lckfb -g lckfb -m 644 /userdata/shared/theme_config.ini "$base/runtime/data/theme_config.ini"
runuser -u lckfb -- bash -c 'set -Ce; printf "%s\n" /home/rkshare/programs/sn100_shell/runtime/data/config.ini > /home/rkshare/programs/sn100_shell/instrument_configuration_file; printf "%s\n" /home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini > /home/rkshare/programs/sn100_shell/theme_configuration_file'
sha256sum "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini" "$base/runtime/data/theme_config.ini" /userdata/shared/config_linux.ini /userdata/shared/consume.ini /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini
sha256sum "$base/original_project_root" "$base/instrument_configuration_file" "$base/theme_configuration_file"
printf 'BOARD_A_CONFIGURATION_PREPARED_ORIGINAL_CONNECTION_UNCHANGED=1\n'
