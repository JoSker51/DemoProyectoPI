#ifndef STATISTICAL_ANALYZER_H
#define STATISTICAL_ANALYZER_H

#include <string>
#include <vector>
#include <map>
#include "data_structurer.h"
#include "finance_config.h"

struct StatSummary {
    double mean = 0.0;
    double std_dev = 0.0;
    double median = 0.0;
    double min_val = 0.0;
    double max_val = 0.0;
    int count = 0;
};

struct Alert {
    std::string type;       // "WARNING", "INFO", "CRITICAL"
    std::string message;
};

// ============================================================
// Metricas avanzadas a nivel de portafolio (Tier 1 + Tier 2)
// Todas calculadas con formulas matematicas explicitas; ninguna IA.
// ============================================================
struct AdvancedMetrics {
    // --- Rendimiento ---
    double yield_ponderado_pct = 0.0;     // Σ(wᵢ × tasa_valoraciónᵢ) en %
    double yield_facial_ponderado_pct = 0.0;
    double spread_promedio_pct = 0.0;     // (valoracion - facial) ponderado

    // --- Riesgo de tasa ---
    double duracion_macaulay_anos = 0.0;  // Σ(wᵢ × tᵢ) en años
    double duracion_modificada    = 0.0;  // Macaulay / (1 + yield)

    // --- Concentracion ---
    double hhi               = 0.0;        // 0-10000
    std::string hhi_categoria = "";        // "bajo" | "moderado" | "alto"
    double top1_exposure_pct = 0.0;
    double top3_exposure_pct = 0.0;
    double top5_exposure_pct = 0.0;

    // --- Vencimientos (cash-flow ladder) ---
    // Buckets: "<corto", "corto-medio", "medio-largo", ">largo"
    std::map<std::string, double> vencimientos_buckets;  // monto en $ por bucket
    std::map<std::string, int>    vencimientos_count;    // # CDTs por bucket
    double dias_promedio_vencimiento = 0.0;

    // --- Spreads facial vs valoracion por CDT ---
    std::map<std::string, double> spread_por_cdt_pct;

    // --- FIC: ratios de desempeno ---
    double sharpe_fic       = 0.0;        // (ret - rfree) / sigma
    double rentab_fic_anual_pct = 0.0;
    double sigma_fic_pct    = 0.0;

    // --- Serie temporal y drawdown ---
    double max_drawdown_pct = 0.0;        // peor caída desde maximo
    double current_drawdown_pct = 0.0;    // caida actual desde max
    double crecimiento_periodo_pct = 0.0;
    double crecimiento_anualizado_pct = 0.0;

    // --- Retorno real (descontado de inflacion) ---
    double retorno_real_pct = 0.0;        // (1+nom)/(1+inf)-1

    // --- Estadistica avanzada (sobre tasas) ---
    double skewness_tasas = 0.0;
    double kurtosis_tasas = 0.0;
    double cv_tasas       = 0.0;          // sigma/mu (coef. variacion)
    double ci95_lower     = 0.0;
    double ci95_upper     = 0.0;

    // Configuracion usada (para trazabilidad)
    FinanceConfig config_usada;

    nlohmann::json to_json() const;
};

struct AnalysisResult {
    // Composicion del portafolio (%)
    std::map<std::string, double> composicion_porcentaje;

    // Estadisticas de tasas de renta fija
    StatSummary tasas_valoracion;
    StatSummary tasas_negociacion;
    StatSummary tasas_faciales;
    StatSummary valores_mercado;

    // Rendimiento FIC
    std::map<std::string, double> rendimientos_fic;

    // Alertas
    std::vector<Alert> alertas;

    // Serie temporal
    std::vector<std::map<std::string, double>> serie_temporal;

    // NUEVO: metricas avanzadas (Tier 1 + 2)
    AdvancedMetrics avanzadas;

    nlohmann::json to_json() const;
};

class StatisticalAnalyzer {
public:
    static double mean(const std::vector<double>& data);
    static double stdDev(const std::vector<double>& data);
    static double median(std::vector<double> data);
    static double minVal(const std::vector<double>& data);
    static double maxVal(const std::vector<double>& data);
    static StatSummary summarize(const std::vector<double>& data);
    static std::vector<int> findOutliers(const std::vector<double>& data, double k = 2.0);

    AnalysisResult analyze(const ExtractoCompleto& extracto);
    AnalysisResult analyzeTimeSeries(const std::vector<ExtractoCompleto>& extractos);

    // NUEVO: calcula AdvancedMetrics. Se invoca desde analyzeTimeSeries.
    // Si serie esta vacia, max_drawdown queda en 0.
    AdvancedMetrics computeAdvanced(const ExtractoCompleto& extracto,
                                     const std::vector<ExtractoCompleto>& serie,
                                     const FinanceConfig& cfg);

    // Helpers de finanzas (estaticos para que sean unitestables)
    static double skewness(const std::vector<double>& x);
    static double kurtosis(const std::vector<double>& x);
    static double maxDrawdown(const std::vector<double>& serie_valores);
    static double sharpeRatio(double rendimiento_anual, double sigma_anual,
                              double tasa_libre_riesgo);
    static double realReturnFisher(double nominal, double inflacion);
    static int    diasEntreFechasISO(const std::string& fecha_inicio,
                                      const std::string& fecha_fin);

    static std::vector<std::map<std::string, std::string>> loadHistoricalData(
        const std::string& csv_path);
};

#endif // STATISTICAL_ANALYZER_H
