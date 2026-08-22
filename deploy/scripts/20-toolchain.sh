#!/bin/sh
# Native aarch64/musl build tools — on-box compilation avoids the musl/aarch64
# cross-toolchain pain of building from a non-ARM dev machine.
set -eu

apk add --no-cache build-base cmake git
