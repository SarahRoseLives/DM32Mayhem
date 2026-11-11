#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the dm32mayhem.com server. The flasher site is embedded in the binary, so this is the only artifact to deploy.
#   web/tools/build.sh                 build build/dm32mayhem-web for linux/amd64
#   GOARCH=arm64 web/tools/build.sh    another architecture
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
CGO_ENABLED=0 GOOS=${GOOS:-linux} GOARCH=${GOARCH:-amd64} \
	go build -trimpath -ldflags '-s -w' -o build/dm32mayhem-web .
ls -lh build/dm32mayhem-web
