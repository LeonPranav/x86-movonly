# Cross-platform build/test image for x86-movonly.
# The tool emits 32-bit x86 (i386) Linux assembly, so a Linux x86 environment
# is required to run the output. This image provides it on any host (Windows,
# Linux, macOS/Apple Silicon) via the linux/amd64 platform.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake make g++ binutils \
        gcc-multilib g++-multilib libc6-dev-i386 lib32stdc++-13-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# Default: run the full translate -> assemble -> run -> mov-only-check suite.
CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
