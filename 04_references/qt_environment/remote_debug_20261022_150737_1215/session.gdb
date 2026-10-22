set pagination off
set confirm off
set debuginfod enabled off
set print pretty on
set auto-solib-add off
set sysroot /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/qt_demo/sysroot
set solib-search-path /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/qt_demo/sysroot/lib/aarch64-linux-gnu:/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/qt_demo/sysroot/usr/lib/aarch64-linux-gnu
file /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/qt_demo/build/qt_demo
target remote | ssh -T -o BatchMode=yes -o ConnectTimeout=6 lckfb@192.168.100.123 bash /home/rkshare/programs/qt_demo/debug_runs/20261022_150737_1215/server.sh 10
break main
continue
sharedlibrary Qt5
printf "\nRK_REMOTE_BREAKPOINT_REACHED\n"
print argc
bt
next
printf "\nRK_REMOTE_SINGLE_STEP_COMPLETE\n"
shell ssh -T -o BatchMode=yes lckfb@192.168.100.123 ps -C gdbserver,qt_demo -o comm=,rss=,pcpu=,stat=
continue
quit
