#include "standard_parser.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <iostream>

// =============================================================================
// Helpers locales
// =============================================================================
namespace {

std::string toUpperAscii(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Distancia de Levenshtein (numero minimo de inserciones/eliminaciones/
// substituciones para convertir a en b). Usada para fuzzy matching de
// labels cuando OCR confunde caracteres en imagenes de baja resolucion
// (ej. 'Cliente' -> 'Ciela' o 'C ierfar', 'RESUMEN' -> 'RESUBEN').
int levenshtein(const std::string& a, const std::string& b) {
    int m = static_cast<int>(a.size()), n = static_cast<int>(b.size());
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;
    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({curr[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// True si en algun punto del haystack aparece needle con distancia <= max_dist.
// Compara substrings de longitud parecida a needle. O(haystack * needle).
bool fuzzyContains(const std::string& haystack, const std::string& needle,
                    int max_dist = 2) {
    std::string h = toUpperAscii(haystack);
    std::string n = toUpperAscii(needle);
    if (h.size() < n.size()) {
        return levenshtein(h, n) <= max_dist;
    }
    // Sliding window: probar cada substring de h del tamano de n (+- max_dist)
    int nl = static_cast<int>(n.size());
    for (size_t i = 0; i + n.size() <= h.size() + max_dist + 1; ++i) {
        for (int len = std::max(1, nl - max_dist); len <= nl + max_dist; ++len) {
            if (i + len > h.size()) continue;
            if (levenshtein(h.substr(i, len), n) <= max_dist) return true;
        }
    }
    return false;
}

bool containsAll(const std::string& haystack, const std::vector<std::string>& needles) {
    // Use fuzzy contains; tolera 2 errores por palabra de >= 5 chars,
    // 1 error para 3-4 chars, exacto para palabras muy cortas.
    for (const auto& n : needles) {
        int max_d = (n.size() >= 5) ? 2 : (n.size() >= 3 ? 1 : 0);
        if (!fuzzyContains(haystack, n, max_d)) return false;
    }
    return true;
}

// Agrupar OCRResults en lineas por proximidad vertical de bbox.
// Devuelve grupos ordenados por Y, cada uno con sus bloques ordenados por X.
struct Line {
    int y_top, y_bot;
    std::vector<OCRResult> blocks;
    std::string text() const {
        std::string s;
        for (const auto& b : blocks) {
            if (!s.empty()) s += " ";
            s += b.text;
        }
        return s;
    }
    int yCenter() const { return (y_top + y_bot) / 2; }
};

std::vector<Line> groupLines(const std::vector<OCRResult>& ocr) {
    auto sorted = ocr;
    std::sort(sorted.begin(), sorted.end(),
              [](const OCRResult& a, const OCRResult& b) {
                  return a.bbox.y < b.bbox.y;
              });
    std::vector<Line> lines;
    for (const auto& r : sorted) {
        if (r.text.empty()) continue;
        int yc = r.bbox.y + r.bbox.height / 2;
        bool placed = false;
        // Tolerancia: 60% de la altura del bloque, minimo 14 px. Esto
        // une bloques de la misma fila incluso cuando OCR los reporta
        // con Y ligeramente distintas (caracteres altos como '|', tildes).
        int tol = std::max(14, static_cast<int>(r.bbox.height * 0.6));
        for (auto& L : lines) {
            int Lc = L.yCenter();
            if (std::abs(yc - Lc) <= tol) {
                L.blocks.push_back(r);
                L.y_top = std::min(L.y_top, r.bbox.y);
                L.y_bot = std::max(L.y_bot, r.bbox.y + r.bbox.height);
                placed = true;
                break;
            }
        }
        if (!placed) {
            Line L;
            L.y_top  = r.bbox.y;
            L.y_bot  = r.bbox.y + r.bbox.height;
            L.blocks.push_back(r);
            lines.push_back(L);
        }
    }
    // Ordenar bloques de cada linea por X
    for (auto& L : lines) {
        std::sort(L.blocks.begin(), L.blocks.end(),
                  [](const OCRResult& a, const OCRResult& b) {
                      return a.bbox.x < b.bbox.x;
                  });
    }
    // Ordenar lineas por Y
    std::sort(lines.begin(), lines.end(),
              [](const Line& a, const Line& b) { return a.yCenter() < b.yCenter(); });
    return lines;
}

// Indices de lineas que contienen TODAS las palabras claves dadas.
std::vector<int> findLines(const std::vector<Line>& lines,
                            const std::vector<std::string>& needles) {
    std::vector<int> out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (containsAll(lines[i].text(), needles)) out.push_back(static_cast<int>(i));
    }
    return out;
}
int findFirstLine(const std::vector<Line>& lines,
                   const std::vector<std::string>& needles) {
    auto v = findLines(lines, needles);
    return v.empty() ? -1 : v.front();
}

// Asignar un bloque a la columna por X. Las column_x_starts son los X
// iniciales de cada columna (orden ascendente). col 0 = primera columna.
int colForX(int x, const std::vector<int>& col_starts) {
    int col = 0;
    for (size_t i = 0; i < col_starts.size(); ++i) {
        if (x >= col_starts[i]) col = static_cast<int>(i);
        else break;
    }
    return col;
}

// Concatena los bloques de una linea que caen en una columna especifica.
std::string columnText(const Line& row, const std::vector<int>& col_starts, int col) {
    std::string s;
    for (const auto& b : row.blocks) {
        if (colForX(b.bbox.x, col_starts) == col) {
            if (!s.empty()) s += " ";
            s += b.text;
        }
    }
    return s;
}

// Lista de labels (puede ser multi-palabra). Se usan tanto para localizar
// el inicio de un valor como para detectar el FIN del valor anterior.
static const std::vector<std::string> KNOWN_LABELS = {
    "Cliente:", "NIT/CC:", "NIT:", "CC:", "Direccion:",
    "Ciudad:", "Asesor:", "Periodo:",
    "Codigo:",
    "Saldo Anterior:", "Adiciones:", "Retiros:",
    "Rendimientos:", "Nuevo Saldo:",
    "Fondo de Inversion:"
};

// Tokeniza un string por espacios.
std::vector<std::string> splitTokens(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string t;
    while (iss >> t) out.push_back(t);
    return out;
}

// Normaliza: mayusculas + sin espacios. Asi "Saldo Anterior:" se vuelve
// "SALDOANTERIOR:" igual que si los tokens estuvieran concatenados.
std::string normalizeLabel(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// True si el bloque parece ser el comienzo (o el todo) de algun KNOWN_LABEL.
// Sirve para "frenar" textAfterLabel cuando topa con el inicio de otro label.
// Acepta:
//   - Coincidencia exacta del primer token (ej. "Saldo")
//   - El bloque completo (normalizado) es el label completo (ej.
//     "Saldo Anterior:" o "SaldoAnterior:" → "SALDOANTERIOR:")
bool blockStartsKnownLabel(const std::string& blocktext) {
    std::string up   = toUpperAscii(blocktext);
    std::string norm = normalizeLabel(blocktext);
    if (norm.empty()) return false;
    for (const auto& L : KNOWN_LABELS) {
        auto toks = splitTokens(L);
        // Fuzzy match para tolerar OCR garbled (ej. "NITICC:" en vez de "NIT/CC:")
        if (!toks.empty()) {
            std::string first_up = toUpperAscii(toks.front());
            int td = static_cast<int>(first_up.size()) >= 5 ? 1 : 0;
            if (levenshtein(up, first_up) <= td) return true;
        }
        std::string label_norm = normalizeLabel(L);
        int max_d = std::min(2, static_cast<int>(label_norm.size()) / 4);
        if (std::abs(static_cast<int>(norm.size()) - static_cast<int>(label_norm.size())) <= 2 &&
            levenshtein(norm, label_norm) <= max_d) return true;
    }
    return false;
}

// Busca una secuencia de bloques que matchee los tokens de label (case-
// insensitive). Devuelve el indice del ULTIMO bloque del label, o -1.
//
// Robusto a fragmentacion OCR: tambien acepta que el label venga
// CONCATENADO en un solo bloque (ej. "SaldoAnterior:") o con tokens
// pegados sin espacio. La normalizacion (sin espacios) hace que
// "Saldo Anterior:" como un solo bloque tambien matchee.
int findLabelEnd(const Line& line, const std::string& label) {
    auto toks = splitTokens(label);
    if (toks.empty()) return -1;
    std::string label_norm = normalizeLabel(label);
    // Tolerancia fuzzy moderada: 1 char por cada 4 chars del label,
    // con tope de 2. Evita falsos matches en labels cortos como "NIT:".
    int max_dist = std::min(2, static_cast<int>(label_norm.size()) / 4);

    // Caso 1: label en un solo bloque, con tolerancia Levenshtein.
    // Match EXACTO primero — si lo hay, lo usamos sin buscar fuzzy.
    for (size_t i = 0; i < line.blocks.size(); ++i) {
        if (normalizeLabel(line.blocks[i].text) == label_norm)
            return static_cast<int>(i);
    }
    // Si no hay exacto, buscamos el mejor fuzzy match.
    int best_idx = -1;
    int best_d   = max_dist + 1;
    for (size_t i = 0; i < line.blocks.size(); ++i) {
        std::string blk = normalizeLabel(line.blocks[i].text);
        if (blk.empty()) continue;
        if (std::abs(static_cast<int>(blk.size()) - static_cast<int>(label_norm.size())) > 2) continue;
        int d = levenshtein(blk, label_norm);
        if (d <= max_dist && d < best_d) {
            best_d = d;
            best_idx = static_cast<int>(i);
        }
    }
    if (best_idx >= 0) return best_idx;

    // Caso 2: secuencia exacta de N bloques con tolerancia por token
    for (size_t i = 0; i + toks.size() <= line.blocks.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < toks.size(); ++j) {
            std::string blk = normalizeLabel(line.blocks[i + j].text);
            std::string tk  = normalizeLabel(toks[j]);
            int td = static_cast<int>(tk.size()) >= 5 ? 2 : 1;
            if (levenshtein(blk, tk) > td) { ok = false; break; }
        }
        if (ok) return static_cast<int>(i + toks.size() - 1);
    }
    // Caso 3: bloques consecutivos cuya concatenacion normalizada
    // matchea label_norm fuzzy.
    for (size_t i = 0; i < line.blocks.size(); ++i) {
        std::string acc;
        for (size_t j = i; j < line.blocks.size() && acc.size() <= label_norm.size() + max_dist; ++j) {
            acc += normalizeLabel(line.blocks[j].text);
            if (levenshtein(acc, label_norm) <= max_dist) return static_cast<int>(j);
            if (acc.size() > label_norm.size() + max_dist) break;
        }
    }
    return -1;
}

// Texto que viene DESPUES del label, parando al toparse con otro label
// conocido (incluso si es multi-palabra: paramos al ver el primer token).
std::string textAfterLabel(const Line& line, const std::string& label) {
    int end = findLabelEnd(line, label);
    if (end < 0) return "";
    std::string s;
    for (size_t i = end + 1; i < line.blocks.size(); ++i) {
        if (blockStartsKnownLabel(line.blocks[i].text)) break;
        if (!s.empty()) s += " ";
        s += line.blocks[i].text;
    }
    return s;
}

// Convierte fecha "DD-MM-YYYY" -> "YYYY-MM-DD" (ISO). Acepta como
// separadores '-', '/', '.', ' ' (Tesseract a veces lee guiones como
// espacios o puntos). Si no matchea, devuelve original.
std::string toISODate(const std::string& s) {
    static const std::regex re_dmY(R"((\d{1,2})[-./ ](\d{1,2})[-./ ](\d{4}))");
    std::smatch m;
    if (std::regex_search(s, m, re_dmY)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      std::stoi(m[3]), std::stoi(m[2]), std::stoi(m[1]));
        return buf;
    }
    return s;
}

// Parser numerico colombiano. IMPORTANTE: extrae SOLO el primer monto
// del texto antes de delegar al parser legacy. Esto evita que dos numeros
// consecutivos (ej. "$1.751.945,99 $602.561.945,99") se concatenen en un
// solo numero gigante (resultaria 1.75e18). El parser legacy hace
// "strip de puntos" y luego stod, que es vulnerable a esa concatenacion.
double parseCOPNumber(const std::string& text) {
    // Normalizar: Tesseract a veces lee "." como " " dentro de un numero
    // grande (ej. "32.067 000,00" o "1.846.624 461,02"). Convertimos
    // espacios entre digitos en puntos antes de intentar el regex.
    std::string norm = text;
    static const std::regex re_space_between_digits(R"((\d) +(\d))");
    // Aplicar varias veces para colapsar grupos como "X X X X" -> "X.X.X.X"
    for (int pass = 0; pass < 4; ++pass) {
        std::string after = std::regex_replace(norm, re_space_between_digits, "$1.$2");
        if (after == norm) break;
        norm = after;
    }
    static const std::regex re_first(R"((-?[\d\.]+,\d{1,2}))");
    std::smatch m;
    if (std::regex_search(norm, m, re_first)) {
        return DataStructurer::parseColombianNumber(m.str());
    }
    // Fallback: numero entero sin decimales ("$1.000.000")
    static const std::regex re_int(R"((-?\d{1,3}(?:\.\d{3})+))");
    if (std::regex_search(norm, m, re_int)) {
        return DataStructurer::parseColombianNumber(m.str());
    }
    return 0.0;
}
// Mismo principio para porcentajes: tomar el primero.
double parseCOPPercent(const std::string& text) {
    static const std::regex re_first(R"((-?[\d\.]+,\d{1,2}\s*%?))");
    std::smatch m;
    if (std::regex_search(text, m, re_first)) {
        return DataStructurer::parseColombianPercentage(m.str());
    }
    return 0.0;
}

// Devuelve el "money-like" string en la linea, requiriendo signo $ para
// distinguir de porcentajes. Toma el primero (mas a la izquierda).
std::string lastMoneyInLine(const Line& line) {
    static const std::regex re(R"(\$\s*[\d\.]+,\d{2})");
    for (const auto& b : line.blocks) {
        std::smatch m;
        std::string t = b.text;
        if (std::regex_search(t, m, re)) return m.str();
    }
    // Concatenar bloques consecutivos por si "$" y "1.234" salieron separados
    std::string full = line.text();
    std::smatch m;
    if (std::regex_search(full, m, re)) return m.str();
    return "";
}

std::string lastPercentInLine(const Line& line) {
    static const std::regex re(R"([\d\.]+,\d{2}\s*%?)");
    std::string out;
    for (auto it = line.blocks.rbegin(); it != line.blocks.rend(); ++it) {
        const std::string& t = it->text;
        if (t.find('%') != std::string::npos) { out = t; break; }
    }
    if (out.empty()) {
        std::string full = line.text();
        std::smatch m;
        if (std::regex_search(full, m, re)) out = m.str();
    }
    return out;
}

} // namespace

// =============================================================================
// Deteccion del template
//
// El titulo "EXTRACTO DE INVERSIONES" esta en fuente grande y Tesseract a
// veces no lo recoge bajo PSM=6. Usamos en su lugar un score multi-senial
// basado en marcadores muy especificos de nuestro template estandar v1:
//   - "Cliente:"    (label exacto)
//   - "NIT/CC:"     (label exacto, no aparece en A&V)
//   - "Periodo:"    (label exacto)
//   - "RESUMEN"+"PORTAFOLIO"  (header de la tabla resumen)
//   - "RENTA"+"FIJA"+"DETALLE" (header de tabla RF)
// Si vemos al menos 3 de estas seniales, es el template estandar.
// =============================================================================
bool StandardParser::detectStandardTemplate(const std::vector<OCRResult>& ocr_data) {
    auto lines = groupLines(ocr_data);
    int score = 0;
    // Marcadores de bloque (con tolerancia Levenshtein para que tolere
    // OCR garbled de imagenes de baja resolucion).
    auto hasBlockFuzzy = [&](const std::string& token, int max_dist) {
        std::string tn = normalizeLabel(token);
        for (const auto& L : lines)
            for (const auto& b : L.blocks) {
                std::string bn = normalizeLabel(b.text);
                if (bn.empty()) continue;
                if (std::abs(static_cast<int>(bn.size()) - static_cast<int>(tn.size())) > max_dist + 2) continue;
                if (levenshtein(bn, tn) <= max_dist) return true;
            }
        return false;
    };
    if (hasBlockFuzzy("Cliente:",   2)) score++;
    if (hasBlockFuzzy("NIT/CC:",    2)) score++;
    if (hasBlockFuzzy("Periodo:",   2)) score++;
    if (hasBlockFuzzy("Direccion:", 2)) score++;
    if (hasBlockFuzzy("Ciudad:",    2)) score++;
    if (hasBlockFuzzy("Asesor:",    2)) score++;
    // Marcadores de seccion (multi-token, busqueda en texto de la linea)
    if (findFirstLine(lines, {"RESUMEN", "PORTAFOLIO"}) >= 0) score++;
    if (findFirstLine(lines, {"RENTA", "FIJA"}) >= 0)         score++;
    if (findFirstLine(lines, {"EXTRACTO", "INVERSIONES"}) >= 0) score++;
    return score >= 3;
}

// =============================================================================
// Parser principal
// =============================================================================
bool StandardParser::parseAll(const std::vector<OCRResult>& ocr_data,
                                ExtractoCompleto& out) {
    auto lines = groupLines(ocr_data);
    std::cout << "[StandardParser] Parseando template estandar v1, "
              << lines.size() << " lineas en el OCR.\n";

    // ---- Encontrar indices de seccion para delimitar ----
    // Helper: buscar header puro (linea con las palabras clave SIN dinero
    // ni porcentaje). Distingue el header de seccion de la fila del resumen.
    auto findSectionHeader = [&](const std::vector<std::string>& needles) -> int {
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string txt = lines[i].text();
            if (!containsAll(txt, needles)) continue;
            if (txt.find('$') != std::string::npos) continue;
            if (txt.find('%') != std::string::npos) continue;
            return static_cast<int>(i);
        }
        return -1;
    };

    int idx_resumen   = findSectionHeader({"RESUMEN", "PORTAFOLIO"});
    int idx_rf        = findSectionHeader({"RENTA", "FIJA", "DETALLE"});
    int idx_saldos    = findSectionHeader({"SALDOS", "EFECTIVO"});
    auto idx_fondos_v = findLines(lines, {"FONDO", "INVERSION"});  // estos sí tienen :

    auto next_section_after = [&](int idx) -> int {
        int best = static_cast<int>(lines.size());
        for (int c : {idx_resumen, idx_rf, idx_saldos}) {
            if (c > idx && c < best) best = c;
        }
        for (int c : idx_fondos_v) {
            if (c > idx && c < best) best = c;
        }
        return best;
    };

    // =========================================================================
    // 1. HEADER + CLIENTE
    // =========================================================================
    // Periodo
    int idx_periodo = findFirstLine(lines, {"PERIODO"});
    if (idx_periodo >= 0) {
        std::string txt = lines[idx_periodo].text();
        static const std::regex re(R"((\d{2}-\d{2}-\d{4})\s*al\s*(\d{2}-\d{2}-\d{4}))",
                                     std::regex::icase);
        std::smatch m;
        if (std::regex_search(txt, m, re)) {
            out.resumen.fecha_extracto = toISODate(m[2]);  // fecha fin
        }
    }

    // Cliente block: cliente, NIT, direccion, ciudad, asesor. Buscamos en
    // todas las lineas el bloque que sea exactamente el label, y tomamos
    // el texto que sigue hasta el siguiente label conocido (textAfterLabel
    // ya hace ese corte por bloques).
    auto fillFromLine = [&](const std::string& label, std::string& target) {
        for (const auto& L : lines) {
            std::string val = textAfterLabel(L, label);
            if (!val.empty()) { target = val; return; }
        }
    };

    fillFromLine("Cliente:",   out.resumen.nombre_cliente);
    fillFromLine("NIT/CC:",    out.resumen.nit);
    if (out.resumen.nit.empty()) fillFromLine("NIT:", out.resumen.nit);
    fillFromLine("Direccion:", out.resumen.direccion);
    fillFromLine("Ciudad:",    out.resumen.ciudad);
    fillFromLine("Asesor:",    out.resumen.asesor);

    // =========================================================================
    // 2. RESUMEN DEL PORTAFOLIO
    // =========================================================================
    if (idx_resumen >= 0) {
        int end = next_section_after(idx_resumen);
        for (int i = idx_resumen + 1; i < end; ++i) {
            const std::string txt = lines[i].text();
            std::string up = toUpperAscii(txt);
            std::string money = lastMoneyInLine(lines[i]);
            double v = parseCOPNumber(money);
            if (up.find("RENTA FIJA") != std::string::npos) {
                out.resumen.activos["Renta Fija"] = v;
            } else if (up.find("FONDOS") != std::string::npos ||
                       up.find("FIC") != std::string::npos) {
                out.resumen.activos["FIC"] = v;
            } else if (up.find("EFECTIVO") != std::string::npos) {
                out.resumen.activos["Saldos en Efectivo"] = v;
            } else if (up.find("TOTAL") != std::string::npos) {
                out.resumen.total_portafolio = v;
                out.resumen.total_activos    = v;
            }
        }
    }

    // =========================================================================
    // 3. RENTA FIJA
    // =========================================================================
    if (idx_rf >= 0) {
        int end = next_section_after(idx_rf);
        // Buscar la linea de header. Tesseract a veces lee mal palabras
        // sobre fondo oscuro (Nemotecnico, F.Emi). Detectamos el header
        // si algun bloque matchea cualquiera de las columnas conocidas
        // (las "robustas" — fondo blanco o suficientemente legibles).
        static const std::vector<std::string> HEADER_TOKENS = {
            "NEMOTECNICO", "F.EMI", "F.VTO", "F.CMP",
            "T.FACIAL", "PERIODICIDAD",
            "T.NEGOCIACION", "T.VALORACION",
            "VALOR NOMINAL", "VALOR MERCADO"
        };
        int idx_header = -1;
        for (int i = idx_rf + 1; i < end; ++i) {
            std::string line_up = toUpperAscii(lines[i].text());
            int hits = 0;
            for (const auto& t : HEADER_TOKENS)
                if (line_up.find(t) != std::string::npos) hits++;
            if (hits >= 3) { idx_header = i; break; }   // 3+ columnas reconocidas
        }
        // Si Tesseract no leyo el header (texto blanco sobre fondo azul
        // a veces se pierde), trabajamos directo desde el titulo de la
        // seccion. El parser semantico no necesita el header — identifica
        // las celdas por tipo de contenido (fecha/$/% / texto).
        if (idx_header < 0) {
            idx_header = idx_rf;
            std::cout << "[StandardParser] RF: header no detectado (OCR), "
                         "parseando desde el titulo de seccion.\n";
        }
        if (idx_header >= 0) {
            // Parser semantico: para cada fila de datos, clasifica cada
            // bloque por su tipo (date/percent/money/short-text/text) y
            // los asigna al campo del schema. Robusto a:
            //   - Headers garbled (no los miramos)
            //   - Nemo multi-palabra (concatenamos texto inicial)
            //   - Bloques fragmentados (los grupos por tipo los unen)
            // El template tiene 10 columnas semanticamente fijas:
            //   nemo | F.Emi | F.Vto | F.Cmp | T.Facial | Period |
            //   Vlr.Nominal | T.Negoc | T.Valor | Vlr.Mercado
            // Date: acepta DD-MM-YYYY, DD/MM/YYYY, DD.MM.YYYY o DD MM YYYY
            // (OCR a veces lee guiones como espacios o puntos).
            static const std::regex re_date(R"(\d{1,2}[-./ ]\d{1,2}[-./ ]\d{4})");
            static const std::regex re_percent(R"([\d\.]+,\d{1,2}\s*%)");
            static const std::regex re_money(R"(\$\s*[\d\.]+,\d{2})");

            // Helpers para extraer todas las ocurrencias de un patron en
            // un texto. Las regex se aplican al TEXTO COMPLETO de la fila
            // (no a cada bloque), lo que las hace robustas a fragmentacion
            // OCR (e.g. "$" "1.234.567,89" como 2 bloques).
            auto findAll = [](const std::string& text, const std::regex& re) {
                std::vector<std::string> out;
                auto it  = std::sregex_iterator(text.begin(), text.end(), re);
                auto end = std::sregex_iterator();
                for (; it != end; ++it) out.push_back(it->str());
                return out;
            };
            // Tambien admitimos numeros sin $ (en filas donde Tesseract
            // perdio el simbolo) si vienen con "M" o muchos digitos.
            static const std::regex re_money_loose(R"([\d\.]{6,},\d{2})");

            int rows_parsed = 0;
            for (int i = idx_header + 1; i < end; ++i) {
                const Line& row = lines[i];
                if (row.blocks.empty()) continue;
                std::string text_full = row.text();

                // Pre-procesar: Tesseract fragmenta numeros con espacios
                // (ej. "1.355 271 000,00" en vez de "1.355.271.000,00").
                // Colapsamos espacios entre digitos como puntos para que
                // los regex agarren los valores completos.
                std::string text_norm = text_full;
                static const std::regex re_space_between_digits(R"((\d) +(\d))");
                for (int pass = 0; pass < 4; ++pass) {
                    std::string after = std::regex_replace(text_norm,
                                                            re_space_between_digits, "$1.$2");
                    if (after == text_norm) break;
                    text_norm = after;
                }

                // Extraer todas las fechas, porcentajes, dineros del texto normalizado.
                // Tambien las fechas matchean el formato con espacios convertidos a puntos.
                auto dates    = findAll(text_norm, re_date);
                auto percents = findAll(text_norm, re_percent);
                auto monies   = findAll(text_norm, re_money);
                if (monies.size() < 2) {
                    // Buscar tambien numeros sin $ (a veces el OCR perdia el simbolo)
                    auto loose = findAll(text_full, re_money_loose);
                    for (const auto& s : loose) monies.push_back("$" + s);
                }

                // Nemotecnico: bloques iniciales que NO sean fechas/dineros/
                // porcentajes (los datos numericos de la tabla). Iteramos
                // bloque por bloque y paramos al toparnos con uno que sea
                // o forme parte de un campo numerico.
                std::string nemo;
                std::string period;
                static const std::regex re_block_is_date(R"(^\s*\d{1,2}([-./ ]\d{1,2}([-./ ]\d{4})?)?\s*$)");
                static const std::regex re_block_is_pct(R"([\d\.]+,\d+\s*%?$)");
                static const std::regex re_block_has_money(R"(\$|[\d\.]+,\d{2})");
                for (const auto& b : row.blocks) {
                    std::string t = b.text;
                    // Trim
                    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
                    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
                    if (t.empty()) continue;
                    if (std::regex_search(t, re_block_is_date)) break;
                    if (std::regex_search(t, re_block_is_pct))  break;
                    if (std::regex_search(t, re_block_has_money)) break;
                    if (!nemo.empty()) nemo += " ";
                    nemo += t;
                }

                // Periodicidad: bloque corto (1-3 chars) entre las fechas
                // y los porcentajes. Ej. "TV", "MV", "AV", "CV".
                for (const auto& b : row.blocks) {
                    std::string up = toUpperAscii(b.text);
                    if (b.text.size() <= 3 &&
                        (up == "TV" || up == "MV" || up == "AV" ||
                         up == "CV" || up == "SV")) {
                        period = b.text;
                        break;
                    }
                }

                // Validar: necesitamos al menos 2 dates, 2 percents, 2 money
                // y un nemo no vacio para tener fila plausible.
                if (nemo.empty()) continue;
                if (dates.size() < 2) continue;
                if (monies.size() < 1) continue;
                if (percents.size() < 1) continue;

                InstrumentoRentaFija inst;
                inst.nemotecnico       = nemo;
                inst.fecha_emision     = dates.size() > 0 ? toISODate(dates[0]) : "";
                inst.fecha_vencimiento = dates.size() > 1 ? toISODate(dates[1]) : "";
                inst.fecha_compra      = dates.size() > 2 ? toISODate(dates[2]) : "";
                inst.tasa_facial       = percents.size() > 0 ? parseCOPPercent(percents[0]) : 0;
                inst.periodicidad      = period;
                inst.valor_nominal     = monies.size() > 0 ? parseCOPNumber(monies[0]) : 0;
                inst.tasa_negociacion  = percents.size() > 1 ? parseCOPPercent(percents[1]) : 0;
                inst.tasa_valoracion   = percents.size() > 2 ? parseCOPPercent(percents[2]) : 0;
                inst.valor_mercado     = monies.size() > 1 ? parseCOPNumber(monies[1]) :
                                          (monies.size() > 0 ? parseCOPNumber(monies[0]) : 0);

                if (inst.valor_mercado > 0) {
                    out.renta_fija.push_back(inst);
                    rows_parsed++;
                }
            }
            std::cout << "[StandardParser] Tabla RF: " << rows_parsed
                      << " filas parseadas (modo semantico).\n";
        }
    }

    // =========================================================================
    // 4. FONDOS
    // =========================================================================
    for (int idx : idx_fondos_v) {
        int end = next_section_after(idx);
        FondoInversion fondo;
        // Nombre del fondo: tras "Fondo de Inversion:" (multi-palabra)
        std::string after = textAfterLabel(lines[idx], "Fondo de Inversion:");
        if (after.empty()) {
            // Fallback: tomar texto despues del bloque que contiene "INVERSION"
            for (size_t bi = 0; bi < lines[idx].blocks.size(); ++bi) {
                std::string up = toUpperAscii(lines[idx].blocks[bi].text);
                if (up.find("INVERSION") != std::string::npos) {
                    for (size_t bj = bi + 1; bj < lines[idx].blocks.size(); ++bj) {
                        if (!after.empty()) after += " ";
                        after += lines[idx].blocks[bj].text;
                    }
                    break;
                }
            }
        }
        fondo.nombre_fondo = after;

        // Recorrer lineas de la seccion buscando labels conocidos
        for (int i = idx + 1; i < end; ++i) {
            const Line& L = lines[i];
            auto fill = [&](const std::string& label, double& target) {
                std::string v = textAfterLabel(L, label);
                if (!v.empty()) target = parseCOPNumber(v);
            };
            std::string codigo_v = textAfterLabel(L, "Codigo:");
            if (!codigo_v.empty()) {
                // Codigo es la primera palabra
                std::istringstream iss(codigo_v);
                iss >> fondo.codigo;
            }
            fill("Saldo Anterior:", fondo.saldo_anterior);
            fill("Adiciones:",      fondo.adiciones);
            fill("Retiros:",        fondo.retiros);
            fill("Rendimientos:",   fondo.rendimientos);
            fill("Nuevo Saldo:",    fondo.nuevo_saldo);

            // Rentabilidades historicas: linea con "Mensual", "Trimestral",
            // "Semestral", etc. y un porcentaje.
            std::string txt_up = toUpperAscii(L.text());
            std::string pct = lastPercentInLine(L);
            double pv = parseCOPPercent(pct);
            auto match_rent = [&](const std::string& key, const std::string& norm) {
                if (txt_up.find(toUpperAscii(key)) != std::string::npos
                    && !pct.empty() && pv != 0.0) {
                    fondo.rentabilidades_historicas[norm] = pv;
                }
            };
            // Solo si la linea no es un label de los anteriores
            bool is_summary_label =
                txt_up.find("SALDO ANTERIOR") != std::string::npos ||
                txt_up.find("ADICIONES")      != std::string::npos ||
                txt_up.find("RETIROS")        != std::string::npos ||
                txt_up.find("RENDIMIENTOS")   != std::string::npos ||
                txt_up.find("NUEVO SALDO")    != std::string::npos ||
                txt_up.find("CODIGO")         != std::string::npos;
            if (!is_summary_label) {
                match_rent("Mensual",     "Mensual");
                match_rent("Trimestral",  "Trimestral");
                match_rent("Semestral",   "Semestral");
                match_rent("Año corrido", "Año corrido");
                match_rent("Ano corrido", "Año corrido");
                match_rent("Ultimo año",  "Ultimo año");
                match_rent("Ultimo ano",  "Ultimo año");
                match_rent("Último año",  "Ultimo año");
                match_rent("Ultimos 2",   "Ultimos 2 años");
                match_rent("Últimos 2",   "Ultimos 2 años");
            }
        }

        if (!fondo.nombre_fondo.empty() || fondo.nuevo_saldo > 0) {
            out.fondos.push_back(fondo);
        }
    }

    // =========================================================================
    // 5. SALDOS EN EFECTIVO
    // =========================================================================
    if (idx_saldos >= 0) {
        int end = next_section_after(idx_saldos);
        // Buscar header con al menos "Cuenta" o "Saldo" (Banco a veces
        // sale ilegible en fotos: lo deja como "AA" o vacio, no exigible).
        int idx_header = -1;
        for (int i = idx_saldos + 1; i < end; ++i) {
            std::string up = toUpperAscii(lines[i].text());
            if (up.find("CUENTA") != std::string::npos ||
                (up.find("SALDO") != std::string::npos &&
                 up.find("BANCO") != std::string::npos)) {
                idx_header = i; break;
            }
        }
        if (idx_header >= 0) {
            for (int i = idx_header + 1; i < end; ++i) {
                const Line& row = lines[i];
                if (row.blocks.size() < 2) continue;
                SaldoEfectivo s;
                // Cuenta = primer bloque (numero); Banco = bloques de en medio;
                // Saldo = ultimo bloque con $.
                s.cuenta          = row.blocks.front().text;
                s.saldo_disponible = parseCOPNumber(lastMoneyInLine(row));
                s.saldo_total      = s.saldo_disponible;
                if (s.saldo_total > 0) out.saldos_efectivo.push_back(s);
            }
        }
    }

    std::cout << "[StandardParser] Resultado: cliente='"
              << out.resumen.nombre_cliente << "' total=$"
              << out.resumen.total_portafolio
              << " RF=" << out.renta_fija.size()
              << " fondos=" << out.fondos.size()
              << " saldos=" << out.saldos_efectivo.size() << "\n";
    return true;
}
