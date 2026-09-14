FROM sdv-sandbox:latest

ENV DEBIAN_FRONTEND=noninteractive \
    DIGITAL_TWIN_WEB_ROOT=/twin/web \
    VSOMEIP_CONFIGURATION=/workspace/config/vsomeipLocal.json

RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /twin

COPY CMakeLists.txt ./
COPY src ./src
COPY web ./web
COPY scripts ./scripts

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DSDV_SANDBOX_DIR=/workspace \
      -DSDV_BUILD_DIR=/workspace/build \
    && cmake --build build --parallel "$(nproc)" \
    && chmod +x /twin/scripts/runDigitalTwin.sh

EXPOSE 8080

LABEL org.opencontainers.image.title="SDV Digital Twin Dashboard" \
      org.opencontainers.image.description="SOME/IP subscriber bridged to a live browser dashboard"

CMD ["/twin/scripts/runDigitalTwin.sh"]
