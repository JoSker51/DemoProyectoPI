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

bool containsAll(const std::string& haystack, const std::vector<std::string>& needles) {
    std::string up = toUpperAscii(haystack);
    for (const auto& n : needles) {
        if (up.find(toUpperAscii(n)) == std::string::npos) return false;
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
        for (auto& L : lines) {
            int Lc = L.yCenter();
            if (std::abs(yc - Lc) <= std::max(8, r.bbox.height / 2)) {
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

// True si el texto del bloque coincide exactamente con el primer token
// (en mayusculas) de algun KNOWN_LABEL. Sirve para "frenar" el textAfterLabel
// cuando topa con el inicio de otro label.
bool blockStartsKnownLabel(const std::string& blocktext) {
    std::string up = toUpperAscii(blocktext);
    for (const auto& L : KNOWN_LABELS) {
        auto toks = splitTokens(L);
        if (!toks.empty() && toUpperAscii(toks.front()) == up) return true;
    }
    return false;
}

// Busca una secuencia de bloques que matchee los tokens de label (case-
// insensitive). Devuelve el indice del ULTIMO bloque del label, o -1.
int findLabelEnd(const Line& line, const std::string& label) {
    auto toks = splitTokens(label);
    if (toks.empty()) return -1;
    for (size_t i = 0; i + toks.size() <= line.blocks.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < toks.size(); ++j) {
            if (toUpperAscii(line.blocks[i + j].text) != toUpperAscii(toks[j])) {
                ok = false; break;
            }
        }
        if (ok) return static_cast<int>(i + toks.size() - 1);
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

// Convierte fecha "DD-MM-YYYY" -> "YYYY-MM-DD" (ISO). Si no matchea, devuelve original.
std::string toISODate(const std::string& s) {
    static const std::regex re_dmY(R"((\d{1,2})-(\d{1,2})-(\d{4}))");
    std::smatch m;
    if (std::regex_search(s, m, re_dmY)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      std::stoi(m[3]), std::stoi(m[2]), std::stoi(m[1]));
        return buf;
    }
    return s;
}

// Parser numerico colombiano: "1.234.567,89" o "$ 1.234.567,89" -> 1234567.89
double parseCOPNumber(const std::string& text) {
    return DataStructurer::parseColombianNumber(text);
}
double parseCOPPercent(const std::string& text) {
    return DataStructurer::parseColombianPercentage(text);
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
    // Marcadores de bloque exacto
    auto hasBlockExact = [&](const std::string& token) {
        for (const auto& L : lines)
            for (const auto& b : L.blocks)
                if (toUpperAscii(b.text) == toUpperAscii(token)) return true;
        return false;
    };
    if (hasBlockExact("Cliente:"))   score++;
    if (hasBlockExact("NIT/CC:"))    score++;
    if (hasBlockExact("Periodo:"))   score++;
    if (hasBlockExact("Direccion:")) score++;
    if (hasBlockExact("Ciudad:"))    score++;
    if (hasBlockExact("Asesor:"))    score++;
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
            static const std::regex re_date(R"(\d{1,2}-\d{1,2}-\d{4})");
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

                // Extraer todas las fechas, porcentajes, dineros del texto completo
                auto dates    = findAll(text_full, re_date);
                auto percents = findAll(text_full, re_percent);
                auto monies   = findAll(text_full, re_money);
                if (monies.size() < 2) {
                    // Buscar tambien numeros sin $ (a veces el OCR perdia el simbolo)
                    auto loose = findAll(text_full, re_money_loose);
                    for (const auto& s : loose) monies.push_back("$" + s);
                }

                // Nemotecnico: todo el texto antes de la primera fecha
                std::string nemo;
                std::string period;
                if (!dates.empty()) {
                    auto pos = text_full.find(dates[0]);
                    if (pos != std::string::npos) {
                        nemo = text_full.substr(0, pos);
                    }
                }
                // Trim y limpieza
                while (!nemo.empty() && std::isspace(static_cast<unsigned char>(nemo.back()))) nemo.pop_back();

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
        // Buscar header con "Cuenta", "Banco", "Saldo"
        int idx_header = -1;
        for (int i = idx_saldos + 1; i < end; ++i) {
            std::string up = toUpperAscii(lines[i].text());
            if (up.find("CUENTA") != std::string::npos &&
                up.find("BANCO")  != std::string::npos) { idx_header = i; break; }
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
