
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libssl-dev \
    zlib1g-dev \
    uuid-dev \
    libjsoncpp-dev \
    libpq-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

RUN git clone --depth 1 https://github.com/drogonframework/drogon.git && \
    cd drogon && \
    git submodule update --init --recursive && \
    cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_TESTING=OFF && \
    cmake --build build -j2 && \
    cmake --install build

WORKDIR /app

COPY app/ /app/

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j2

ENV PORT=10000

EXPOSE 10000

CMD ["sh", "-c", "./build/jkmart_backend"]
