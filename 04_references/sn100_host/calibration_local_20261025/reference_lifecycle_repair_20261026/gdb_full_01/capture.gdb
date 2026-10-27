set pagination off
set confirm off
set print thread-events off
set print pretty on
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
python
import gdb,json,os
receipt={'events':[],'inferior_exit_code':None,'inferior_stop_signal':None}
def preserve():
    with open(os.environ['SN100_GDB_RECEIPT'],'w') as stream:
        json.dump(receipt,stream,indent=2)
def stopped(event):
    if isinstance(event,gdb.SignalEvent):
        receipt['inferior_stop_signal']=event.stop_signal
        receipt['events'].append({'event':'signal','signal':event.stop_signal})
        preserve()
def exited(event):
    receipt['inferior_exit_code']=getattr(event,'exit_code',None)
    receipt['events'].append({'event':'exited','exit_code':getattr(event,'exit_code',None)})
    preserve()
gdb.events.stop.connect(stopped)
gdb.events.exited.connect(exited)
preserve()
end
run
thread apply all bt full
info threads
python
preserve()
end
