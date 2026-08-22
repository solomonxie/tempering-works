#!/bin/sh
# Base OS setup: packages, timezone, app user/group, swapfile, sshd hardening.
set -eu
DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

apk update
apk upgrade
apk add --no-cache curl vim htop tzdata

ln -sf /usr/share/zoneinfo/UTC /etc/localtime

getent group temperingworks >/dev/null 2>&1 || addgroup -S temperingworks
id -u temperingworks >/dev/null 2>&1 || adduser -S -D -H -s /sbin/nologin -G temperingworks temperingworks

# t4g.small has 2GB RAM; on-box `make -j$(nproc)` C++ builds can spike memory, and
# Alpine provisions no swap by default.
if [ ! -f /swapfile ]; then
  dd if=/dev/zero of=/swapfile bs=1M count=1024
  chmod 600 /swapfile
  mkswap /swapfile
  swapon /swapfile
  grep -q '^/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

# See files/sshd_config.orig for the shipped default this replaces. Safe to run over the
# connection it's replacing: root login stays allowed, only the password fallback goes away.
cp "$DIR/files/sshd_config" /etc/ssh/sshd_config
sshd -t
rc-service sshd restart
