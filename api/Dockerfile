FROM debian:trixie-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake g++ make pkg-config libcpp-httplib-dev libicu-dev libpq-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends libnats-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt *.cpp *.hpp ./
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel

FROM debian:trixie-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends curl libcpp-httplib0.18 libicu76 libpq5 \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends libhiredis1.1.0 \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install -y --no-install-recommends libnats3.10 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/mini-test-api /build/mini-test-worker /usr/local/bin/
USER 10001
CMD ["/usr/local/bin/mini-test-api"]
