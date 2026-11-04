#!/usr/bin/env bash
# 只读Qt导出状态的指针和容器长度；不附加调试器、不暂停线程或调用应用函数。
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(awk '{print $22}' /proc/5836/stat) == 225261 ]]
library=/lib/aarch64-linux-gnu/libQt5Widgets.so.5
mapping=$(awk '$3=="00000000" && $NF ~ /libQt5Widgets.so/ {split($1,a,"-");print "0x"a[1];exit}' /proc/5836/maps)
[[ $mapping == 0x* ]]
symbol=$(nm -D "$library" | awk '$3 ~ /^_ZN19QApplicationPrivate12popupWidgetsE/ {print "0x"$1;exit}')
[[ $symbol == 0x* ]]
address=$((mapping+symbol))
list=$(od -An -tu8 -j "$address" -N 8 /proc/5836/mem | tr -d ' ')
printf 'QT_POPUP_LIST_POINTER_NONZERO=%s\n' "$([[ $list != 0 ]] && printf 1 || printf 0)"
if [[ $list != 0 ]];then
    data=$(od -An -tu8 -j "$list" -N 8 /proc/5836/mem | tr -d ' ')
    if [[ $data != 0 ]];then
        values=$(od -An -tu4 -j "$((data+8))" -N 8 /proc/5836/mem)
        read -r begin end <<< "$values"
        [[ $begin =~ ^[0-9]+$ && $end =~ ^[0-9]+$ && $end -ge $begin && $((end-begin)) -le 64 ]]
        printf 'QT_ACTIVE_POPUP_COUNT=%s\n' "$((end-begin))"
    fi
fi
printf 'QT_GUI_MODAL_EXPORTED_SYMBOLS\n'
nm -D -C /lib/aarch64-linux-gnu/libQt5Gui.so.5 2>/dev/null | grep -E 'QGuiApplicationPrivate::(modalWindowList|focus_window|mousePressWindow|currentMouseWindow)' || true
printf 'QT_STATE_READ_ONLY_NO_ATTACH_NO_FUNCTION_CALL=1\n'
