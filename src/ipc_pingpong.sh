#!/bin/sh
# $Id$
# ipc_pingpong.sh - wrapper script for ipc_ping
# This starts multiple processes running ipc_ping with (presumably)
# different strings to spit out. The entire output can be redirected.
# To use, copy this script to the same directory as ipc_ping and 
# run it with two or more distinct strings as arguments. Note that
# ipc_ping will remain running in the background until it is finished
# taking turns.
# Example usages:
# ./ipc_pingpong.sh p0ng p1ng p2ng p3ng p4ng p5ng p6ng p7ng > /tmp/foo.log
# ./ipc_pingpong.sh ping pong
# ./ipc_pingpong.sh one two three four five six seven eight nine ten


[ "${DELAY}" ] || DELAY=5
[ "${TURNS}" ] || TURNS=20

[ "$2" ] || { echo "At least two args must be specified"; exit 1; }

echo "Starting ping pong test for ${TURNS} turns with delay of ${DELAY}ms"
MYDIR=$(dirname $0)
[ -x ${MYDIR}/ipc_ping ] || { echo "${MYDIR}/ipc_ping not found"; exit 1; }

while [ "$1" ]
do
  ${MYDIR}/ipc_ping $1 ${DELAY} ${TURNS} &
  shift
done

