FROM debian:13

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    debhelper \
    debootstrap \
    devscripts \
    dpkg-dev \
    libsystemd-dev \
    libx11-dev \
    libxext-dev \
    lintian \
    pkgconf \
    python3 \
    python3-pil \
    sbuild \
    schroot \
    xz-utils \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
