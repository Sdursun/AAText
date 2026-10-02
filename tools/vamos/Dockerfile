# Build image with the vamos AmigaOS emulator (amitools) for host-side tests.
# docker build -t aatext-vamos tools/vamos
FROM ghcr.io/rondoval/amiga-build-container:latest
RUN apt-get update && apt-get install -y --no-install-recommends python3-venv python3-dev gcc g++ git && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/vamos && /opt/vamos/bin/pip install --no-cache-dir git+https://github.com/cnvogelg/amitools.git git+https://github.com/cnvogelg/machine68k.git greenlet
ENV PATH=/opt/vamos/bin:$PATH
