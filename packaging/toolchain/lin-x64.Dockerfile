# SPDX-License-Identifier: GPL-3.0-or-later
# Build environment for the official rack-plugin-toolchain Linux toolchain
# (crosstool-ng x86_64-ubuntu16.04-linux-gnu, glibc 2.23). bin/rack-toolchain
# builds this image and runs `make toolchain-lin` in it; the toolchain itself is
# written to a cache directory outside git, never into the image.
#
# The package list is the official toolchain's Dockerfile (commit 4fd13187).
# The base image is pinned by digest; apt package versions are whatever the
# Ubuntu 24.04 archive serves, so the image is reproducible in intent, not
# bit for bit. bin/rack-toolchain check verifies the toolchain it produced.
FROM ubuntu@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
ENV LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        make ca-certificates git build-essential autoconf automake bison flex gawk \
        libtool-bin libncurses5-dev unzip zip jq libgl-dev libglu-dev wget curl cmake \
        nasm xz-utils file python3 libxml2-dev libssl-dev texinfo help2man libz-dev \
        rsync xxd perl coreutils zstd markdown libarchive-tools gettext libgmp-dev \
        libmpfr-dev \
    && rm -rf /var/lib/apt/lists/*
