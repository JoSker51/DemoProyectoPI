# ============================================================
# ProyectoPI - Sistema de Análisis de Extractos de Inversión
# Imagen Docker con todas las dependencias
# ============================================================
FROM ubuntu:22.04

# Evitar prompts interactivos durante instalación
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/Bogota

# --- 1. Dependencias del sistema ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libopencv-dev \
    libwxgtk3.0-gtk3-dev \
    nlohmann-json3-dev \
    poppler-utils \
    # Tesseract OCR (reemplaza EasyOCR; OCR clasico, sin Python)
    tesseract-ocr \
    tesseract-ocr-spa \
    tesseract-ocr-eng \
    tesseract-ocr-osd \
    # Para la GUI (X11 forwarding)
    x11-apps \
    libgtk-3-0 \
    libx11-xcb1 \
    libxcb1 \
    dbus-x11 \
    # Utilidades
    nano \
    && rm -rf /var/lib/apt/lists/*

# --- 2. Copiar proyecto ---
WORKDIR /app
COPY CMakeLists.txt .
COPY src/ src/
COPY data/ data/
COPY schema/ schema/

# --- 3. Compilar ---
RUN mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && echo "=== Compilacion exitosa ==="

# --- 4. Directorio de trabajo en build ---
WORKDIR /app/build

# Crear directorios de salida
RUN mkdir -p output/images output/graphs output/csv output/json

# Puerto por si se necesita en el futuro
EXPOSE 8080

# Punto de entrada: modo consola por defecto
ENTRYPOINT ["./ProyectoPI"]
