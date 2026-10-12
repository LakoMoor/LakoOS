FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential git curl ca-certificates cmake ninja-build flex bison python3 \
        nasm xz-utils unzip pkg-config \
 && rm -rf /var/lib/apt/lists/*

ARG ROSBE_BOOTSTRAP_URL=https://raw.githubusercontent.com/ahmedarif193/winget-rosbe/main/rosbe-unix-bootstrap.sh
RUN curl -fsSL "$ROSBE_BOOTSTRAP_URL" -o /tmp/rosbe-bootstrap.sh \
 && sh /tmp/rosbe-bootstrap.sh \
 && rm -f /tmp/rosbe-bootstrap.sh

ENV ROSBE_ROOT=/root/.local/opt/rosbe
WORKDIR /src
CMD ["./buildenv/build.sh"]
