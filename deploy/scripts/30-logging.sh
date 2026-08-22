#!/bin/sh
# Alpine ships neither logrotate nor a cron daemon by default; without dcron running,
# logrotate.d entries never get invoked.
set -eu
DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

apk add --no-cache logrotate dcron
rc-update add dcron
rc-service dcron start

cp "$DIR/files/temperingworks-server.logrotate" /etc/logrotate.d/temperingworks-server
