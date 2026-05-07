# ProyectoPI — Sistema de Análisis de Extractos de Inversión

Pipeline en C++/OpenCV/Tesseract que toma un extracto de inversión (PDF, JPG, PNG, foto de celular) y genera análisis estadístico, gráficas y CSV/JSON con los datos extraídos.

**Stack**: C++17 + CMake + OpenCV + wxWidgets + Tesseract OCR + nlohmann-json. Todo el procesamiento de imagen y OCR es 100% C++ (sin Python en runtime).

---

## Quick start (Docker — recomendado)

Solo necesitas Docker. Funciona idéntico en Linux, macOS y Windows.

```bash
# 1. Clonar
git clone https://github.com/JoSker51/DemoProyectoPI.git
cd DemoProyectoPI

# 2. Construir la imagen (primera vez, ~10-15 min — descarga Ubuntu + deps)
docker compose build

# 3. Correr modo consola sobre uno de los 10 PDFs de ejemplo
docker compose run --rm proyecto pdfs/examples/extracto_05_carlos.pdf

# 4. (Opcional) Modo GUI — requiere X11
xhost +local:docker
docker compose up proyecto-gui
```

### Outputs

Quedan en `./resultados/` (montado como volumen del contenedor):

```
resultados/
├── graphs/              # 6 gráficas PNG
│   ├── 01_yield_curve.png
│   ├── 02_maturity_ladder.png
│   ├── 03_pareto_concentracion.png
│   ├── 04_boxplot_tasas.png
│   ├── 05_drawdown.png
│   └── 06_dashboard_kpis.png
├── csv/                 # 4 archivos CSV
│   ├── extracto.csv
│   ├── renta_fija.csv
│   ├── fondos.csv
│   └── analisis.csv
└── json/
    └── extracto.json    # datos estructurados (schema v1)
```

### Otros modos de uso

```bash
# Procesar una imagen (JPG/PNG, foto de celular, escaneo)
docker compose run --rm proyecto pdfs/examples/extracto_05_carlos_rot90.jpg

# PDF con contraseña
docker compose run --rm proyecto pdfs/mi_extracto.pdf --pw MICONTRASEÑA

# Solo análisis sobre un JSON ya extraído (saltea OCR)
docker compose run --rm proyecto --from-json schema/example_extracto.json

# Activar guardado de etapas intermedias del preprocessor (debug)
docker compose run --rm -e PROYECTOPI_DEBUG_PREPROC=1 proyecto pdfs/foo.pdf
docker compose run --rm -e PROYECTOPI_DEBUG_OCR=1     proyecto pdfs/foo.pdf
```

### Procesar tus propios PDFs

Cualquier archivo en la carpeta local `./pdfs/` está disponible dentro del contenedor en `pdfs/`:

```bash
cp ~/Descargas/extracto_real.pdf pdfs/
docker compose run --rm proyecto pdfs/extracto_real.pdf
```

---

## Instalación nativa (Linux Ubuntu/Debian, sin Docker)

```bash
# 1. Dependencias del sistema
sudo apt update
sudo apt install -y \
  build-essential cmake git \
  libopencv-dev \
  libwxgtk3.0-gtk3-dev \
  nlohmann-json3-dev \
  poppler-utils \
  tesseract-ocr tesseract-ocr-spa tesseract-ocr-eng tesseract-ocr-osd

# 2. Clonar y compilar
git clone https://github.com/JoSker51/DemoProyectoPI.git
cd DemoProyectoPI
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. Correr
./ProyectoPI ../pdfs/examples/extracto_05_carlos.pdf       # consola
./ProyectoPI                                                 # GUI
```

> Para Fedora/Arch: usa `dnf` o `pacman` con los paquetes equivalentes (`opencv-devel`, `wxGTK3-devel`, `nlohmann-json-devel`, `tesseract`, `tesseract-langpack-spa`, etc.).

---

## Generar más datos de prueba

Hay 10 PDFs de ejemplo en `pdfs/examples/`. Para generar más con datos sintéticos variados:

```bash
pip install reportlab pypdfium2 pillow numpy

# 10 PDFs con clientes/holdings aleatorios
python3 tools/generate_test_extracts.py --count 10 --seed 42

# 8 variantes de imagen para estresar el preprocessor (JPG, rotaciones,
# blur, perspectiva, low-light) sobre un PDF base
python3 tools/generate_image_variants.py pdfs/examples/extracto_05_carlos.pdf
```

---

## Pipeline (cómo funciona)

```
PDF / Imagen
  │
  ├─ pdftoppm (si es PDF)  →  imagen 300 DPI
  │
  ▼
ImagePreprocessor (OpenCV)
  ├─ 1. Binarización (Otsu)
  ├─ 2. Detección de bordes (Canny)
  ├─ 3. Extracción de contornos
  ├─ 4. Convex Hull + área (rechaza no-rectangulares)
  ├─ 5. Clasificación (rectangularityScore: 4 vértices, ángulos ≈ 90°)
  ├─ 6. Hough → corrección fina de rotación
  ├─ Tesseract OSD → corrección gruesa 0/90/180/270°
  ├─ CLAHE + median + sharpening adaptativo
  └─ Cap de tamaño (max_dim=4000)
  │
  ▼
Tesseract OCR (lang=spa, --psm 6)
  │
  ▼
StandardParser (semántico)
  ├─ Detecta template v1 por marcadores
  ├─ Para cada bloque OCR: clasifica por tipo (date / % / $ / texto)
  ├─ Asigna campos del schema (nemo, fechas, tasas, valores)
  └─ Maneja headers ilegibles, nemos multi-palabra, OCR fragmentado
  │
  ▼
ExtractoCompleto (struct C++)
  │
  ├─→ StatisticalAnalyzer  →  25+ KPIs (HHI, Sharpe, Macaulay, Drawdown, ...)
  ├─→ GraphGenerator       →  6 PNGs
  └─→ CSVExporter          →  4 CSVs + JSON
```

---

## Resultados de robustez (8 variantes del extracto #05)

Ground truth: 6 CDTs, 1 fondo, $8.828.842.085,91

| Variante | OSD detecta | RF (esp 6) | Fondos (esp 1) |
|---|---|---|---|
| `base.png` (escaneo limpio)        | 0°   | ✅ 6   | ✅ 1 |
| `hi.jpg` (JPG calidad 95)          | 0°   | ✅ 6   | ✅ 1 |
| `lo.jpg` (JPG calidad 50)          | 0°   | ✅ 6   | ✅ 1 |
| `lowlight.jpg` (poca luz)          | 0°   | ✅ 6   | ✅ 1 |
| `rot90.jpg` (foto de costado)      | 270° | ✅ 6   | ✅ 1 |
| `rot180.jpg` (foto al revés)       | 180° | ✅ 6   | ✅ 1 |
| `perspective.jpg` (ángulo)         | 0°   | ⚠️ 5   | ✅ 1 |
| `blur.jpg` (foto desenfocada fuerte)| 0°  | ⚠️ 4   | ❌ 0 |

---

## Estructura del repo

```
.
├── src/                    # Fuentes C++
│   ├── main.cpp                  # entry point (consola + GUI)
│   ├── pdf_processor.cpp         # pdftoppm wrapper
│   ├── image_preprocessor.cpp    # pipeline OpenCV (1-6 del slide)
│   ├── ocr_extractor.cpp         # wrapper Tesseract CLI
│   ├── standard_parser.cpp       # parser del template v1
│   ├── data_structurer.cpp       # builders de las structs del schema
│   ├── statistical_analyzer.cpp  # 25+ KPIs financieros
│   ├── graph_generator.cpp       # 6 gráficas con OpenCV
│   ├── csv_exporter.cpp          # exportador CSV/JSON
│   └── ui_manager.cpp            # GUI wxWidgets
├── schema/                 # JSON schema v1 (formato unificado)
├── data/                   # config (tasas, thresholds)
├── pdfs/examples/          # 10 PDFs de ejemplo
├── tools/                  # generadores Python (no runtime)
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
└── CAMBIOS.md              # changelog detallado vs proyecto original
```

---

## Notas

- **No hay IA**: todo el OCR es Tesseract clásico, todo el análisis es matemática explícita (HHI, Sharpe, Macaulay, Fisher, drawdown, percentiles, IC). Cero modelos ML.
- **El parser está optimizado para el "template estándar v1"** definido en `tools/generate_test_extracts.py`. Otros formatos requieren añadir un parser específico (la capa de análisis funciona con cualquier JSON que respete `schema/extracto_v1.schema.json`).
- **Para PDFs escaneados o fotos**: el preprocessor maneja rotación arbitraria (incluso 180° gracias a Tesseract OSD), iluminación variable y baja resolución. El blur fuerte sigue degradando OCR — para esos casos, re-tomar la foto.
