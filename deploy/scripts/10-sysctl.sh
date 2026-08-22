#!/bin/sh
# C10K sysctl tuning for a high-concurrency epoll TCP/HTTP server.
set -eu
DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

cp "$DIR/files/99-temperingworks-c10k.conf" /etc/sysctl.d/99-temperingworks-c10k.conf
sysctl --system
