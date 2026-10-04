FROM opennow-ps5-builder
RUN apt-get update && apt-get install -y --no-install-recommends bison flex patch xz-utils ffmpeg python3-venv glslang-tools && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/gpu-tools && /opt/gpu-tools/bin/pip install meson==1.10.1 Mako==1.3.10 PyYAML==6.0.2 packaging==25.0
ENV PATH="/opt/gpu-tools/bin:${PATH}"
