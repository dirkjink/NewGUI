#!/usr/bin/env bash
# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Installs Debian packages on a GitHub runner, with time limits and retries:
# an apt mirror that stalls must fail an attempt within minutes instead of
# hanging the job until its timeout (as the nightly fuzz jobs did).

set -euo pipefail

# The runners' repository of Microsoft's packages, which nothing here needs,
# fails now and then (403, "no longer signed"), and with it apt-get update.
sudo rm -f /etc/apt/sources.list.d/microsoft-prod.list /etc/apt/sources.list.d/microsoft-prod.sources

apt_options=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
for attempt in 1 2 3; do
	if timeout 300 sudo apt-get "${apt_options[@]}" update -qq \
		&& timeout 900 sudo apt-get "${apt_options[@]}" install -y --no-install-recommends "$@"; then
		exit 0
	fi
	echo "apt-get failed (attempt $attempt of 3)" >&2
	sleep 15
done
exit 1
