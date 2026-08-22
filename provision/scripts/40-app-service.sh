#!/bin/sh
# App directory layout + OpenRC service. rsync is installed here for deploy-app.sh's
# ongoing binary/static-asset syncs.
set -eu
DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

mkdir -p /opt/temperingworks/bin /opt/temperingworks/static /opt/temperingworks/logs
chown -R temperingworks:temperingworks /opt/temperingworks

apk add --no-cache rsync

cp "$DIR/files/temperingworks-server.initd" /etc/init.d/temperingworks-server
chmod 755 /etc/init.d/temperingworks-server
cp "$DIR/files/temperingworks-server.confd" /etc/conf.d/temperingworks-server

rc-update add temperingworks-server default

# Left stopped (not failed) until deploy-app.sh syncs a real binary.
if [ -x /opt/temperingworks/bin/temperingworks-server ]; then
  rc-service temperingworks-server start
else
  echo "No binary at /opt/temperingworks/bin/temperingworks-server yet — service enabled but not started."
  echo "Run provision/deploy-app.sh once you have a build."
fi
