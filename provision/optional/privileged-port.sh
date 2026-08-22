#!/bin/sh
# Opt-in, run manually (not part of deploy.sh's default sequence) once ready to serve on
# the standard HTTP port 80 instead of 8080. Grants the binary cap_net_bind_service
# instead of running the service as root.
set -eu
BIN=/opt/temperingworks/bin/temperingworks-server

[ -x "$BIN" ] || { echo "no binary at $BIN yet — run deploy-app.sh first" >&2; exit 1; }

apk add --no-cache libcap
setcap cap_net_bind_service+ep "$BIN"
sed -i 's/^APP_PORT=.*/APP_PORT=80/' /etc/conf.d/temperingworks-server
rc-service temperingworks-server restart

echo "Remember to also open port 80 in terraform's security group (app_http_port) and re-apply."
