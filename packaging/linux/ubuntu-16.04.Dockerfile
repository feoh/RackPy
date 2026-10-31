# SPDX-License-Identifier: GPL-3.0-or-later
# RP-39: the oldest Linux the release candidates claim, Ubuntu 16.04 (glibc 2.23, the official toolchain's
# floor), with only what Rack Free 2.6.6 links, Xvfb, xdotool, ImageMagick and jq, as ubuntu-20.04.Dockerfile.
# No Python interpreter is installed: the build fails if one is. `bin/rackpy-release verify --image ubuntu-16.04` runs it
# without a network.
FROM ubuntu@sha256:1f1a2d56de1d604801a9671f301190704c25d604a416f59e03c04f5c6ffee0d6
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgl1-mesa-glx libgl1-mesa-dri libx11-6 libasound2 libpulse0 libjack-jackd2-0 \
        xvfb xauth xdotool imagemagick jq ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && if ls /usr/bin/python* /usr/local/bin/python* 2>/dev/null; then echo "Python is installed" >&2; exit 1; fi
