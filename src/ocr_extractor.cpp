#include "ocr_extractor.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <array>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// =============================================================
// Helper: ejecutar comando y capturar stdout (igual que pdf_processor)
// Usa popen en Linux/macOS y _popen en Windows MSVC.
// =============================================================
#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
#else
  #define POPEN  popen
  #define PCLOSE pclose
#endif

static std::string execAndCapture(const std::string& cmd) {
    std::string full = cmd + " 2>&1";
    std::array<char, 4096> buf;
    std::string out;
    FILE* p = POPEN(full.c_str(), "r");
    if (!p) return "";
    while (fgets(buf.data(), static_cast<int>(buf.size()), p) != nullptr) {
        out += buf.data();
    }
    PCLOSE(p);
    return out;
}

// =============================================================
// Disponibilidad de Tesseract
// =============================================================
bool OCRExtractor::isTesseractAvailable() {
    std::string out = execAndCapture("tesseract --version");
    return out.find("tesseract") != std::string::npos;
}

bool OCRExtractor::hasLanguage(const std::string& lang) {
    std::string out = execAndCapture("tesseract --list-langs");
    // Buscar la linea exacta del idioma
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        // limpiar \r y espacios
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line == lang) return true;
    }
    return false;
}

// =============================================================
// Constructor / disponibilidad
// =============================================================
OCRExtractor::OCRExtractor()
    : temp_dir_("output/images/temp_ocr") {
    fs::create_directories(temp_dir_);
}

bool OCRExtractor::startServer() {
    if (!isTesseractAvailable()) {
        std::cerr << "[OCRExtractor] ERROR: 'tesseract' no esta en el PATH.\n"
                  << "  Linux:   sudo apt install tesseract-ocr tesseract-ocr-spa\n"
                  << "  Windows: scoop install tesseract\n"
                  << "           (o instalador UB-Mannheim, anadir tesseract.exe al PATH)\n";
        available_ = false;
        return false;
    }
    if (!hasLanguage(lang_)) {
        std::cerr << "[OCRExtractor] ERROR: idioma '" << lang_ << "' no instalado.\n"
                  << "  Linux:   sudo apt install tesseract-ocr-" << lang_ << "\n"
                  << "  Windows: descargar " << lang_
                  << ".traineddata desde https://github.com/tesseract-ocr/tessdata\n"
                  << "           y ponerlo en TESSDATA_PREFIX o tessdata/.\n";
        available_ = false;
        return false;
    }
    available_ = true;
    std::cout << "[OCRExtractor] Tesseract OCR listo (idioma=" << lang_
              << ", psm=" << psm_ << ", oem=" << oem_ << ")\n";
    return true;
}

// =============================================================
// Guardado de imagen temporal
// =============================================================
std::string OCRExtractor::saveTempImage(const cv::Mat& img, const std::string& prefix) {
    std::string path = temp_dir_ + "/" + prefix + "_" +
                       std::to_string(temp_counter_++) + ".png";
    cv::imwrite(path, img);
    return path;
}

// =============================================================
// Llamada a Tesseract (TSV: linea por palabra con bbox y conf)
// =============================================================
std::string OCRExtractor::runTesseractTSV(const std::string& image_path) {
    // tesseract <img> stdout -l <lang> --psm <n> --oem <n> tsv
    // El argumento "stdout" hace que escriba a stdout en vez de archivo .tsv
    std::ostringstream cmd;
    cmd << "tesseract"
        << " \"" << image_path << "\""
        << " stdout"
        << " -l " << lang_
        << " --psm " << psm_
        << " --oem " << oem_
        << " tsv";
    return execAndCapture(cmd.str());
}

// =============================================================
// Parseo del TSV. Formato (12 columnas tab-separadas):
//   level page_num block_num par_num line_num word_num
//   left top width height conf text
// level: 1=page, 2=block, 3=para, 4=line, 5=word
//
// Emitimos un OCRResult por PALABRA (level=5) en vez de agrupar por
// linea. El downstream (data_structurer, table_detector) hace su propio
// clustering por bbox para asignar palabras a columnas — necesita
// granularidad fina, no lineas completas. Esto replica el comportamiento
// que tenia EasyOCR (un bbox por frase corta).
//
// Detalle: Tesseract a veces parte palabras como "1.000.000.000,00" en
// varias entradas con espacios. Las uniones de "palabras adyacentes
// muy juntas" (gap horizontal < altura/2 y misma linea) las fusionamos
// para reconstruir numeros y nombres compuestos.
// =============================================================
std::vector<OCRResult> OCRExtractor::parseTSV(const std::string& tsv) {
    std::istringstream iss(tsv);
    std::string line;
    bool header_skipped = false;

    struct Word {
        int block, par, ln;
        int left, top, width, height;
        double conf;
        std::string text;
    };
    std::vector<Word> words;

    while (std::getline(iss, line)) {
        if (!header_skipped) { header_skipped = true; continue; }
        if (line.empty()) continue;

        std::vector<std::string> cols;
        std::string tok;
        std::istringstream ls(line);
        while (std::getline(ls, tok, '\t')) cols.push_back(tok);
        if (cols.size() < 12) continue;

        try {
            int level = std::stoi(cols[0]);
            if (level != 5) continue;
            std::string t = cols[11];
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
            if (t.empty()) continue;

            Word w;
            w.block  = std::stoi(cols[2]);
            w.par    = std::stoi(cols[3]);
            w.ln     = std::stoi(cols[4]);
            w.left   = std::stoi(cols[6]);
            w.top    = std::stoi(cols[7]);
            w.width  = std::stoi(cols[8]);
            w.height = std::stoi(cols[9]);
            w.conf   = std::stod(cols[10]);
            w.text   = t;
            if (w.conf < 0) continue;
            words.push_back(w);
        } catch (...) {
            continue;
        }
    }

    // Emitir un OCRResult por palabra. Fusionar adyacentes en la misma
    // linea cuando esten muy pegadas (gap < altura/2). Eso reconstruye
    // numeros como "1.000.000.000,00" que tesseract puede partir.
    std::vector<OCRResult> results;
    size_t i = 0;
    while (i < words.size()) {
        size_t j = i + 1;
        OCRResult r;
        r.text       = words[i].text;
        r.confidence = words[i].conf / 100.0;
        int x1 = words[i].left;
        int y1 = words[i].top;
        int x2 = words[i].left + words[i].width;
        int y2 = words[i].top  + words[i].height;
        int h  = words[i].height;

        while (j < words.size()
               && words[j].block == words[i].block
               && words[j].par   == words[i].par
               && words[j].ln    == words[i].ln) {
            int gap = words[j].left - x2;
            if (gap > h / 2) break;          // espacio = nueva celda
            r.text += " " + words[j].text;
            r.confidence = (r.confidence + words[j].conf / 100.0) / 2.0;
            x2 = std::max(x2, words[j].left + words[j].width);
            y1 = std::min(y1, words[j].top);
            y2 = std::max(y2, words[j].top + words[j].height);
            ++j;
        }
        r.bbox = cv::Rect(x1, y1, x2 - x1, y2 - y1);
        results.push_back(r);
        i = j;
    }
    return results;
}

// =============================================================
// API publica
// =============================================================
std::vector<OCRResult> OCRExtractor::extractAll(const cv::Mat& image) {
    if (!available_ && !startServer()) return {};
    std::cout << "[OCRExtractor] Tesseract sobre imagen "
              << image.cols << "x" << image.rows << "..." << std::endl;
    std::string path = saveTempImage(image, "full");
    std::string tsv  = runTesseractTSV(path);
    fs::remove(path);
    auto results = parseTSV(tsv);
    std::cout << "[OCRExtractor] " << results.size()
              << " lineas de texto extraidas." << std::endl;
    return results;
}

std::vector<OCRResult> OCRExtractor::extractFromRegion(const cv::Mat& region) {
    if (!available_ && !startServer()) return {};
    std::string path = saveTempImage(region, "region");
    std::string tsv  = runTesseractTSV(path);
    fs::remove(path);
    return parseTSV(tsv);
}

OCRResult OCRExtractor::extractFromCell(const cv::Mat& cell_img) {
    auto rows = extractFromRegion(cell_img);
    OCRResult combined;
    combined.confidence = 0.0;
    if (rows.empty()) return combined;
    double conf_sum = 0;
    for (const auto& r : rows) {
        if (!combined.text.empty()) combined.text += " ";
        combined.text += r.text;
        conf_sum += r.confidence;
    }
    combined.confidence = conf_sum / rows.size();
    combined.bbox = rows.front().bbox;
    return combined;
}

// Optimizacion: una sola pasada de OCR sobre la imagen completa,
// luego asignacion de cada linea de texto a la celda con la que tenga
// mayor interseccion de bbox. Mucho mas rapido que llamar a Tesseract
// por celda (evita N spawns de proceso y N cargas del modelo).
std::vector<std::vector<OCRResult>> OCRExtractor::extractFromGrid(
    const cv::Mat& img, const TableGrid& grid) {

    std::cout << "[OCRExtractor] OCR de tabla por interseccion de bbox ("
              << grid.cells.size() << " filas)..." << std::endl;

    auto all = extractAll(img);

    int num_rows = static_cast<int>(grid.cells.size());
    std::vector<std::vector<OCRResult>> table(num_rows);
    for (int r = 0; r < num_rows; ++r)
        table[r].resize(grid.cells[r].size());

    auto area = [](const cv::Rect& a) { return a.width * a.height; };

    for (const auto& line : all) {
        int best_r = -1, best_c = -1, best_area = 0;
        for (int r = 0; r < num_rows; ++r) {
            for (size_t c = 0; c < grid.cells[r].size(); ++c) {
                cv::Rect inter = line.bbox & grid.cells[r][c];
                int a = area(inter);
                if (a > best_area) {
                    best_area = a;
                    best_r = r;
                    best_c = static_cast<int>(c);
                }
            }
        }
        if (best_r >= 0 && best_area > 0) {
            auto& dst = table[best_r][best_c];
            if (!dst.text.empty()) dst.text += " ";
            dst.text += line.text;
            dst.confidence = (dst.confidence + line.confidence) / 2.0;
            dst.bbox = grid.cells[best_r][best_c];
        }
    }

    std::cout << "[OCRExtractor] Asignacion por bbox completada." << std::endl;
    return table;
}
