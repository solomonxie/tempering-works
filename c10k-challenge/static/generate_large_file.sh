#!/bin/sh
# Generates static/large.bin (~5MB), the large-response workload target.
# Not committed to git -- run this once locally before pointing
# throughput.cpp/mixed_workload.cpp at /large.bin.
#
#   $ c10k-challenge/static/generate_large_file.sh

set -e
cd "$(dirname "$0")"
dd if=/dev/urandom of=large.bin bs=1048576 count=5
echo "wrote $(wc -c < large.bin) bytes to $(pwd)/large.bin"
