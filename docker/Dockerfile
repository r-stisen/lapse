# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
    libfftw3-dev \
    g++ cmake pkg-config git \
    wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
# libfvad is not packaged for Debian, cmake fetches and links it in statically
COPY CMakeLists.txt ./
COPY engine ./engine
RUN cmake -B build && \
    cmake --build build -j "$(nproc)" && \
    cp build/lapse /lapse

ARG ORT_VERSION=1.28.0
RUN set -eux; \
    case "$(dpkg --print-architecture)" in \
      amd64) ORT_ARCH=x64 ;; \
      arm64) ORT_ARCH=aarch64 ;; \
      *) ORT_ARCH="" ;; \
    esac; \
    if [ -n "$ORT_ARCH" ]; then \
      wget -q -O /tmp/ort.tgz "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}.tgz" && \
      tar xf /tmp/ort.tgz -C /tmp && \
      cp /tmp/onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}/lib/libonnxruntime.so.${ORT_VERSION} /libonnxruntime.so && \
      wget -q -O /silero_vad.onnx "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx" && \
      rm -rf /tmp/ort.tgz /tmp/onnxruntime-linux-*; \
    fi; \
    touch /libonnxruntime.so /silero_vad.onnx


FROM debian:bookworm-slim
LABEL org.opencontainers.image.title="lapse" \
      org.opencontainers.image.description="Language-agnostic subtitle synchronization" \
      org.opencontainers.image.source="https://github.com/Schwponaco-org/lapse" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"

RUN apt-get update && apt-get install -y --no-install-recommends \
    libavcodec59 libavformat59 libavutil57 libswresample4 \
    libfftw3-double3 \
    python3 python3-pip \
    ffmpeg \
    gosu \
    && rm -rf /var/lib/apt/lists/*

ENV PYTHONUNBUFFERED=1
ENV MEDIA_ROOT=/media
ENV DB_PATH=/data/lapse.db
ENV LAPSE_BIN=/app/lapse
ENV WEB_PORT=8080

WORKDIR /app
COPY docker/requirements.txt .
RUN pip3 install -r requirements.txt --break-system-packages && \
    apt-get purge -y python3-pip && apt-get autoremove -y && \
    rm -rf /root/.cache /var/lib/apt/lists/*

COPY --from=build /lapse /libonnxruntime.so /silero_vad.onnx ./
COPY docker/main.py docker/web.py docker/healthcheck.py docker/index.html ./
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh /app/lapse && \
    find /app -maxdepth 1 -size 0 -name "*.so" -delete && \
    find /app -maxdepth 1 -size 0 -name "*.onnx" -delete

EXPOSE 8080
HEALTHCHECK --interval=60s --timeout=10s --start-period=30s --retries=3 \
    CMD ["python3", "/app/healthcheck.py"]

ENTRYPOINT ["/entrypoint.sh"]
CMD ["python3", "main.py"]
