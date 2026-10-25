# PS4 Save Remote — produce IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg
# Usage: scripts/build-pkg.ps1   OR   docker build -t ps4-save-remote .

FROM ubuntu:22.04 AS deps

ENV DEBIAN_FRONTEND=noninteractive
ENV OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git cmake make clang-12 lld-12 llvm-12 \
    python3 xz-utils unzip pkg-config \
    && curl -fsSL -o /tmp/libssl1.1.deb \
      http://archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb \
    && dpkg -i /tmp/libssl1.1.deb \
    && rm -f /tmp/libssl1.1.deb \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-12 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-12 100 \
    && update-alternatives --install /usr/bin/ld.lld ld.lld /usr/bin/ld.lld-12 100 \
    && ln -sf /usr/bin/llvm-ar-12 /usr/bin/llvm-ar \
    && ln -sf /usr/bin/llvm-ranlib-12 /usr/bin/llvm-ranlib \
    && ln -sf /usr/bin/llvm-objcopy-12 /usr/bin/llvm-objcopy \
    && ln -sf /usr/bin/llvm-strip-12 /usr/bin/llvm-strip

WORKDIR /deps

RUN curl -sL "https://src.fedoraproject.org/repo/pkgs/mbedtls/mbedtls-2.16.12.tar.gz/sha512/8d96d8cd906cc0999134320e4e1f550631426d166eab5da6e65469ee7286093810fcc6ac4bd5500ee55972d159f8bef7f9e53245f7f0eec72f72c35265b4313b/mbedtls-2.16.12.tar.gz" \
    | tar xz \
 && git clone --depth 1 https://github.com/bucanero/oosdk_libraries.git \
 && git clone --depth 1 https://github.com/bucanero/dbglogger.git \
 && git clone --depth 1 https://github.com/bucanero/apollo-lib.git \
 && git clone --depth 1 https://github.com/bucanero/libSQLite-ps4.git \
 && git clone --depth 1 -b ps4 https://github.com/bucanero/SDL-PS4.git \
 && git clone --depth 1 https://github.com/bucanero/mxml.git \
 && git clone --depth 1 https://github.com/bucanero/libunrar-ps3.git \
 && git clone --depth 1 https://github.com/bucanero/libun7zip.git \
 && git clone --depth 1 https://github.com/bucanero/ps4-libjbc.git \
 && git clone --depth 1 https://github.com/bucanero/s3mplay.git \
 && git clone --depth 1 https://github.com/bucanero/mini18n.git

FROM deps AS build
WORKDIR /src
COPY . /src
RUN sed -i 's/\r$//' /src/scripts/docker-build.sh && chmod +x /src/scripts/docker-build.sh && bash /src/scripts/docker-build.sh

FROM scratch AS export
COPY --from=build /out/ /
