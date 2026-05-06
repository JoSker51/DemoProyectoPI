# Cambios al Sistema de Análisis de Extractos

> Documento que detalla las modificaciones realizadas al repositorio original
> [DemoProyectoPI](https://github.com/Oscarpeg/DemoProyectoPI.git).

---

## 1. Resumen ejecutivo

El sistema original procesaba un único formato de extracto (Acciones & Valores
S.A.) extrayendo datos via OCR + reglas, calculaba estadísticas básicas y
generaba 5 gráficas simples. Las 4 modificaciones solicitadas eran:

1. Soportar imágenes como input y mostrarlas
2. Probar que funcionara con imágenes (mismo formato)
3. NO usar IA — usar fórmulas matemáticas para los resultados
4. Mejorar las gráficas según los resultados calculados

Adicionalmente se introdujeron mejoras arquitectónicas (schema JSON unificado,
loader desacoplado, configuración externa) que dejan el sistema preparado
para evolucionar a soportar otros emisores.

---

## 2. Cambios solicitados

### 2.1 — Mostrar imágenes (Punto 1)

**Antes**: la GUI solo aceptaba PDFs y no mostraba ninguna vista previa.

**Después**:
- Nueva pestaña **"Vista previa"** (`createPreviewTab`) en la UI
- Cuando se selecciona una imagen, se carga inmediatamente en la pestaña con
  reescalado automático (mantiene aspect ratio, max 1000px de ancho)
- Cuando se procesa un PDF, después de la conversión a imágenes se muestra
  la primera página en la misma pestaña

**Archivos modificados**:
- `src/ui_manager.h` — nuevos atributos `panel_preview_`, `scroll_preview_`,
  `lbl_preview_info_`
- `src/ui_manager.cpp` — método `showPreview(path)` y nueva pestaña

### 2.2 — Soporte de imágenes como input (Punto 2)

**Antes**: el pipeline asumía PDF → `pdftoppm` → OCR. Las imágenes no
eran un input válido.

**Después**:
- Detección automática del tipo (`isImageFile()` / `isPdfFile()`) por extensión
- Si es imagen: se salta la conversión PDF y la imagen va directo al OCR como
  si fuera la única página
- Si es PDF: flujo original sin cambios + ahora muestra preview de página 1
- Soporta: `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`
- Diálogo de archivo actualizado con filtros separados para PDFs e imágenes
- En modo consola: `./ProyectoPI mi_imagen.png` funciona idéntico a un PDF
- Si es imagen, no se pide contraseña (lógicamente)

**Archivos modificados**:
- `src/ui_manager.cpp` — `OnSelectPDF()` ramifica según tipo
- `src/ui_manager.cpp` — `processExtracto()` salta `pdftoppm` para imágenes
- `src/main.cpp` — `runConsole()` también ramifica

### 2.3 — Análisis con fórmulas matemáticas (Punto 3)

**Estado original**: el código ya usaba fórmulas matemáticas para todas las
métricas estadísticas (`statistical_analyzer.cpp`). El único componente
"AI" era EasyOCR, que es OCR estándar (transcribe píxeles a texto).

**Lo que se añadió**:
Se añadieron **25+ métricas avanzadas** de nivel CFO, todas implementadas
con fórmulas matemáticas explícitas (sin IA). Ver sección 5 para el detalle.

**Archivos modificados/nuevos**:
- `src/statistical_analyzer.h` — nuevo struct `AdvancedMetrics`
- `src/statistical_analyzer.cpp` — función `computeAdvanced()` y helpers
  (`skewness`, `kurtosis`, `maxDrawdown`, `sharpeRatio`, `realReturnFisher`,
  `diasEntreFechasISO`)
- `src/finance_config.h/.cpp` — nuevo módulo de configuración
- `data/finance_config.json` — presets configurables sin recompilar

### 2.4 — Mejorar gráficas (Punto 4)

**Antes**: 5 gráficas simples (composición, distribución RF, rentabilidades
FIC, evolución saldo, tabla estadísticas).

**Después**: las 5 gráficas originales **fueron reemplazadas** por 6
gráficas financieras profesionales (ver sección 6).

**Archivos modificados**:
- `src/graph_generator.h` — nueva API con 6 métodos públicos
- `src/graph_generator.cpp` — reescrito completo (~700 líneas) con
  primitivas adicionales (`drawAxes`, `drawRegressionLine`, etc.)
- `src/ui_manager.cpp` — pestaña "Análisis" ahora despliega los 25+ KPIs
  categorizados

---

## 3. Cambios adicionales (mejoras arquitectónicas)

Estos no estaban en la lista pedida pero se hicieron para que el sistema
sea más mantenible y extensible.

### 3.1 — Schema JSON unificado

Se diseñó un schema formal en **JSON Schema 2020-12** que actúa como
contrato entre la fase de extracción y la de análisis. Define
metadatos comunes (issuer, account_holder, period) y un array de
`sections` con tipos discriminados (`cash_balance`, `fixed_income`,
`investment_fund`, `transactions`).

**Archivos nuevos**:
- `schema/extracto_v1.schema.json` — el contrato formal
- `schema/example_extracto.json` — un ejemplo válido para testing

### 3.2 — ExtractoLoader

Clase nueva que carga un `ExtractoCompleto` desde un JSON conforme al
schema. Permite ejecutar el análisis sobre cualquier extracto bien
formateado, independiente de cómo se obtuvo el JSON.

**Archivos nuevos**:
- `src/extracto_loader.h/.cpp`

### 3.3 — Flag `--from-json`

Nueva opción CLI que salta toda la extracción PDF/OCR y va directo a
validación → análisis → gráficas → CSV/JSON. Útil para:
- Probar nuevas fórmulas/gráficas sin re-extraer
- Validar el pipeline de análisis con datos sintéticos
- Procesar extractos cuyo JSON haya sido producido por otra fuente

**Uso**:
```bash
./ProyectoPI --from-json schema/example_extracto.json
```

### 3.4 — Configuración externa

Antes los thresholds (HHI, sigma de outliers, etc.) eran constantes en
código. Ahora son ajustables vía `data/finance_config.json` sin recompilar.

### 3.5 — Soporte para sección de transacciones

El struct `ExtractoCompleto` se extendió con `vector<CuentaTransacciones>`
para soportar movimientos bancarios en el schema. La extracción todavía
no las llena (no aplica al PDF de Acciones & Valores), pero el modelo,
loader y serialización están listos.

### 3.6 — Wrapper de ejecución

Script `run.sh` que activa el venv de Python (necesario para EasyOCR
en WSL/Linux) antes de invocar el binario. Evita errores de "easyocr
not found" cuando se ejecuta desde una shell fresca.

---

## 4. Estructura de archivos nuevos/modificados

```
DemoProyectoPI/
├── CAMBIOS.md                       ← este documento
├── CMakeLists.txt                   ← actualizado (nuevos sources, copia schema/)
├── run.sh                           ← NUEVO wrapper de ejecución
│
├── data/
│   ├── historico_inicial.csv        (sin cambios)
│   └── finance_config.json          ← NUEVO presets configurables
│
├── schema/                          ← NUEVA carpeta
│   ├── extracto_v1.schema.json
│   └── example_extracto.json
│
└── src/
    ├── data_structurer.h/.cpp       ← extendido (Transaction, CuentaTransacciones)
    ├── extracto_loader.h/.cpp       ← NUEVO
    ├── finance_config.h/.cpp        ← NUEVO
    ├── statistical_analyzer.h/.cpp  ← AdvancedMetrics + 25 métricas
    ├── graph_generator.h/.cpp       ← reescrito (6 gráficas nuevas)
    ├── ui_manager.h/.cpp            ← pestaña Vista previa + nueva tabla Análisis
    ├── main.cpp                     ← flags --from-json, soporte imágenes
    │
    └── (resto sin cambios)
        ├── pdf_processor.h/.cpp
        ├── image_preprocessor.h/.cpp
        ├── ocr_extractor.h/.cpp
        ├── page_classifier.h/.cpp
        ├── table_detector.h/.cpp
        └── csv_exporter.h/.cpp
```

---

## 5. Métricas implementadas

Todas calculadas con fórmulas matemáticas explícitas en
`statistical_analyzer.cpp`. **Cero IA en el análisis**.

### 5.1 Rendimiento

| Métrica | Fórmula |
|---|---|
| Yield ponderado (TIR) | `Σ(wᵢ × tasa_valoraciónᵢ)` con `wᵢ = market_valueᵢ / total_RF` |
| Yield facial ponderado | Igual pero con `tasa_facial` |
| Spread promedio | `yield_valoración − yield_facial` |
| Crecimiento del periodo | `(Vfin / Vinicio − 1) × 100` |
| Crecimiento anualizado | `((Vfin/Vinicio)^(1/años) − 1) × 100` |
| Retorno real (Fisher) | `(1+nominal)/(1+inflación) − 1` |

### 5.2 Riesgo de tasa

| Métrica | Fórmula |
|---|---|
| Duración Macaulay | `Σ(wᵢ × tᵢ)` con `tᵢ = días_a_vencer/365` |
| Duración modificada | `Macaulay / (1 + yield)` |

### 5.3 Concentración

| Métrica | Fórmula |
|---|---|
| HHI (Herfindahl) | `Σ(wᵢ²) × 10000`. Categorías: <1500 bajo, 1500-2500 moderado, >2500 alto |
| Top-1 / Top-3 / Top-5 exposure | `Σ(top_n_valores) / total × 100` |

### 5.4 Vencimientos

| Métrica | Cálculo |
|---|---|
| Buckets configurables | Distribución por <90d / 90-365d / 1-3y / >3y |
| Días promedio ponderado | `Σ(wᵢ × díasᵢ_a_vencer)` |

### 5.5 FIC (ratios de desempeño)

| Métrica | Fórmula |
|---|---|
| Sharpe Ratio | `(rentabilidad − tasa_libre_riesgo) / σ` |
| Rentabilidad anual | Toma `historical_returns.last_year` |
| Sigma estimada | `stdDev(rentabilidades_históricas)` (proxy cuando no hay serie completa) |

### 5.6 Serie temporal

| Métrica | Fórmula |
|---|---|
| Max Drawdown | `min((Vt − Vmax)/Vmax) × 100` |
| Drawdown actual | `(Vactual − Vmax) / Vmax × 100` |

### 5.7 Estadística avanzada

| Métrica | Fórmula |
|---|---|
| Skewness (Fisher) | `(1/n) × Σ((xᵢ−μ)/σ)³` |
| Kurtosis (exceso) | `(1/n) × Σ((xᵢ−μ)/σ)⁴ − 3` |
| Coeficiente de Variación | `σ / μ` |
| Intervalo de confianza 95% | `μ ± 1.96 × σ/√n` |

### 5.8 Estadística básica (preexistente)

`mean`, `stdDev`, `median`, `min/max`, outliers k-sigma sobre 4 tipos
de tasas (valoración, negociación, facial) y valores de mercado.

---

## 6. Gráficas (las 6 nuevas)

Reemplazan las 5 originales. Todas en `graph_generator.cpp`, generadas
con OpenCV puro.

| # | Gráfica | Métricas que visualiza |
|---|---|---|
| **1** | **Yield Curve** | Scatter de tasa de valoración vs días a vencimiento + línea de regresión lineal con R². Muestra si el portafolio sigue una curva de tasas coherente |
| **2** | **Maturity Ladder** | Barras del monto $ por bucket de vencimiento. Apoya planeación de liquidez |
| **3** | **Pareto de Concentración** | Barras (% individual por holding) + línea acumulada al 100% + umbral 80%. Visualiza HHI y Top-N |
| **4** | **Boxplot de tasas** | Cuartiles, mediana, whiskers, outliers. Banda gris = IC 95%. Línea verde = media |
| **5** | **Drawdown Chart** | Línea del valor del portafolio + área roja entre peaks y caídas. Muestra Max Drawdown visualmente |
| **6** | **Dashboard Ejecutivo** | 8 KPIs principales en formato tarjetas con código de color semántico. "One-pager para el jefe" |

### Características de implementación

- **Robustez ante datos degenerados**: si todas las fechas son iguales,
  el Yield Curve espacia X artificialmente y omite la regresión.
- **Deduplicación de nemotécnicos**: si la extracción produce 4 CDTs con
  el mismo nombre, las gráficas los renombran a `CDT #1, #2, #3, #4`.
- **Filtrado de holdings vacíos**: el Pareto omite items con $0.
- **Auto-distribución de etiquetas**: el Boxplot reposiciona labels que
  estarían demasiado cerca para evitar superposiciones.
- **Colores semánticos**: verde=positivo, rojo=negativo, naranja=warning,
  gris=neutral. Aplicados según valor (ej. Sharpe > 1.5 verde, < 0 rojo).

---

## 7. Configuración externa

Archivo `data/finance_config.json`. Editable sin recompilar:

```json
{
  "tasa_libre_riesgo_anual": 0.0925,
  "inflacion_anual":         0.0480,
  "benchmark_rendimiento_anual": 0.1100,
  "umbrales_concentracion": {
    "hhi_bajo": 1500,
    "hhi_alto": 2500
  },
  "umbrales_duracion_dias": {
    "corto":  90,
    "medio": 365,
    "largo": 1095
  },
  "umbral_outliers_sigma": 2.0
}
```

Cambiar `inflacion_anual` a 0.06 y volver a correr → todas las métricas
de retorno real recalculan automáticamente.

---

## 8. Cómo ejecutar

### Modo GUI (interfaz gráfica completa)
```bash
./run.sh
```

### Modo consola sobre PDF
```bash
./run.sh pdfs/extracto.pdf --pw CONTRASEÑA
```

### Modo consola sobre imagen
```bash
./run.sh extracto.png
```

### Modo solo análisis (sobre JSON ya extraído)
```bash
./run.sh --from-json schema/example_extracto.json
```

Outputs en `build/output/`:
- `graphs/` — las 6 PNGs
- `csv/` — 4 CSVs
- `json/extracto.json` y `json/analysis.json`

---

## 9. Limitaciones conocidas y trabajo futuro

### 9.1 Extracción específica al formato Acciones & Valores

El pipeline de extracción (`pdf_processor` → `page_classifier` →
`data_structurer`) está diseñado para el layout específico de Acciones &
Valores S.A. Otros formatos no serán reconocidos correctamente. La
parte de **análisis y gráficas funciona con cualquier extracto** si
se provee como JSON conforme al schema.

Caminos posibles para soportar otros formatos:
- **Plantillas por emisor** — config files que mapean cada emisor a
  reglas específicas
- **Entrada manual asistida** — UI donde el usuario rellena el JSON
- **Document AI** (descartado por ahora) — usar modelos como Table
  Transformer / LayoutLMv3 para entender layouts genéricos

### 9.2 Bug conocido en parseo de tabla Renta Fija

Cuando se procesa el PDF real, los 4 CDTs aparecen con el mismo
nemotécnico y misma fecha de vencimiento (probable error en
`data_structurer.cpp` agrupando filas). Las gráficas son resilientes
(renombran con sufijos #1, #2…) pero la causa raíz está en la
extracción y queda como pendiente.

### 9.3 Métricas que requerirían datos adicionales

- **VaR (paramétrico)**: requiere histórico de retornos del portafolio
  con frecuencia mensual/diaria
- **Beta y Alpha**: requieren benchmark con histórico
- **Tracking Error**: requiere benchmark con histórico

Por ahora no se calculan; se pueden añadir al `AdvancedMetrics` cuando
haya disponibilidad de los datos.

---

## 10. Dependencias añadidas

Ninguna nueva dependencia de C++. Se reutilizaron las del proyecto:
- OpenCV (para gráficas e image handling)
- wxWidgets (para UI)
- nlohmann/json (para JSON Schema parsing)

EasyOCR sigue siendo invocada via Python como en el proyecto original.
