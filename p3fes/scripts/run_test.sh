#!/bin/bash
# usage: run_test.sh <name> <seconds> <shot-times (space separated)> ; env passes through (PS2X_PAD_SCRIPT etc.)
name=$1; secs=$2; shift 2; shots="$*"
cd /home/claude/p3/game
pkill -f "Xvfb :99" 2>/dev/null; sleep 0.5
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:99 PS2X_CALLBACK_STACK_TOP=${PS2X_CALLBACK_STACK_TOP:-0x100000} PS2X_HIDE_DEBUG_UI=${PS2X_HIDE_DEBUG_UI:-1}
/home/claude/p3/PS2Recomp/build-linux/ps2xRuntime/ps2EntryRunner /home/claude/p3/game/SLUS_216.21 > /home/claude/p3/game/$name.log 2>&1 &
RPID=$!
start=$(date +%s)
for t in $shots; do
  while [ $(( $(date +%s) - start )) -lt $t ]; do sleep 1; done
  import -window root /home/claude/p3/shots/${name}_$t.png 2>/dev/null
done
while [ $(( $(date +%s) - start )) -lt $secs ]; do sleep 1; kill -0 $RPID 2>/dev/null || break; done
kill $RPID 2>/dev/null; sleep 1; kill -9 $RPID 2>/dev/null
kill $XPID 2>/dev/null
echo done
