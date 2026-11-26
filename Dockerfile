# SPDX-License-Identifier: GPL-2.0-only
# Containerized Build & Hardware-Simulation Verification Environment
# Compatible with both `podman` and `docker`:
#   podman build -t mt7902-macos-test . && podman run --rm mt7902-macos-test

FROM registry.fedoraproject.org/fedora:41

RUN dnf install -y --setopt=install_weak_deps=False \
        clang \
        make \
        python3 \
        zlib-devel \
    && dnf clean all

WORKDIR /workspace
COPY . /workspace

# Regenerate Xcode project, validate all Info.plist bundles, and compile C++17 test binary from scratch
RUN make clean && make xcode && make plist-check && CXX=clang++ make build/test_mt7902

CMD ["./build/test_mt7902"]
