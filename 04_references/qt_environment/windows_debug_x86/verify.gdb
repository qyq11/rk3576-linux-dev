set pagination off
set confirm off
set print pretty on
break main
run --timeout 10 --state state.json --snapshot render.png
printf "\nDEBUG_BREAKPOINT_REACHED\n"
print argc
bt
next
printf "\nDEBUG_SINGLE_STEP_COMPLETE\n"
continue
quit