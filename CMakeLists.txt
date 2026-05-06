cmake_minimum_required(VERSION 3.16)
project(ProyectoPI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencias ---
find_package(OpenCV REQUIRED)
find_package(wxWidgets REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})

# nlohmann-json (del sistema: apt install nlohmann-json3-dev)
find_package(nlohmann_json 3.10 REQUIRED)

# --- Fuentes ---
set(SOURCES
    src/main.cpp
    src/pdf_processor.cpp
    src/page_classifier.cpp
    src/image_preprocessor.cpp
    src/table_detector.cpp
    src/ocr_extractor.cpp
    src/data_structurer.cpp
    src/statistical_analyzer.cpp
    src/graph_generator.cpp
    src/csv_exporter.cpp
    src/ui_manager.cpp
    src/extracto_loader.cpp
    src/finance_config.cpp
    src/standard_parser.cpp
)

# Copiar schema al build (para ejecutarse desde build/ con rutas relativas)
file(COPY schema/ DESTINATION ${CMAKE_BINARY_DIR}/schema)

add_executable(ProyectoPI ${SOURCES})

target_include_directories(ProyectoPI PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(ProyectoPI
    ${OpenCV_LIBS}
    ${wxWidgets_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Copiar datos al directorio de build (schema ya copiado arriba)
file(COPY data/   DESTINATION ${CMAKE_BINARY_DIR}/data)

# --- Verificacion de Tesseract en build time (informativo) ---
find_program(TESSERACT_EXE tesseract)
if(TESSERACT_EXE)
    message(STATUS "Tesseract OCR encontrado: ${TESSERACT_EXE}")
else()
    message(WARNING
        "Tesseract no encontrado en PATH. El binario compilara pero fallara en runtime.\n"
        "  Linux:   sudo apt install tesseract-ocr tesseract-ocr-spa\n"
        "  Windows: scoop install tesseract  (luego anadir spa.traineddata)")
endif()

# Crear directorios de salida en build
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output/images)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output/graphs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output/csv)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output/json)
