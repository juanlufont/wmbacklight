FROM debian:bullseye-slim

RUN apt-get update \
    && apt-get install -y \
    gcc \
    make \
    libdockapp-dev \
    libxext-dev \
    && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/bin/bash", "-c", "/usr/bin/make -C /target"]
