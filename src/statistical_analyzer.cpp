#include "statistical_analyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

double StatisticalAnalyzer::mean(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / static_cast<double>(data.size());
}

double StatisticalAnalyzer::stdDev(const std::vector<double>& data) {
    if (data.size() < 2) return 0.0;
    double m = mean(data);
    double sum_sq = 0.0;
    for (double v : data) {
        sum_sq += (v - m) * (v - m);
    }
    return std::sqrt(sum_sq / static_cast<double>(data.size() - 1));
}

double StatisticalAnalyzer::median(std::vector<double> data) {
    if (data.empty()) return 0.0;
    std::sort(data.begin(), data.end());
    size_t n = data.size();
    if (n % 2 == 0) {
        return (data[n / 2 - 1] + data[n / 2]) / 2.0;
    }
    return data[n / 2];
}

double StatisticalAnalyzer::minVal(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return *std::min_element(data.begin(), data.end());
}

double StatisticalAnalyzer::maxVal(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return *std::max_element(data.begin(), data.end());
}

StatSummary StatisticalAnalyzer::summarize(const std::vector<double>& data) {
    StatSummary s;
    s.count = static_cast<int>(data.size());
    if (data.empty()) return s;
    s.mean = mean(data);
    s.std_dev = stdDev(data);
    s.median = median(data);
    s.min_val = minVal(data);
    s.max_val = maxVal(data);
    return s;
}

std::vector<int> StatisticalAnalyzer::findOutliers(const std::vector<double>& data, double k) {
    std::vector<int> outliers;
    if (data.size() < 3) return outliers;

    double m = mean(data);
    double sd = stdDev(data);
    if (sd < 1e-9) return outliers;

    for (size_t i = 0; i < data.size(); ++i) {
        if (std::abs(data[i] - m) > k * sd) {
            outliers.push_back(static_cast<int>(i));
        }
    }
    return outliers;
}

AnalysisResult StatisticalAnalyzer::analyze(const ExtractoCompleto& extracto) {
    std::cout << "[StatisticalAnalyzer] Analizando extracto..." << std::endl;
    AnalysisResult result;

    // ---- Composicion porcentual del portafolio ----
    double total = extracto.resumen.total_portafolio;
    if (total > 0) {
        for (const auto& [key, val] : extracto.resumen.activos) {
            result.composicion_porcentaje[key] = (val / total) * 100.0;
        }
    }

    // ---- Estadisticas de Renta Fija ----
    std::vector<double> tasas_val, tasas_neg, tasas_fac, valores_merc;
    for (const auto& inst : extracto.renta_fija) {
        if (inst.tasa_valoracion > 0) tasas_val.push_back(inst.tasa_valoracion);
        if (inst.tasa_negociacion > 0) tasas_neg.push_back(inst.tasa_negociacion);
        if (inst.tasa_facial > 0) tasas_fac.push_back(inst.tasa_facial);
        if (inst.valor_mercado > 0) valores_merc.push_back(inst.valor_mercado);
    }

    result.tasas_valoracion = summarize(tasas_val);
    result.tasas_negociacion = summarize(tasas_neg);
    result.tasas_faciales = summarize(tasas_fac);
    result.valores_mercado = summarize(valores_merc);

    // ---- Rendimientos FIC ----
    for (const auto& fondo : extracto.fondos) {
        for (const auto& [periodo, valor] : fondo.rentabilidades_historicas) {
            result.rendimientos_fic[periodo] = valor;
        }
    }

    // ---- Alertas ----
    // Outliers en tasas de valoracion
    auto outlier_idx = findOutliers(tasas_val);
    for (int idx : outlier_idx) {
        if (idx < static_cast<int>(extracto.renta_fija.size())) {
            result.alertas.push_back({
                "WARNING",
                "Tasa de valoracion atipica en " + extracto.renta_fija[idx].nemotecnico +
                ": " + std::to_string(extracto.renta_fija[idx].tasa_valoracion) + "%"
            });
        }
    }

    // Concentracion excesiva
    for (const auto& [key, pct] : result.composicion_porcentaje) {
        if (pct > 80.0) {
            result.alertas.push_back({
                "WARNING",
                "Alta concentracion en " + key + ": " + std::to_string(pct) + "%"
            });
        }
    }

    // Verificar si FIC tiene rendimiento negativo
    for (const auto& fondo : extracto.fondos) {
        if (fondo.rendimientos < 0) {
            result.alertas.push_back({
                "CRITICAL",
                "Rendimiento negativo en fondo: " + fondo.nombre_fondo
            });
        }
    }

    // Instrumentos con tasa facial muy baja
    for (const auto& inst : extracto.renta_fija) {
        if (inst.tasa_facial > 0 && inst.tasa_facial < 5.0) {
            result.alertas.push_back({
                "INFO",
                "Tasa facial baja en " + inst.nemotecnico +
                ": " + std::to_string(inst.tasa_facial) + "%"
            });
        }
    }

    std::cout << "[StatisticalAnalyzer] Analisis completado. "
              << result.alertas.size() << " alertas generadas." << std::endl;

    return result;
}

AnalysisResult StatisticalAnalyzer::analyzeTimeSeries(
    const std::vector<ExtractoCompleto>& extractos) {

    std::cout << "[StatisticalAnalyzer] Analizando serie temporal de "
              << extractos.size() << " extractos..." << std::endl;

    AnalysisResult result;
    if (extractos.empty()) return result;

    // Analizar el ultimo extracto como base
    result = analyze(extractos.back());

    // NUEVO: metricas avanzadas (CFO-grade)
    FinanceConfig cfg = FinanceConfig::load("data/finance_config.json");
    cfg.print();
    result.avanzadas = computeAdvanced(extractos.back(), extractos, cfg);

    // Construir serie temporal
    for (const auto& ext : extractos) {
        std::map<std::string, double> point;
        point["total_portafolio"] = ext.resumen.total_portafolio;
        point["total_activos"] = ext.resumen.total_activos;

        for (const auto& [key, val] : ext.resumen.activos) {
            point[key] = val;
        }

        result.serie_temporal.push_back(point);
    }

    // Tendencia del portafolio
    if (extractos.size() >= 2) {
        double first = extractos.front().resumen.total_portafolio;
        double last = extractos.back().resumen.total_portafolio;
        if (first > 0) {
            double growth = ((last - first) / first) * 100.0;
            result.alertas.push_back({
                "INFO",
                "Crecimiento del portafolio: " + std::to_string(growth) +
                "% en " + std::to_string(extractos.size()) + " periodos"
            });
        }
    }

    return result;
}

std::vector<std::map<std::string, std::string>> StatisticalAnalyzer::loadHistoricalData(
    const std::string& csv_path) {

    std::cout << "[StatisticalAnalyzer] Cargando datos historicos de: " << csv_path << std::endl;
    std::vector<std::map<std::string, std::string>> records;

    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "[StatisticalAnalyzer] No se pudo abrir: " << csv_path << std::endl;
        return records;
    }

    std::string line;
    std::vector<std::string> headers;

    // Leer headers
    if (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string header;
        while (std::getline(iss, header, ',')) {
            // Trim
            while (!header.empty() && (header.front() == ' ' || header.front() == '\t'))
                header.erase(header.begin());
            while (!header.empty() && (header.back() == ' ' || header.back() == '\t' ||
                                        header.back() == '\r' || header.back() == '\n'))
                header.pop_back();
            headers.push_back(header);
        }
    }

    // Leer datos
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        // Eliminar \r si existe
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream iss(line);
        std::string field;
        std::map<std::string, std::string> record;
        size_t col = 0;

        while (std::getline(iss, field, ',') && col < headers.size()) {
            // Trim
            while (!field.empty() && field.front() == ' ') field.erase(field.begin());
            while (!field.empty() && field.back() == ' ') field.pop_back();
            record[headers[col]] = field;
            col++;
        }

        if (!record.empty()) {
            records.push_back(record);
        }
    }

    std::cout << "[StatisticalAnalyzer] " << records.size()
              << " registros historicos cargados" << std::endl;

    return records;
}

json AnalysisResult::to_json() const {
    json j;
    j["composicion_porcentaje"] = composicion_porcentaje;

    auto stat_to_json = [](const StatSummary& s) -> json {
        return {
            {"mean", s.mean}, {"std_dev", s.std_dev}, {"median", s.median},
            {"min", s.min_val}, {"max", s.max_val}, {"count", s.count}
        };
    };

    j["tasas_valoracion"] = stat_to_json(tasas_valoracion);
    j["tasas_negociacion"] = stat_to_json(tasas_negociacion);
    j["tasas_faciales"] = stat_to_json(tasas_faciales);
    j["valores_mercado"] = stat_to_json(valores_mercado);
    j["rendimientos_fic"] = rendimientos_fic;

    json alertas_arr = json::array();
    for (const auto& a : alertas) {
        alertas_arr.push_back({{"type", a.type}, {"message", a.message}});
    }
    j["alertas"] = alertas_arr;

    j["avanzadas"] = avanzadas.to_json();
    return j;
}

// ==========================================================================
// AdvancedMetrics::to_json
// ==========================================================================
nlohmann::json AdvancedMetrics::to_json() const {
    json j;
    j["yield_ponderado_pct"]        = yield_ponderado_pct;
    j["yield_facial_ponderado_pct"] = yield_facial_ponderado_pct;
    j["spread_promedio_pct"]        = spread_promedio_pct;
    j["duracion_macaulay_anos"]     = duracion_macaulay_anos;
    j["duracion_modificada"]        = duracion_modificada;
    j["hhi"]                        = hhi;
    j["hhi_categoria"]              = hhi_categoria;
    j["top1_exposure_pct"]          = top1_exposure_pct;
    j["top3_exposure_pct"]          = top3_exposure_pct;
    j["top5_exposure_pct"]          = top5_exposure_pct;
    j["vencimientos_buckets"]       = vencimientos_buckets;
    j["vencimientos_count"]         = vencimientos_count;
    j["dias_promedio_vencimiento"]  = dias_promedio_vencimiento;
    j["spread_por_cdt_pct"]         = spread_por_cdt_pct;
    j["sharpe_fic"]                 = sharpe_fic;
    j["rentab_fic_anual_pct"]       = rentab_fic_anual_pct;
    j["sigma_fic_pct"]              = sigma_fic_pct;
    j["max_drawdown_pct"]           = max_drawdown_pct;
    j["current_drawdown_pct"]       = current_drawdown_pct;
    j["crecimiento_periodo_pct"]    = crecimiento_periodo_pct;
    j["crecimiento_anualizado_pct"] = crecimiento_anualizado_pct;
    j["retorno_real_pct"]           = retorno_real_pct;
    j["skewness_tasas"]             = skewness_tasas;
    j["kurtosis_tasas"]             = kurtosis_tasas;
    j["cv_tasas"]                   = cv_tasas;
    j["ci95_lower"]                 = ci95_lower;
    j["ci95_upper"]                 = ci95_upper;
    j["config"] = {
        {"tasa_libre_riesgo", config_usada.tasa_libre_riesgo},
        {"inflacion_anual",   config_usada.inflacion_anual},
        {"benchmark_rendto",  config_usada.benchmark_rendto},
    };
    return j;
}

// ==========================================================================
// HELPERS DE FINANZAS
// Cada formula esta documentada con su definicion matematica.
// ==========================================================================

// Asimetria (skewness) - g1 de Fisher.
// g1 = (1/n) * Σ((xᵢ - μ)/σ)³
// > 0: cola larga a la derecha (valores altos atipicos)
// < 0: cola larga a la izquierda
double StatisticalAnalyzer::skewness(const std::vector<double>& x) {
    if (x.size() < 3) return 0.0;
    double m = mean(x);
    double s = stdDev(x);
    if (s == 0) return 0.0;
    double acc = 0;
    for (double v : x) {
        double z = (v - m) / s;
        acc += z * z * z;
    }
    return acc / x.size();
}

// Curtosis (Fisher, exceso sobre normal). Normal = 0.
// kurt = (1/n) * Σ((xᵢ - μ)/σ)⁴ - 3
// > 0: leptocurtica (colas pesadas, mas extremos)
// < 0: platicurtica (colas ligeras)
double StatisticalAnalyzer::kurtosis(const std::vector<double>& x) {
    if (x.size() < 4) return 0.0;
    double m = mean(x);
    double s = stdDev(x);
    if (s == 0) return 0.0;
    double acc = 0;
    for (double v : x) {
        double z = (v - m) / s;
        acc += z * z * z * z;
    }
    return (acc / x.size()) - 3.0;
}

// Maximum Drawdown sobre una serie de valores de portafolio.
// MDD = max sobre t de  (max_acumulado_hasta_t - V_t) / max_acumulado_hasta_t
// Se devuelve como NEGATIVO (-0.15 = caida 15% desde el pico).
double StatisticalAnalyzer::maxDrawdown(const std::vector<double>& serie) {
    if (serie.size() < 2) return 0.0;
    double peak = serie.front();
    double mdd  = 0.0;
    for (double v : serie) {
        if (v > peak) peak = v;
        if (peak > 0) {
            double dd = (v - peak) / peak;  // <= 0
            if (dd < mdd) mdd = dd;
        }
    }
    return mdd * 100.0;  // como porcentaje, negativo
}

// Sharpe Ratio = (r - r_f) / σ
// Mide rendimiento por unidad de riesgo. > 1 considerado bueno; > 2 muy bueno.
double StatisticalAnalyzer::sharpeRatio(double r, double sigma, double rf) {
    if (sigma == 0) return 0.0;
    return (r - rf) / sigma;
}

// Retorno real con formula de Fisher: (1 + nominal) / (1 + inflacion) - 1
// Mas exacta que la simple resta cuando nominal o inflacion son altos.
double StatisticalAnalyzer::realReturnFisher(double nominal, double inflacion) {
    return (1.0 + nominal) / (1.0 + inflacion) - 1.0;
}

// Dias entre dos fechas en formato ISO YYYY-MM-DD. Devuelve 0 si no parseable.
int StatisticalAnalyzer::diasEntreFechasISO(const std::string& a,
                                             const std::string& b) {
    auto parse = [](const std::string& s, std::tm& out) -> bool {
        if (s.size() < 10) return false;
        try {
            out.tm_year = std::stoi(s.substr(0, 4)) - 1900;
            out.tm_mon  = std::stoi(s.substr(5, 2)) - 1;
            out.tm_mday = std::stoi(s.substr(8, 2));
            out.tm_hour = out.tm_min = out.tm_sec = 0;
            out.tm_isdst = 0;
            return true;
        } catch (...) { return false; }
    };
    std::tm ta = {}, tb = {};
    if (!parse(a, ta) || !parse(b, tb)) return 0;
    std::time_t t1 = std::mktime(&ta);
    std::time_t t2 = std::mktime(&tb);
    if (t1 == -1 || t2 == -1) return 0;
    return static_cast<int>(std::difftime(t2, t1) / (60 * 60 * 24));
}

// ==========================================================================
// computeAdvanced - calcula todas las metricas avanzadas del extracto actual
// ==========================================================================
AdvancedMetrics StatisticalAnalyzer::computeAdvanced(
    const ExtractoCompleto& extracto,
    const std::vector<ExtractoCompleto>& serie,
    const FinanceConfig& cfg) {

    AdvancedMetrics m;
    m.config_usada = cfg;

    // ---------- 1. Yield ponderado y spread ----------
    double total_rf = 0.0;
    for (const auto& cdt : extracto.renta_fija) total_rf += cdt.valor_mercado;

    if (total_rf > 0) {
        double yv = 0.0, yf = 0.0;
        for (const auto& cdt : extracto.renta_fija) {
            double w = cdt.valor_mercado / total_rf;
            yv += w * cdt.tasa_valoracion;
            yf += w * cdt.tasa_facial;
            m.spread_por_cdt_pct[cdt.nemotecnico] =
                cdt.tasa_valoracion - cdt.tasa_facial;
        }
        m.yield_ponderado_pct        = yv;
        m.yield_facial_ponderado_pct = yf;
        m.spread_promedio_pct        = yv - yf;
    }

    // ---------- 2. Duracion (Macaulay simplificada) ----------
    // tᵢ = días desde "hoy" hasta vencimiento / 365
    // wᵢ = market_value / total_RF
    // Macaulay = Σ wᵢ × tᵢ
    // Modificada = Macaulay / (1 + yield)
    // Para los calculos basados en "dias hasta el vencimiento" usamos como
    // referencia la FECHA DEL EXTRACTO (no la fecha actual del sistema).
    // Eso es lo correcto: el inversionista ve el extracto en su fecha y los
    // dias deben calcularse desde ese punto en el tiempo. Si el extracto no
    // tiene fecha, caemos a hoy como fallback.
    auto today = [&]() -> std::string {
        if (!extracto.resumen.fecha_extracto.empty())
            return extracto.resumen.fecha_extracto;
        std::time_t now = std::time(nullptr);
        std::tm* tm_now = std::localtime(&now);
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_now);
        return std::string(buf);
    }();

    if (total_rf > 0) {
        double mac = 0.0;
        double total_no_expired = 0.0;
        // Macaulay: solo con instrumentos VIVOS (vencimiento > extracto).
        for (const auto& cdt : extracto.renta_fija) {
            int dias = diasEntreFechasISO(today, cdt.fecha_vencimiento);
            if (dias <= 0) continue;            // expirado: no contribuye
            total_no_expired += cdt.valor_mercado;
        }
        if (total_no_expired > 0) {
            for (const auto& cdt : extracto.renta_fija) {
                int dias = diasEntreFechasISO(today, cdt.fecha_vencimiento);
                if (dias <= 0) continue;
                double t = dias / 365.0;
                double w = cdt.valor_mercado / total_no_expired;
                mac += w * t;
            }
        }
        m.duracion_macaulay_anos = mac;
        double y = m.yield_ponderado_pct / 100.0;
        m.duracion_modificada = (1 + y) > 0 ? mac / (1 + y) : 0;
    }

    // ---------- 3. HHI y Top-N ----------
    // HHI = Σ(wᵢ²) × 10000  con wᵢ = participacion del holding en el portafolio total
    std::vector<double> holdings_valor;
    for (const auto& cdt    : extracto.renta_fija)      holdings_valor.push_back(cdt.valor_mercado);
    for (const auto& fondo  : extracto.fondos)          holdings_valor.push_back(fondo.nuevo_saldo);
    for (const auto& saldo  : extracto.saldos_efectivo) holdings_valor.push_back(saldo.saldo_total);

    double total_port = 0.0;
    for (double v : holdings_valor) total_port += v;
    if (total_port > 0) {
        double hhi = 0.0;
        for (double v : holdings_valor) {
            double w = v / total_port;
            hhi += w * w;
        }
        m.hhi = hhi * 10000.0;
        if      (m.hhi < cfg.hhi_bajo)  m.hhi_categoria = "bajo";
        else if (m.hhi < cfg.hhi_alto)  m.hhi_categoria = "moderado";
        else                            m.hhi_categoria = "alto";

        std::sort(holdings_valor.begin(), holdings_valor.end(), std::greater<double>());
        if (holdings_valor.size() >= 1)
            m.top1_exposure_pct = (holdings_valor[0] / total_port) * 100;
        double sum3 = 0;
        for (size_t i = 0; i < std::min<size_t>(3, holdings_valor.size()); ++i) sum3 += holdings_valor[i];
        m.top3_exposure_pct = (sum3 / total_port) * 100;
        double sum5 = 0;
        for (size_t i = 0; i < std::min<size_t>(5, holdings_valor.size()); ++i) sum5 += holdings_valor[i];
        m.top5_exposure_pct = (sum5 / total_port) * 100;
    }

    // ---------- 4. Vencimientos por bucket ----------
    // Usa la misma fecha de referencia (fecha_extracto) ya calculada arriba.
    // Los instrumentos vencidos se EXCLUYEN del ladder: el maturity ladder
    // es una herramienta de planificacion de liquidez futura; incluir
    // vencidos distorsiona la lectura. Se cuentan aparte para el footer.
    auto label_for = [&](int dias) -> std::string {
        if (dias < cfg.dias_corto)   return "<" + std::to_string(cfg.dias_corto) + "d";
        if (dias < cfg.dias_medio)   return std::to_string(cfg.dias_corto) + "-" +
                                            std::to_string(cfg.dias_medio) + "d";
        if (dias < cfg.dias_largo)   return std::to_string(cfg.dias_medio) + "d-" +
                                            std::to_string(cfg.dias_largo / 365) + "a";
        return ">" + std::to_string(cfg.dias_largo / 365) + "a";
    };

    double weighted_dias = 0.0;
    double total_no_expired = 0.0;
    int n_vencidos = 0;
    for (const auto& cdt : extracto.renta_fija) {
        int dias = diasEntreFechasISO(today, cdt.fecha_vencimiento);
        if (dias <= 0) {
            n_vencidos++;
            continue;   // excluir del ladder
        }
        std::string b = label_for(dias);
        m.vencimientos_buckets[b] += cdt.valor_mercado;
        m.vencimientos_count[b]   += 1;
        total_no_expired += cdt.valor_mercado;
        weighted_dias += cdt.valor_mercado * dias;
    }
    // dias_promedio: ponderado solo sobre instrumentos vivos (sin expirados)
    m.dias_promedio_vencimiento = total_no_expired > 0 ?
                                    weighted_dias / total_no_expired : 0.0;
    m.n_vencidos = n_vencidos;

    // ---------- 5. FIC: Sharpe y rentabilidad ----------
    if (!extracto.fondos.empty()) {
        const auto& f = extracto.fondos.front();
        // Si esta presente "Ultimo Año" lo usamos; sino "12m" o el promedio
        double r_anual = 0.0;
        for (const auto& [k, v] : f.rentabilidades_historicas) {
            std::string lk = k;
            for (auto& c : lk) c = static_cast<char>(std::tolower(c));
            if (lk.find("ano") != std::string::npos ||
                lk.find("ultimo año") != std::string::npos ||
                lk.find("ultimo ano") != std::string::npos) {
                r_anual = v;  // ya viene en %
                break;
            }
        }
        if (r_anual == 0 && !f.rentabilidades_historicas.empty()) {
            // promedio
            double s = 0; int n = 0;
            for (const auto& [_, v] : f.rentabilidades_historicas) { s += v; ++n; }
            if (n) r_anual = s / n;
        }
        // Sigma del FIC: sigma de la dispersion de las rentabilidades historicas
        // (proxy razonable cuando no hay serie completa)
        std::vector<double> rents;
        for (const auto& [_, v] : f.rentabilidades_historicas) rents.push_back(v);
        double sigma = stdDev(rents);

        m.rentab_fic_anual_pct = r_anual;
        m.sigma_fic_pct        = sigma;
        m.sharpe_fic = sharpeRatio(r_anual / 100.0, sigma / 100.0,
                                    cfg.tasa_libre_riesgo);
    }

    // ---------- 6. Drawdown sobre serie temporal ----------
    if (serie.size() >= 2) {
        std::vector<double> totales;
        for (const auto& e : serie)
            if (e.resumen.total_portafolio > 0)
                totales.push_back(e.resumen.total_portafolio);
        if (totales.size() >= 2) {
            m.max_drawdown_pct = maxDrawdown(totales);
            // current drawdown: caida desde max acumulado hasta el ultimo punto
            double peak = totales.front();
            for (double v : totales) if (v > peak) peak = v;
            m.current_drawdown_pct = peak > 0
                ? ((totales.back() - peak) / peak) * 100.0
                : 0.0;

            double inicio = totales.front();
            double fin    = totales.back();
            if (inicio > 0) {
                m.crecimiento_periodo_pct = ((fin / inicio) - 1.0) * 100.0;
                // Anualizado asumiendo serie mensual: n meses = totales.size()
                double n_meses = static_cast<double>(totales.size() - 1);
                if (n_meses > 0) {
                    double anos = n_meses / 12.0;
                    m.crecimiento_anualizado_pct =
                        (std::pow(fin / inicio, 1.0 / anos) - 1.0) * 100.0;
                }
            }
        }
    }

    // ---------- 7. Retorno real (Fisher) ----------
    // Usa la rentabilidad anualizada del portafolio si la calculamos,
    // sino la del FIC como proxy
    double r_nom = m.crecimiento_anualizado_pct != 0
                     ? m.crecimiento_anualizado_pct / 100.0
                     : m.rentab_fic_anual_pct / 100.0;
    m.retorno_real_pct = realReturnFisher(r_nom, cfg.inflacion_anual) * 100.0;

    // ---------- 8. Estadistica avanzada sobre tasas de valoracion ----------
    std::vector<double> tasas_v;
    for (const auto& cdt : extracto.renta_fija)
        tasas_v.push_back(cdt.tasa_valoracion);

    if (tasas_v.size() >= 2) {
        double mu    = mean(tasas_v);
        double sigma = stdDev(tasas_v);
        m.skewness_tasas = skewness(tasas_v);
        m.kurtosis_tasas = kurtosis(tasas_v);
        m.cv_tasas       = mu != 0 ? sigma / mu : 0;
        // IC 95% con t aprox = 1.96 (para n grande); razonable para n>=4 con margen
        double margen = 1.96 * sigma / std::sqrt(static_cast<double>(tasas_v.size()));
        m.ci95_lower = mu - margen;
        m.ci95_upper = mu + margen;
    }

    return m;
}