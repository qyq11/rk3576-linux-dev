set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
break QSignalSpy::qt_metacall
run
printf "\nINTENTIONAL_QSIGNALSPY_DIRECT_RECEIVER_BREAKPOINT\n"
info registers rdi
thread apply all bt full
info threads
python
import gdb,json,os
row={'intentional_breakpoint':True,'selected_thread_number':gdb.selected_thread().global_num,'main_thread_number':1,'original_crash_reproduced':False}
for expression in ['(void*)QThread::currentThread()','(void*)((QObject*)$rdi)->thread()']:
    try: row[expression]=gdb.execute('print '+expression,to_string=True)
    except gdb.error as error: row[expression]='unavailable: '+str(error)
with open(os.environ['SN100_GDB_RECEIPT'],'w') as stream:json.dump(row,stream,indent=2)
end
