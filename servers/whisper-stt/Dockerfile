FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/ggml-org/whisper.cpp /app

WORKDIR /app

RUN cmake -B build \
    -DWHISPER_BUILD_SERVER=ON \
    -DGGML_AVX=ON \
    -DGGML_AVX2=ON \
    -DGGML_AVX512=OFF \
    -DGGML_CUDA=OFF \
    && cmake --build build --config Release -j$(nproc)

EXPOSE 8080

ENTRYPOINT ["/app/build/bin/whisper-server"]
CMD ["-m", "/app/models/ggml-small.en.bin", "--host", "0.0.0.0", "--port", "8080", "-ng", "--inference-path", "/inference"]
