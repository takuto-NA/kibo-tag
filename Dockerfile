# Responsibility: provide a reproducible Linux toolchain for kibo-tag native tests and WASM builds.
# The repository is bind-mounted at run time; this image does not COPY application source.
#
# Build:  docker build -t kibo-tag-dev .
# Tests:  docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make tests
# WASM:   docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make apriltag_wasm.js

FROM emscripten/emsdk:3.1.50

USER root

RUN apt-get update \
    && apt-get install -y --no-install-recommends libcmocka-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
