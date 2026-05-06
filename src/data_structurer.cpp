#include "data_structurer.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <regex>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

// ============================================================
// Normalizacion UTF-8: elimina acentos y convierte a minusculas
// ============================================================
std::string DataStructurer::normalize(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        // Secuencias UTF-8 de 2 bytes para acentos (C3xx)
        if (c == 0xC3 && i + 1 < text.size()) {
            unsigned char next = static_cast<unsigned char>(text[i + 1]);
            // a con acento (C3 A1), e (C3 A9), i (C3 AD), o (C3 B3), u (C3 BA)
            // A con acento (C3 81), E (C3 89), I (C3 8D), O (C3 93), U (C3 9A)
            // n tilde (C3 B1), N tilde (C3 91)
            switch (next) {
                case 0xA1: case 0x81: result += 'a'; break;
                case 0xA9: case 0x89: result += 'e'; break;
                case 0xAD: case 0x8D: result += 'i'; break;
                case 0xB3: case 0x93: result += 'o'; break;
                case 0xBA: case 0x9A: result += 'u'; break;
                case 0xB1: case 0x91: result += 'n'; break;
                default: result += c; result += text[i + 1]; break;
            }
            ++i; // saltar segundo byte
        } else {
            result += static_cast<char>(std::tolower(c));
        }
    }
    return result;
}

bool DataStructurer::fuzzyMatch(const std::string& text, const std::string& keywords) {
    std::string norm_text = normalize(text);
    std::istringstream iss(keywords);
    std::string word;
    while (iss >> word) {
        std::string norm_word = normalize(word);
        if (norm_text.find(norm_word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// ============================================================
// Parsing de numeros colombianos
// ============================================================
double DataStructurer::parseColombianNumber(const std::string& text) {
    // Formato: "4.382.067.330,00" o "$4.382.067.330,00"
    std::string clean;
    for (char c : text) {
        if (c == '$' || c == ' ' || c == '\t') continue;
        clean += c;
    }
    // Eliminar puntos de miles
    std::string no_dots;
    for (char c : clean) {
        if (c != '.') no_dots += c;
    }
    // Coma -> punto decimal
    for (char& c : no_dots) {
        if (c == ',') c = '.';
    }
    // Eliminar caracteres no numericos excepto punto y signo
    std::string numeric;
    for (char c : no_dots) {
        if (std::isdigit(c) || c == '.' || c == '-') numeric += c;
    }
    try {
        return std::stod(numeric);
    } catch (...) {
        return 0.0;
    }
}

double DataStructurer::parseColombianPercentage(const std::string& text) {
    // Formato: "11,66 % E.A." o "4,46%"
    std::string clean;
    for (char c : text) {
        if (c == '%' || c == ' ') continue;
        clean += c;
    }
    // Eliminar sufijos como "E.A.", "MV", etc
    std::string lower = normalize(clean);
    size_t pos = lower.find("e.a");
    if (pos != std::string::npos) clean = clean.substr(0, pos);
    pos = lower.find("ea");
    if (pos != std::string::npos && pos > 0) clean = clean.substr(0, pos);
    pos = lower.find("mv");
    if (pos != std::string::npos) clean = clean.substr(0, pos);

    // Coma -> punto
    for (char& c : clean) {
        if (c == ',') c = '.';
    }
    std::string numeric;
    for (char c : clean) {
        if (std::isdigit(c) || c == '.' || c == '-') numeric += c;
    }
    try {
        return std::stod(numeric);
    } catch (...) {
        return 0.0;
    }
}

std::string DataStructurer::cleanOCRText(const std::string& text) {
    std::string result = text;
    // Trim
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
        result.erase(result.begin());
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' ||
                                result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

// ============================================================
// VisualLine: agrupacion geometrica de bloques OCR
// ============================================================
std::string VisualLine::fullText() const {
    std::string result;
    for (const auto& block : blocks) {
        if (!result.empty()) result += " ";
        result += block.text;
    }
    return result;
}

std::string VisualLine::leftText() const {
    if (blocks.empty()) return "";
    // Primera mitad de bloques
    size_t mid = blocks.size() / 2;
    if (mid == 0) mid = 1;
    std::string result;
    for (size_t i = 0; i < mid; ++i) {
        if (!result.empty()) result += " ";
        result += blocks[i].text;
    }
    return result;
}

std::string VisualLine::rightText() const {
    if (blocks.empty()) return "";
    size_t mid = blocks.size() / 2;
    std::string result;
    for (size_t i = mid; i < blocks.size(); ++i) {
        if (!result.empty()) result += " ";
        result += blocks[i].text;
    }
    return result;
}

std::string VisualLine::moneyValue() const {
    // PRIORIDAD 1: Bloques con signo $ (mas confiable para valores monetarios)
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        if (t.find('$') != std::string::npos) {
            bool has_digit = false;
            for (char c : t) { if (std::isdigit(c)) { has_digit = true; break; } }
            if (has_digit) return t;
        }
    }

    // PRIORIDAD 2: Numeros colombianos grandes (>=2 puntos = separadores de miles)
    // Ejemplo: "1.997.903.323,18" o "4.382.067.330"
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        if (t.find('%') != std::string::npos) continue; // Saltar porcentajes
        int dot_count = 0;
        bool has_digit = false;
        for (char c : t) {
            if (c == '.') dot_count++;
            if (std::isdigit(c)) has_digit = true;
        }
        if (has_digit && dot_count >= 2) return t;
    }

    // PRIORIDAD 3: Numeros con separador, pero NO porcentajes
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        // Saltar si contiene %
        if (t.find('%') != std::string::npos) continue;
        // Saltar si el bloque siguiente es "%" o "E.A."
        if (i + 1 < static_cast<int>(blocks.size())) {
            std::string next = blocks[i + 1].text;
            std::transform(next.begin(), next.end(), next.begin(), ::tolower);
            if (next.find('%') != std::string::npos ||
                next.find("e.a") != std::string::npos ||
                next.find("ea") != std::string::npos) continue;
        }
        bool has_digit = false, has_separator = false;
        for (char c : t) {
            if (std::isdigit(c)) has_digit = true;
            if (c == '.' || c == ',') has_separator = true;
        }
        if (has_digit && has_separator) return t;
    }

    // PRIORIDAD 4: Bloque con digitos Y al menos un separador (punto o coma)
    // Excluye numeros planos como NITs (ej: "901127521") que no son valores monetarios
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        if (t.find('%') != std::string::npos) continue;
        bool has_digit = false, has_separator = false;
        for (char c : t) {
            if (std::isdigit(c)) has_digit = true;
            if (c == '.' || c == ',' || c == '$') has_separator = true;
        }
        if (has_digit && has_separator) return t;
    }
    return "";
}

std::string VisualLine::percentValue() const {
    // PRIORIDAD 1: Bloque que contiene "%" con un numero
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        if (t.find('%') != std::string::npos) {
            bool has_digit = false;
            for (char c : t) { if (std::isdigit(c)) { has_digit = true; break; } }
            if (has_digit) return t;
        }
    }

    // PRIORIDAD 2: Numero justo antes de "%" o "E.A."
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        std::string lower = blocks[i].text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find('%') != std::string::npos ||
            lower.find("e.a") != std::string::npos ||
            lower.find("ea") != std::string::npos) {
            // Buscar numero en el bloque anterior
            if (i > 0) {
                const std::string& prev = blocks[i - 1].text;
                bool has_digit = false;
                for (char c : prev) { if (std::isdigit(c)) { has_digit = true; break; } }
                if (has_digit) return prev;
            }
        }
    }

    // PRIORIDAD 3: Numero pequeno con coma (patron de porcentaje colombiano: "4,46")
    // Solo si NO parece un valor monetario grande (sin $ ni multiples puntos)
    for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i) {
        const std::string& t = blocks[i].text;
        if (t.find('$') != std::string::npos) continue; // No es porcentaje
        int dot_count = 0;
        bool has_digit = false, has_comma = false;
        for (char c : t) {
            if (c == '.') dot_count++;
            if (std::isdigit(c)) has_digit = true;
            if (c == ',') has_comma = true;
        }
        // Porcentaje: pocos caracteres, coma decimal, sin multiples puntos
        if (has_digit && has_comma && dot_count <= 1 && t.size() < 10) return t;
    }
    return "";
}

// ============================================================
// Agrupacion en lineas visuales por coordenada Y
// ============================================================
std::vector<VisualLine> DataStructurer::groupIntoLines(
    const std::vector<OCRResult>& ocr_data, int y_tolerance) {

    if (ocr_data.empty()) return {};

    // Copiar y ordenar por Y center
    struct BlockWithY {
        int y_center;
        const OCRResult* block;
    };
    std::vector<BlockWithY> sorted;
    for (const auto& b : ocr_data) {
        int yc = b.bbox.y + b.bbox.height / 2;
        sorted.push_back({yc, &b});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const BlockWithY& a, const BlockWithY& b) { return a.y_center < b.y_center; });

    // Agrupar por Y
    std::vector<VisualLine> lines;
    VisualLine current_line;
    current_line.y_center = sorted[0].y_center;
    current_line.blocks.push_back(*sorted[0].block);

    for (size_t i = 1; i < sorted.size(); ++i) {
        if (std::abs(sorted[i].y_center - current_line.y_center) <= y_tolerance) {
            current_line.blocks.push_back(*sorted[i].block);
            // Actualizar y_center como promedio
            int sum = 0;
            for (const auto& b : current_line.blocks) {
                sum += b.bbox.y + b.bbox.height / 2;
            }
            current_line.y_center = sum / static_cast<int>(current_line.blocks.size());
        } else {
            // Ordenar bloques por X dentro de la linea
            std::sort(current_line.blocks.begin(), current_line.blocks.end(),
                      [](const OCRResult& a, const OCRResult& b) {
                          return a.bbox.x < b.bbox.x;
                      });
            lines.push_back(current_line);
            current_line = VisualLine();
            current_line.y_center = sorted[i].y_center;
            current_line.blocks.push_back(*sorted[i].block);
        }
    }
    // Ultima linea
    std::sort(current_line.blocks.begin(), current_line.blocks.end(),
              [](const OCRResult& a, const OCRResult& b) {
                  return a.bbox.x < b.bbox.x;
              });
    lines.push_back(current_line);

    return lines;
}

const VisualLine* DataStructurer::findLineByLabel(
    const std::vector<VisualLine>& lines, const std::string& label) {
    for (const auto& line : lines) {
        if (fuzzyMatch(line.fullText(), label)) {
            return &line;
        }
    }
    return nullptr;
}

const VisualLine* DataStructurer::findLineByLabels(
    const std::vector<VisualLine>& lines,
    const std::vector<std::string>& labels) {
    for (const auto& label : labels) {
        auto* line = findLineByLabel(lines, label);
        if (line) return line;
    }
    return nullptr;
}

// ============================================================
// Emparejamiento espacial: busca el valor monetario mas cercano
// a la derecha de un bloque label, sin depender de lineas visuales
// ============================================================
std::string DataStructurer::findNearestMoneyRight(
    const std::vector<OCRResult>& ocr_data,
    const OCRResult& label_block,
    int y_tolerance) {

    int label_right = label_block.bbox.x + label_block.bbox.width;
    int label_cy = label_block.bbox.y + label_block.bbox.height / 2;

    std::string best_money;
    int best_dist = INT_MAX;

    for (const auto& block : ocr_data) {
        // Solo bloques a la derecha del label
        if (block.bbox.x < label_right - 10) continue;

        // Dentro de tolerancia vertical
        int block_cy = block.bbox.y + block.bbox.height / 2;
        if (std::abs(block_cy - label_cy) > y_tolerance) continue;

        const std::string& t = block.text;
        if (t.empty()) continue;

        // Verificar si parece valor monetario
        bool has_digit = false, has_dollar = false;
        int dot_count = 0;
        bool has_separator = false;
        for (char c : t) {
            if (std::isdigit(c)) has_digit = true;
            if (c == '$') has_dollar = true;
            if (c == '.') dot_count++;
            if (c == '.' || c == ',') has_separator = true;
        }

        // Saltar porcentajes
        if (t.find('%') != std::string::npos) continue;

        // Debe parecer monetario: tiene $ con digito, o formato colombiano (2+ puntos),
        // o al menos tiene digito con separador
        bool looks_monetary = false;
        if (has_dollar && has_digit) looks_monetary = true;
        else if (has_digit && dot_count >= 2) looks_monetary = true;
        else if (has_digit && has_separator) looks_monetary = true;

        if (!looks_monetary) continue;

        int dist = block.bbox.x - label_right;
        if (dist < 0) dist = 0;
        if (dist < best_dist) {
            best_dist = dist;
            best_money = t;
        }
    }

    return best_money;
}

std::string DataStructurer::extractMoney(const std::string& text) {
    // Extraer primer patron monetario del texto
    std::regex money_re(R"(\$?\s*[\d.,]+)");
    std::smatch match;
    if (std::regex_search(text, match, money_re)) {
        return match[0].str();
    }
    return "";
}

bool DataStructurer::hasMoney(const std::string& text) {
    return !extractMoney(text).empty();
}

bool DataStructurer::hasPercent(const std::string& text) {
    return text.find('%') != std::string::npos ||
           normalize(text).find("e.a") != std::string::npos;
}

// ============================================================
// Diagnostic dump
// ============================================================
static void dumpLines(const std::vector<VisualLine>& lines) {
    std::cout << "\n===== DUMP DE LINEAS VISUALES =====" << std::endl;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::cout << "[Y=" << lines[i].y_center << "] " << lines[i].fullText() << std::endl;
    }
    std::cout << "===== FIN DUMP =====\n" << std::endl;
}

// ============================================================
// buildResumen: Pagina 1 - Resumen del portafolio
// ============================================================
ResumenPortafolio DataStructurer::buildResumen(const std::vector<OCRResult>& ocr_data) {
    std::cout << "[DataStructurer] Construyendo resumen del portafolio..." << std::endl;
    ResumenPortafolio resumen;

    auto lines = groupIntoLines(ocr_data);
    dumpLines(lines);

    // Buscar campos del cliente usando ":" como separador
    for (const auto& line : lines) {
        std::string full = line.fullText();
        std::string norm = normalize(full);
        size_t colon = full.find(':');

        if (colon != std::string::npos) {
            std::string label = normalize(full.substr(0, colon));
            std::string value = cleanOCRText(full.substr(colon + 1));

            if (label.find("nombre") != std::string::npos ||
                label.find("cliente") != std::string::npos) {
                resumen.nombre_cliente = value;
            } else if (label.find("nit") != std::string::npos ||
                       label.find("identificacion") != std::string::npos) {
                resumen.nit = value;
            } else if (label.find("direccion") != std::string::npos) {
                resumen.direccion = value;
            } else if (label.find("ciudad") != std::string::npos) {
                resumen.ciudad = value;
            } else if (label.find("telefono") != std::string::npos) {
                resumen.telefono = value;
            } else if (label.find("asesor") != std::string::npos) {
                resumen.asesor = value;
            } else if (label.find("email") != std::string::npos ||
                       label.find("correo") != std::string::npos) {
                resumen.email_asesor = value;
            } else if (label.find("fecha") != std::string::npos) {
                resumen.fecha_extracto = value;
            }
        }
    }

    // Post-procesamiento: si nombre_cliente contiene un NIT (numero de 9+ digitos),
    // separarlo y asignarlo al campo nit si esta vacio
    if (resumen.nit.empty() && !resumen.nombre_cliente.empty()) {
        std::regex nit_re(R"((\d{9,}))");
        std::smatch nit_match;
        std::string nombre = resumen.nombre_cliente;
        if (std::regex_search(nombre, nit_match, nit_re)) {
            resumen.nit = nit_match[1].str();
            // Si el nombre ES solo el NIT, dejarlo como NIT y limpiar nombre
            std::string nombre_sin_nit = std::regex_replace(nombre, nit_re, "");
            nombre_sin_nit = cleanOCRText(nombre_sin_nit);
            if (!nombre_sin_nit.empty()) {
                resumen.nombre_cliente = nombre_sin_nit;
            }
            std::cout << "  [Resumen] NIT extraido del nombre: " << resumen.nit << std::endl;
        }
    }

    // Si nombre_cliente es puramente numerico (el NIT), buscar el nombre real
    // en las lineas que contienen "nombre" o "cliente"
    {
        bool is_numeric = !resumen.nombre_cliente.empty();
        for (char c : resumen.nombre_cliente) {
            if (!std::isdigit(c) && c != ' ' && c != '-') {
                is_numeric = false;
                break;
            }
        }
        if (is_numeric || resumen.nombre_cliente.empty()) {
            for (const auto& line : lines) {
                std::string norm = normalize(line.fullText());
                if (norm.find("nombre") != std::string::npos &&
                    norm.find("cliente") != std::string::npos) {
                    // Buscar bloques de texto no-numerico en esta linea
                    for (const auto& block : line.blocks) {
                        std::string t = cleanOCRText(block.text);
                        if (t.empty()) continue;
                        // Saltar si es numerico, es un label conocido, o muy corto
                        std::string tn = normalize(t);
                        if (tn.find("nombre") != std::string::npos ||
                            tn.find("cliente") != std::string::npos ||
                            tn.find("nit") != std::string::npos) continue;
                        bool all_digits = true;
                        for (char c : t) {
                            if (!std::isdigit(c) && c != ' ' && c != '-' && c != ':') {
                                all_digits = false;
                                break;
                            }
                        }
                        if (!all_digits && t.size() > 3) {
                            resumen.nombre_cliente = t;
                            std::cout << "  [Resumen] Nombre real encontrado: "
                                      << t << std::endl;
                            break;
                        }
                    }
                    if (!resumen.nombre_cliente.empty() &&
                        resumen.nombre_cliente != resumen.nit) break;
                }
            }
        }
    }

    // Fallback fecha_extracto: buscar patron "dd de Mes yyyy" en primeras lineas
    if (resumen.fecha_extracto.empty()) {
        std::regex date_re(R"((\d{1,2}\s+de\s+\w+\s+\d{4}))");
        for (size_t i = 0; i < std::min(lines.size(), size_t(8)); ++i) {
            std::string full = lines[i].fullText();
            std::smatch dm;
            if (std::regex_search(full, dm, date_re)) {
                resumen.fecha_extracto = dm[1].str();
                std::cout << "  [Resumen] Fecha extraida por patron: "
                          << resumen.fecha_extracto << std::endl;
                break;
            }
        }
    }

    // Buscar activos por etiqueta con valor monetario en la misma linea
    struct AssetLabel {
        std::string key;
        std::vector<std::string> labels;
    };
    std::vector<AssetLabel> asset_labels = {
        {"Renta Fija", {"renta fija"}},
        {"FIC", {"fic", "fondo inversion colectiva", "fondo inversion", "fondo de inversion", "fondos inversion colectiva"}},
        {"Saldos en Efectivo", {"saldos efectivo", "efectivo"}},
        {"Acciones", {"acciones"}},
        {"Derivados", {"derivados"}},
        {"Divisas", {"divisas"}},
    };

    for (const auto& asset : asset_labels) {
        bool found = false;

        // Intento 1: buscar en lineas visuales (label + moneyValue en misma linea)
        auto* line = findLineByLabels(lines, asset.labels);
        if (line) {
            std::string money = line->moneyValue();
            if (!money.empty()) {
                double val = parseColombianNumber(money);
                if (val > 0) {
                    resumen.activos[asset.key] = val;
                    found = true;
                }
            }
        }

        // Intento 2: emparejamiento espacial sobre bloques OCR crudos
        // Se ejecuta si el intento 1 no encontro valor (incluso si no encontro linea)
        // Busca bloques que contengan CUALQUIER palabra clave del label (no todas)
        // porque el OCR puede fragmentar "Fondo de Inversion Colectiva" en multiples bloques
        if (!found) {
            // Construir lista de palabras clave individuales para este activo
            std::vector<std::string> keywords;
            for (const auto& lbl : asset.labels) {
                // Cada label puede tener multiples palabras; usar cada una como keyword
                keywords.push_back(lbl);
            }

            for (const auto& block : ocr_data) {
                std::string block_norm = normalize(block.text);
                if (block_norm.size() < 3) continue;

                // Verificar si algun label matchea el bloque completo
                bool matches = false;
                for (const auto& lbl : asset.labels) {
                    if (fuzzyMatch(block_norm, lbl)) { matches = true; break; }
                }

                // Fallback: si el bloque contiene una palabra clave distintiva
                // "colectiva", "fic" son suficientemente unicas
                if (!matches) {
                    if (asset.key == "FIC") {
                        if (block_norm.find("colectiva") != std::string::npos ||
                            block_norm.find("fic") != std::string::npos ||
                            (block_norm.find("fondo") != std::string::npos &&
                             block_norm.find("inversion") != std::string::npos)) {
                            matches = true;
                        }
                    }
                }

                if (matches) {
                    std::string mv = findNearestMoneyRight(ocr_data, block, 40);
                    if (!mv.empty()) {
                        double val = parseColombianNumber(mv);
                        if (val > 0) {
                            resumen.activos[asset.key] = val;
                            found = true;
                            std::cout << "  [Resumen] " << asset.key
                                      << " = " << mv << " -> " << val
                                      << " (spatial pairing from block: '"
                                      << block.text << "')" << std::endl;
                        }
                    }
                    if (found) break;
                }
            }
        }
    }

    // Total activos
    auto* total_activos_line = findLineByLabels(lines, {"total activos"});
    if (total_activos_line) {
        std::string money = total_activos_line->moneyValue();
        resumen.total_activos = parseColombianNumber(money);
    }

    // Total pasivos
    auto* total_pasivos_line = findLineByLabels(lines, {"total pasivos"});
    if (total_pasivos_line) {
        std::string money = total_pasivos_line->moneyValue();
        resumen.total_pasivos = parseColombianNumber(money);
    }

    // Total portafolio
    auto* total_port_line = findLineByLabels(lines, {"total portafolio", "total cartera"});
    if (total_port_line) {
        std::string money = total_port_line->moneyValue();
        resumen.total_portafolio = parseColombianNumber(money);
    }

    std::cout << "[DataStructurer] Resumen construido. Cliente: " << resumen.nombre_cliente
              << ", Total: " << resumen.total_portafolio << std::endl;

    return resumen;
}

// ============================================================
// buildRentaFija: Pagina 3 - tabla OCR de instrumentos
// ============================================================
std::vector<InstrumentoRentaFija> DataStructurer::buildRentaFija(
    const std::vector<std::vector<OCRResult>>& table_data) {

    std::cout << "[DataStructurer] Construyendo instrumentos de renta fija..." << std::endl;
    std::vector<InstrumentoRentaFija> instrumentos;

    // Mapeo de columnas por indice:
    // 0=nemotecnico, 1=emision, 2=vencimiento, 3=compra,
    // 4=tasa facial, 5=periodicidad, 6=nominal,
    // 7=tasa negociacion, 8=tasa valoracion, 9=valor mercado
    for (size_t r = 0; r < table_data.size(); ++r) {
        const auto& row = table_data[r];
        if (row.empty()) continue;

        // Saltar headers y filas vacias
        std::string first_cell = cleanOCRText(row[0].text);
        std::string norm_first = normalize(first_cell);
        if (norm_first.empty()) continue;
        // Saltar filas de encabezado (palabras clave de header)
        if (norm_first.find("nemo") != std::string::npos ||
            norm_first.find("titulo") != std::string::npos ||
            norm_first.find("emision") != std::string::npos ||
            norm_first.find("fecha") != std::string::npos ||
            norm_first.find("tasa") != std::string::npos ||
            norm_first.find("valor") != std::string::npos ||
            norm_first.find("periodicidad") != std::string::npos) {
            continue;
        }
        // Saltar filas de totales
        if (norm_first.find("total") != std::string::npos ||
            norm_first.find("subtotal") != std::string::npos) {
            continue;
        }

        InstrumentoRentaFija inst;
        if (row.size() > 0) inst.nemotecnico = cleanOCRText(row[0].text);
        if (row.size() > 1) inst.fecha_emision = cleanOCRText(row[1].text);
        if (row.size() > 2) inst.fecha_vencimiento = cleanOCRText(row[2].text);
        if (row.size() > 3) inst.fecha_compra = cleanOCRText(row[3].text);
        if (row.size() > 4) inst.tasa_facial = parseColombianPercentage(row[4].text);
        if (row.size() > 5) inst.periodicidad = cleanOCRText(row[5].text);
        if (row.size() > 6) inst.valor_nominal = parseColombianNumber(row[6].text);
        if (row.size() > 7) inst.tasa_negociacion = parseColombianPercentage(row[7].text);
        if (row.size() > 8) inst.tasa_valoracion = parseColombianPercentage(row[8].text);
        if (row.size() > 9) inst.valor_mercado = parseColombianNumber(row[9].text);

        // Solo agregar si tiene nemotecnico valido
        if (!inst.nemotecnico.empty() && inst.nemotecnico.size() > 2) {
            instrumentos.push_back(inst);
            std::cout << "  [RF] " << inst.nemotecnico
                      << " -> Mercado: " << inst.valor_mercado << std::endl;
        }
    }

    std::cout << "[DataStructurer] " << instrumentos.size()
              << " instrumentos de renta fija encontrados" << std::endl;
    return instrumentos;
}

// ============================================================
// buildRentaFijaFromLines: Fallback usando OCR completo + lineas visuales
// Cuando detectAllTables no encuentra la tabla, usa agrupacion geometrica
// ============================================================
std::vector<InstrumentoRentaFija> DataStructurer::buildRentaFijaFromLines(
    const std::vector<OCRResult>& ocr_data) {

    std::cout << "[DataStructurer] Fallback: construyendo renta fija desde lineas visuales..."
              << std::endl;
    std::vector<InstrumentoRentaFija> instrumentos;

    auto lines = groupIntoLines(ocr_data, 15);
    dumpLines(lines);

    // Buscar lineas que contengan nemotecnicos de CDT
    // Los CDTs tienen patrones como "CDT", "CDTDTF", etc.
    // Cada linea de instrumento tiene: nemotecnico, fechas, tasas, valores
    bool in_renta_fija_section = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string full = lines[i].fullText();
        std::string norm = normalize(full);

        // Detectar inicio de seccion de renta fija
        if (norm.find("renta fija") != std::string::npos ||
            norm.find("nemotecnico") != std::string::npos ||
            norm.find("nemo") != std::string::npos) {
            in_renta_fija_section = true;
            continue;
        }

        // Detectar fin de seccion (otra seccion empieza)
        if (in_renta_fija_section &&
            (norm.find("fondos") != std::string::npos ||
             norm.find("total") != std::string::npos && norm.find("renta") != std::string::npos)) {
            // Podria ser "Total Renta Fija" - extraer valor pero no como instrumento
            continue;
        }

        if (!in_renta_fija_section) continue;

        // Buscar lineas con patron de CDT/instrumento
        // Un instrumento tiene al menos: nemotecnico (texto con letras), fechas, y numeros
        bool has_cdt_pattern = (norm.find("cdt") != std::string::npos ||
                                norm.find("dtf") != std::string::npos ||
                                norm.find("tif") != std::string::npos ||
                                norm.find("tes") != std::string::npos);

        // Tambien detectar por patron: bloques con fechas (dd/mm/yyyy o similar)
        int date_count = 0;
        int number_count = 0;
        for (const auto& block : lines[i].blocks) {
            std::string bt = block.text;
            // Contar fechas (patron: digitos/digitos/digitos o digitos-digitos-digitos)
            if ((bt.size() >= 8 && bt.size() <= 12) &&
                (bt.find('/') != std::string::npos || bt.find('-') != std::string::npos)) {
                int slash_count = 0;
                for (char c : bt) if (c == '/' || c == '-') slash_count++;
                if (slash_count >= 1) date_count++;
            }
            // Contar numeros con separadores
            bool bd = false, bs = false;
            for (char c : bt) {
                if (std::isdigit(c)) bd = true;
                if (c == '.' || c == ',') bs = true;
            }
            if (bd && bs) number_count++;
        }

        if (has_cdt_pattern || (date_count >= 2 && number_count >= 2)) {
            InstrumentoRentaFija inst;

            // Los bloques estan ordenados por X (izquierda a derecha)
            // Intentar mapear: nemotecnico, fechas, tasas, valores
            std::vector<std::string> parts;
            for (const auto& block : lines[i].blocks) {
                std::string cleaned = cleanOCRText(block.text);
                if (!cleaned.empty()) parts.push_back(cleaned);
            }

            if (parts.empty()) continue;

            // Primer bloque es nemotecnico
            inst.nemotecnico = parts[0];

            // Buscar fechas, tasas y valores en el resto
            std::vector<std::string> fechas;
            std::vector<double> tasas;
            std::vector<double> valores_grandes;

            for (size_t p = 1; p < parts.size(); ++p) {
                const std::string& part = parts[p];
                // Es fecha?
                if ((part.find('/') != std::string::npos || part.find('-') != std::string::npos) &&
                    part.size() >= 8 && part.size() <= 12) {
                    fechas.push_back(part);
                    continue;
                }
                // Es numero?
                bool has_d = false;
                int dots = 0;
                for (char c : part) {
                    if (std::isdigit(c)) has_d = true;
                    if (c == '.') dots++;
                }
                if (!has_d) continue;

                double val = parseColombianNumber(part);
                if (dots >= 2 || val > 100000) {
                    // Valor grande (nominal o mercado)
                    valores_grandes.push_back(val);
                } else if (val > 0 && val < 100) {
                    // Probablemente una tasa
                    tasas.push_back(val);
                } else {
                    // Podria ser porcentaje escrito como "9.761800"
                    double pval = parseColombianPercentage(part);
                    if (pval > 0 && pval < 100) tasas.push_back(pval);
                    else if (val > 0) valores_grandes.push_back(val);
                }
            }

            // Asignar fechas
            if (fechas.size() > 0) inst.fecha_emision = fechas[0];
            if (fechas.size() > 1) inst.fecha_vencimiento = fechas[1];
            if (fechas.size() > 2) inst.fecha_compra = fechas[2];

            // Asignar tasas (facial, negociacion, valoracion)
            if (tasas.size() > 0) inst.tasa_facial = tasas[0];
            if (tasas.size() > 1) inst.tasa_negociacion = tasas[1];
            if (tasas.size() > 2) inst.tasa_valoracion = tasas[2];

            // Asignar valores (nominal, mercado)
            if (valores_grandes.size() > 0) inst.valor_nominal = valores_grandes[0];
            if (valores_grandes.size() > 1) inst.valor_mercado = valores_grandes[1];
            else if (valores_grandes.size() == 1) inst.valor_mercado = valores_grandes[0];

            if (!inst.nemotecnico.empty() && inst.nemotecnico.size() > 2) {
                instrumentos.push_back(inst);
                std::cout << "  [RF-Lines] " << inst.nemotecnico
                          << " TF=" << inst.tasa_facial
                          << " TN=" << inst.tasa_negociacion
                          << " TV=" << inst.tasa_valoracion
                          << " VM=" << inst.valor_mercado << std::endl;
            }
        }
    }

    std::cout << "[DataStructurer] Fallback: " << instrumentos.size()
              << " instrumentos extraidos desde lineas" << std::endl;
    return instrumentos;
}

// ============================================================
// buildSaldosEfectivo: Pagina 3 - tabla de saldos
// ============================================================
std::vector<SaldoEfectivo> DataStructurer::buildSaldosEfectivo(
    const std::vector<std::vector<OCRResult>>& table_data) {

    std::cout << "[DataStructurer] Construyendo saldos en efectivo..." << std::endl;
    std::vector<SaldoEfectivo> saldos;

    // 5 columnas: cuenta, disponible, canje, bloqueado, total
    for (size_t r = 0; r < table_data.size(); ++r) {
        const auto& row = table_data[r];
        if (row.empty()) continue;

        std::string first_cell = cleanOCRText(row[0].text);
        std::string norm = normalize(first_cell);
        if (norm.find("cuenta") != std::string::npos ||
            norm.find("saldo") != std::string::npos ||
            first_cell.empty()) {
            continue; // header
        }

        SaldoEfectivo s;
        if (row.size() > 0) s.cuenta = cleanOCRText(row[0].text);
        if (row.size() > 1) s.saldo_disponible = parseColombianNumber(row[1].text);
        if (row.size() > 2) s.saldo_canje = parseColombianNumber(row[2].text);
        if (row.size() > 3) s.saldo_bloqueado = parseColombianNumber(row[3].text);
        if (row.size() > 4) s.saldo_total = parseColombianNumber(row[4].text);

        if (!s.cuenta.empty()) {
            saldos.push_back(s);
        }
    }

    std::cout << "[DataStructurer] " << saldos.size() << " saldos encontrados" << std::endl;
    return saldos;
}

// ============================================================
// buildFondo: Pagina 4 - Extracto de Fondos
// ============================================================
FondoInversion DataStructurer::buildFondo(const std::vector<OCRResult>& ocr_data) {
    std::cout << "[DataStructurer] Construyendo datos de fondo de inversion..." << std::endl;
    FondoInversion fondo;

    auto lines = groupIntoLines(ocr_data);
    dumpLines(lines);

    // Extraer campos con ":" (nombre fondo, codigo, etc.)
    for (const auto& line : lines) {
        std::string full = line.fullText();
        size_t colon = full.find(':');

        if (colon != std::string::npos) {
            std::string label = normalize(full.substr(0, colon));
            std::string value = cleanOCRText(full.substr(colon + 1));

            if (label.find("nombre") != std::string::npos &&
                label.find("fondo") != std::string::npos) {
                fondo.nombre_fondo = value;
            } else if (label.find("codigo") != std::string::npos) {
                fondo.codigo = value;
            } else if (label.find("inversionista") != std::string::npos) {
                fondo.nombre_inversionista = value;
            } else if (label.find("identificacion") != std::string::npos) {
                fondo.identificacion = value;
            } else if (label.find("objetivo") != std::string::npos) {
                fondo.objetivo_inversion = value;
            } else if (label.find("administrador") != std::string::npos) {
                fondo.administrador = value;
            }
        }
    }

    // Rentabilidades historicas (usan lineas visuales — funciona bien)
    for (const auto& line : lines) {
        std::string norm = normalize(line.fullText());
        struct RentLabel {
            std::string key;
            std::string search;
        };
        std::vector<RentLabel> rent_labels = {
            {"Mensual", "mensual"},
            {"Semestral", "semestral"},
            {"Ano corrido", "ano corrido"},
            {"Ultimo ano", "ultimo ano"},
            {"Ultimos 2 anos", "ultimos 2"},
            {"Ultimos 3 anos", "ultimos 3"},
        };
        for (const auto& rl : rent_labels) {
            if (fuzzyMatch(norm, rl.search)) {
                std::string pval = line.percentValue();
                if (!pval.empty()) {
                    fondo.rentabilidades_historicas[rl.key] = parseColombianPercentage(pval);
                    std::cout << "  [Fondo] Rentabilidad " << rl.key << " = " << pval << std::endl;
                }
                break;
            }
        }
    }

    // ============================================================
    // Movimientos: emparejamiento espacial sobre bloques OCR crudos
    // Busca cada label en los bloques OCR y empareja con el valor
    // monetario mas cercano a su derecha (sin depender de lineas visuales)
    // ============================================================
    struct MovField {
        std::string label;
        double* target;
        std::string log_name;
    };
    std::vector<MovField> mov_fields = {
        {"saldo anterior", &fondo.saldo_anterior, "Saldo anterior"},
        {"adiciones", &fondo.adiciones, "Adiciones"},
        {"retiros", &fondo.retiros, "Retiros"},
        {"rendimientos", &fondo.rendimientos, "Rendimientos"},
        {"nuevo saldo", &fondo.nuevo_saldo, "Nuevo saldo"},
        {"unidades", &fondo.unidades, "Unidades"},
        {"valor unidad", &fondo.valor_unidad_final, "Valor unidad"},
    };

    for (const auto& field : mov_fields) {
        // Buscar bloques OCR que contengan el label
        for (const auto& block : ocr_data) {
            std::string block_norm = normalize(block.text);
            // Saltar si contiene "rentab" (evitar confundir rendimientos con rentabilidad)
            if (block_norm.find("rentab") != std::string::npos) continue;

            if (fuzzyMatch(block_norm, field.label)) {
                // Encontramos el bloque del label, buscar valor monetario a su derecha
                std::string mv = findNearestMoneyRight(ocr_data, block, 40);
                if (!mv.empty()) {
                    double val = parseColombianNumber(mv);
                    *(field.target) = val;
                    std::cout << "  [Fondo] " << field.log_name << " = " << mv
                              << " -> " << val << " (spatial pairing)" << std::endl;
                }
                break; // Solo usar la primera coincidencia del label
            }
        }
    }

    std::cout << "[DataStructurer] Fondo: " << fondo.nombre_fondo
              << ", Nuevo saldo: " << fondo.nuevo_saldo << std::endl;

    return fondo;
}

// ============================================================
// Deteccion de tablas basada en bounding boxes del OCR
// ============================================================
std::vector<DataStructurer::OCRTable> DataStructurer::detectTablesFromOCR(
    const std::vector<OCRResult>& ocr_data, int img_width) {

    std::cout << "[DataStructurer] Detectando tablas desde OCR ("
              << ocr_data.size() << " bloques)..." << std::endl;

    if (ocr_data.empty()) return {};

    // Paso 1: Agrupar todos los bloques en lineas visuales (tolerancia ajustada)
    auto all_lines = groupIntoLines(ocr_data, 15);
    std::cout << "  " << all_lines.size() << " lineas visuales agrupadas" << std::endl;

    if (all_lines.empty()) return {};

    // Paso 2: Detectar gaps verticales grandes para separar regiones (tablas)
    // Un gap es la distancia Y entre dos lineas consecutivas
    // Si el gap es significativamente mayor al promedio, es un separador de tabla
    std::vector<int> gaps;
    for (size_t i = 1; i < all_lines.size(); ++i) {
        int gap = all_lines[i].y_center - all_lines[i - 1].y_center;
        gaps.push_back(gap);
    }

    // Calcular gap promedio y umbral
    double avg_gap = 0;
    for (int g : gaps) avg_gap += g;
    if (!gaps.empty()) avg_gap /= gaps.size();
    // Un gap >= 2.5x el promedio indica separacion de tabla
    double gap_threshold = std::max(avg_gap * 2.5, 40.0);

    std::cout << "  Gap promedio: " << avg_gap << ", umbral: " << gap_threshold << std::endl;

    // Paso 3: Dividir lineas en regiones por gaps
    std::vector<std::vector<VisualLine>> regions;
    std::vector<VisualLine> current_region;
    current_region.push_back(all_lines[0]);

    for (size_t i = 1; i < all_lines.size(); ++i) {
        int gap = all_lines[i].y_center - all_lines[i - 1].y_center;
        if (gap > gap_threshold && !current_region.empty()) {
            regions.push_back(current_region);
            current_region.clear();
        }
        current_region.push_back(all_lines[i]);
    }
    if (!current_region.empty()) {
        regions.push_back(current_region);
    }

    std::cout << "  " << regions.size() << " regiones detectadas por gaps" << std::endl;

    // Paso 4: Para cada region, determinar si es tabla (filas con columnas consistentes)
    // y clasificarla por contenido
    std::vector<OCRTable> tables;

    for (size_t r = 0; r < regions.size(); ++r) {
        const auto& region = regions[r];
        if (region.size() < 2) continue; // Necesita al menos header + 1 fila

        // Contar bloques por linea para ver si hay estructura tabular
        std::vector<int> blocks_per_line;
        for (const auto& line : region) {
            blocks_per_line.push_back(static_cast<int>(line.blocks.size()));
        }

        // Una tabla tiene lineas con cantidad similar de bloques (>= 3)
        int max_blocks = *std::max_element(blocks_per_line.begin(), blocks_per_line.end());
        int consistent_count = 0;
        for (int b : blocks_per_line) {
            // Tolerancia: al menos 60% del maximo de bloques
            if (b >= std::max(3, max_blocks * 6 / 10)) consistent_count++;
        }

        // Si al menos 50% de las lineas tienen estructura consistente, es tabla
        bool is_table = (consistent_count >= static_cast<int>(region.size()) / 2) && max_blocks >= 3;

        if (!is_table && region.size() >= 2) {
            // Fallback: si tiene pocas lineas pero contenido de tabla conocido
            std::string first_text = normalize(region[0].fullText());
            if (first_text.find("saldo") != std::string::npos ||
                first_text.find("cuenta") != std::string::npos ||
                first_text.find("nemo") != std::string::npos ||
                first_text.find("renta") != std::string::npos ||
                first_text.find("fondo") != std::string::npos ||
                first_text.find("fic") != std::string::npos) {
                is_table = true;
            }
        }

        if (!is_table) continue;

        OCRTable table;
        table.rows = region;
        table.num_cols = max_blocks;

        // Calcular bounds
        int min_x = INT_MAX, min_y = INT_MAX, max_x = 0, max_y = 0;
        for (const auto& line : region) {
            for (const auto& block : line.blocks) {
                min_x = std::min(min_x, block.bbox.x);
                min_y = std::min(min_y, block.bbox.y);
                max_x = std::max(max_x, block.bbox.x + block.bbox.width);
                max_y = std::max(max_y, block.bbox.y + block.bbox.height);
            }
        }
        table.bounds = cv::Rect(min_x, min_y, max_x - min_x, max_y - min_y);

        // Clasificar por contenido
        std::string all_text;
        for (const auto& line : region) {
            all_text += normalize(line.fullText()) + " ";
        }

        if (all_text.find("nemo") != std::string::npos ||
            all_text.find("cdt") != std::string::npos ||
            all_text.find("facial") != std::string::npos ||
            all_text.find("valoracion") != std::string::npos) {
            table.type = "renta_fija";
        } else if (all_text.find("cuenta") != std::string::npos ||
                   (all_text.find("saldo") != std::string::npos &&
                    all_text.find("disponible") != std::string::npos)) {
            table.type = "saldos";
        } else if (all_text.find("fic") != std::string::npos ||
                   all_text.find("fondo") != std::string::npos ||
                   all_text.find("participacion") != std::string::npos) {
            table.type = "fic_resumen";
        } else {
            table.type = "unknown";
        }

        std::cout << "  Tabla " << (tables.size() + 1) << ": tipo=" << table.type
                  << ", " << region.size() << " filas, ~" << max_blocks << " cols"
                  << std::endl;

        tables.push_back(table);
    }

    std::cout << "[DataStructurer] " << tables.size()
              << " tablas detectadas desde OCR" << std::endl;
    return tables;
}

std::vector<std::vector<OCRResult>> DataStructurer::ocrTableToGrid(const OCRTable& table) {
    // Convertir OCRTable a formato grid (vector de filas, cada fila es vector de celdas)
    // compatible con buildRentaFija() y buildSaldosEfectivo()
    if (table.rows.empty()) return {};

    // Paso 1: Determinar posiciones X de columnas usando todos los bloques
    // Recopilar todas las posiciones X centrales
    std::vector<int> all_x_centers;
    for (const auto& row : table.rows) {
        for (const auto& block : row.blocks) {
            all_x_centers.push_back(block.bbox.x + block.bbox.width / 2);
        }
    }
    if (all_x_centers.empty()) return {};

    // Clusterizar posiciones X para encontrar columnas
    std::sort(all_x_centers.begin(), all_x_centers.end());

    // Umbral de clustering: bloques con centro X a menos de esta distancia son misma columna
    int x_threshold = 40;
    std::vector<int> col_centers;
    int current_sum = all_x_centers[0];
    int current_count = 1;

    for (size_t i = 1; i < all_x_centers.size(); ++i) {
        if (all_x_centers[i] - all_x_centers[i - 1] <= x_threshold) {
            current_sum += all_x_centers[i];
            current_count++;
        } else {
            col_centers.push_back(current_sum / current_count);
            current_sum = all_x_centers[i];
            current_count = 1;
        }
    }
    col_centers.push_back(current_sum / current_count);

    int num_cols = static_cast<int>(col_centers.size());
    std::cout << "  [ocrTableToGrid] " << num_cols << " columnas detectadas" << std::endl;

    // Paso 2: Para cada fila, asignar cada bloque a la columna mas cercana
    std::vector<std::vector<OCRResult>> grid;
    for (const auto& row : table.rows) {
        std::vector<OCRResult> grid_row(num_cols);
        // Inicializar celdas vacias
        for (int c = 0; c < num_cols; ++c) {
            grid_row[c].text = "";
            grid_row[c].confidence = 0.0;
        }

        for (const auto& block : row.blocks) {
            int block_center_x = block.bbox.x + block.bbox.width / 2;
            // Encontrar columna mas cercana
            int best_col = 0;
            int best_dist = std::abs(block_center_x - col_centers[0]);
            for (int c = 1; c < num_cols; ++c) {
                int dist = std::abs(block_center_x - col_centers[c]);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_col = c;
                }
            }
            // Si la celda ya tiene texto, concatenar
            if (!grid_row[best_col].text.empty()) {
                grid_row[best_col].text += " ";
            }
            grid_row[best_col].text += block.text;
            grid_row[best_col].confidence = std::max(grid_row[best_col].confidence,
                                                      block.confidence);
            // Usar bbox del primer bloque asignado
            if (grid_row[best_col].bbox.width == 0) {
                grid_row[best_col].bbox = block.bbox;
            }
        }
        grid.push_back(grid_row);
    }

    return grid;
}

// ============================================================
// Validacion
// ============================================================
bool DataStructurer::validate(const ExtractoCompleto& extracto) {
    validation_errors_.clear();

    if (extracto.resumen.nombre_cliente.empty()) {
        validation_errors_.push_back("Nombre del cliente no encontrado");
    }
    if (extracto.resumen.total_portafolio <= 0) {
        validation_errors_.push_back("Total portafolio es 0 o negativo");
    }
    if (extracto.renta_fija.empty()) {
        validation_errors_.push_back("No se encontraron instrumentos de renta fija");
    }

    // Verificar que la suma de activos sea coherente
    double suma_activos = 0;
    for (const auto& [key, val] : extracto.resumen.activos) {
        suma_activos += val;
    }
    if (extracto.resumen.total_activos > 0 && suma_activos > 0) {
        double diff = std::abs(suma_activos - extracto.resumen.total_activos);
        double tolerance = extracto.resumen.total_activos * 0.05; // 5%
        if (diff > tolerance) {
            validation_errors_.push_back(
                "Suma de activos (" + std::to_string(suma_activos) +
                ") difiere del total (" + std::to_string(extracto.resumen.total_activos) +
                ") en mas del 5%");
        }
    }

    for (const auto& inst : extracto.renta_fija) {
        if (inst.valor_mercado <= 0) {
            validation_errors_.push_back(
                "Instrumento " + inst.nemotecnico + " tiene valor mercado <= 0");
        }
    }

    if (!validation_errors_.empty()) {
        std::cout << "[DataStructurer] Validacion: " << validation_errors_.size()
                  << " advertencias" << std::endl;
        for (const auto& err : validation_errors_) {
            std::cout << "  WARN: " << err << std::endl;
        }
    } else {
        std::cout << "[DataStructurer] Validacion exitosa" << std::endl;
    }

    return validation_errors_.empty();
}

std::vector<std::string> DataStructurer::getValidationErrors() const {
    return validation_errors_;
}

// ============================================================
// Serializacion JSON
// ============================================================
json ResumenPortafolio::to_json() const {
    json j;
    j["fecha_extracto"] = fecha_extracto;
    j["nombre_cliente"] = nombre_cliente;
    j["nit"] = nit;
    j["direccion"] = direccion;
    j["ciudad"] = ciudad;
    j["telefono"] = telefono;
    j["asesor"] = asesor;
    j["email_asesor"] = email_asesor;
    j["activos"] = activos;
    j["total_activos"] = total_activos;
    j["pasivos"] = pasivos;
    j["total_pasivos"] = total_pasivos;
    j["total_portafolio"] = total_portafolio;
    return j;
}

json InstrumentoRentaFija::to_json() const {
    return {
        {"nemotecnico", nemotecnico},
        {"fecha_emision", fecha_emision},
        {"fecha_vencimiento", fecha_vencimiento},
        {"fecha_compra", fecha_compra},
        {"tasa_facial", tasa_facial},
        {"periodicidad", periodicidad},
        {"valor_nominal", valor_nominal},
        {"tasa_negociacion", tasa_negociacion},
        {"tasa_valoracion", tasa_valoracion},
        {"valor_mercado", valor_mercado}
    };
}

json SaldoEfectivo::to_json() const {
    return {
        {"cuenta", cuenta},
        {"saldo_disponible", saldo_disponible},
        {"saldo_canje", saldo_canje},
        {"saldo_bloqueado", saldo_bloqueado},
        {"saldo_total", saldo_total}
    };
}

json FondoInversion::to_json() const {
    json j;
    j["nombre_fondo"] = nombre_fondo;
    j["codigo"] = codigo;
    j["nombre_inversionista"] = nombre_inversionista;
    j["identificacion"] = identificacion;
    j["objetivo_inversion"] = objetivo_inversion;
    j["administrador"] = administrador;
    j["rentabilidad_valor_unidad"] = rentabilidad_valor_unidad;
    j["valor_unidad_final"] = valor_unidad_final;
    j["fecha_inicio_extracto"] = fecha_inicio_extracto;
    j["fecha_fin_extracto"] = fecha_fin_extracto;
    j["saldo_anterior"] = saldo_anterior;
    j["adiciones"] = adiciones;
    j["retiros"] = retiros;
    j["rendimientos"] = rendimientos;
    j["nuevo_saldo"] = nuevo_saldo;
    j["unidades"] = unidades;
    j["rentabilidades_historicas"] = rentabilidades_historicas;
    return j;
}

json ExtractoCompleto::to_json() const {
    json j;
    j["resumen"] = resumen.to_json();

    json saldos_arr = json::array();
    for (const auto& s : saldos_efectivo) saldos_arr.push_back(s.to_json());
    j["saldos_efectivo"] = saldos_arr;

    json rf_arr = json::array();
    for (const auto& r : renta_fija) rf_arr.push_back(r.to_json());
    j["renta_fija"] = rf_arr;

    json fondos_arr = json::array();
    for (const auto& f : fondos) fondos_arr.push_back(f.to_json());
    j["fondos"] = fondos_arr;

    json tx_arr = json::array();
    for (const auto& t : transacciones) tx_arr.push_back(t.to_json());
    j["transacciones"] = tx_arr;

    j["currency"] = source_currency;

    return j;
}

json Transaction::to_json() const {
    json j;
    j["date"] = date;
    if (!value_date.empty()) j["value_date"] = value_date;
    j["description"] = description;
    if (!reference.empty()) j["reference"] = reference;
    if (!category.empty())  j["category"]  = category;
    j["debit"]  = debit;
    j["credit"] = credit;
    if (has_balance_after) j["balance_after"] = balance_after;
    return j;
}

json CuentaTransacciones::to_json() const {
    json j;
    j["account_id"]   = account_id;
    if (!account_type.empty()) j["account_type"] = account_type;
    if (!title.empty())        j["title"]        = title;
    j["currency"] = currency;
    json items_arr = json::array();
    for (const auto& it : items) items_arr.push_back(it.to_json());
    j["items"] = items_arr;
    if (has_totals) {
        j["totals"] = {
            {"debits",         total_debits},
            {"credits",        total_credits},
            {"net",            net},
            {"ending_balance", ending_balance}
        };
    }
    return j;
}

bool ExtractoCompleto::saveToFile(const std::string& path) const {
    try {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << to_json().dump(2);
        file.close();
        std::cout << "[ExtractoCompleto] Guardado en: " << path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ExtractoCompleto] Error guardando: " << e.what() << std::endl;
        return false;
    }
}
