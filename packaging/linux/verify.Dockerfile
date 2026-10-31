# SPDX-License-Identifier: GPL-3.0-or-later
# RP-39: a clean Linux x86_64 machine for RackPy's release candidates. Ubuntu 20.04 (glibc 2.31, older than
# the build machine's) with only what Rack Free 2.6.6 links (GL, X11, ALSA, PulseAudio and JACK client
# libraries), Xvfb, xdotool and ImageMagick to drive and photograph its window, and jq to read patches.
# No Python is installed: the build fails if any is. bin/rackpy-release verify runs it without a network.
FROM ubuntu@sha256:8feb4d8ca5354def3d8fce243717141ce31e2c428701f6682bd2fafe15388214
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgl1 libgl1-mesa-dri libx11-6 libasound2 libpulse0 libjack-jackd2-0 \
        xvfb xauth xdotool imagemagick jq ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && if command -v python3 || ls -d /usr/lib/python3* 2>/dev/null; then echo "Python is installed" >&2; exit 1; fi
