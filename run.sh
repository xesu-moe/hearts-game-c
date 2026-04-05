#!/bin/bash
# Launch all three Hollow Hearts binaries (lobby, server, client)
# Ctrl+C kills all three

trap 'kill 0' EXIT

./hh-lobby 7778 &
sleep 0.3
./hh-server 7777 127.0.0.1 7778 &
sleep 0.3
./hollow-hearts --lobby-addr 127.0.0.1 --lobby-port 7778 &

wait
