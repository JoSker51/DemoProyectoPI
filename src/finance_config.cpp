#include "finance_config.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

FinanceConfig FinanceConfig::load(const std::string& json_path) {
    FinanceConfig cfg;
    std::ifstream f(json_path);
    if (!f.is_open()) {
        std::cerr << "[FinanceConfig] No se encontro " << json_path
                  << ", usando defaults.\n";
        return cfg;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[FinanceConfig] Error parseando JSON: " << e.what()
                  << ". Usando defaults.\n";
        return cfg;
    }

    if (j.contains("tasa_libre_riesgo_anual"))
        cfg.tasa_libre_riesgo = j["tasa_libre_riesgo_anual"].get<double>();
    if (j.contains("inflacion_anual"))
        cfg.inflacion_anual   = j["inflacion_anual"].get<double>();
    if (j.contains("benchmark_rendimiento_anual"))
        cfg.benchmark_rendto  = j["benchmark_rendimiento_anual"].get<double>();
    if (j.contains("umbrales_concentracion")) {
        const auto& uc = j["umbrales_concentracion"];
        if (uc.contains("hhi_bajo")) cfg.hhi_bajo = uc["hhi_bajo"].get<double>();
        if (uc.contains("hhi_alto")) cfg.hhi_alto = uc["hhi_alto"].get<double>();
    }
    if (j.contains("umbrales_duracion_dias")) {
        const auto& ud = j["umbrales_duracion_dias"];
        if (ud.contains("corto")) cfg.dias_corto = ud["corto"].get<int>();
        if (ud.contains("medio")) cfg.dias_medio = ud["medio"].get<int>();
        if (ud.contains("largo")) cfg.dias_largo = ud["largo"].get<int>();
    }
    if (j.contains("umbral_outliers_sigma"))
        cfg.sigma_outliers = j["umbral_outliers_sigma"].get<double>();

    return cfg;
}

void FinanceConfig::print() const {
    std::cout << "[FinanceConfig] Parametros activos:\n"
              << "  Tasa libre de riesgo: " << (tasa_libre_riesgo * 100) << "%\n"
              << "  Inflacion anual:      " << (inflacion_anual   * 100) << "%\n"
              << "  Benchmark rendto:     " << (benchmark_rendto  * 100) << "%\n"
              << "  HHI bajo/alto:        " << hhi_bajo << " / " << hhi_alto << "\n"
              << "  Buckets dias:         <" << dias_corto << " | <" << dias_medio
              << " | <" << dias_largo << " | resto\n"
              << "  Sigma outliers:       " << sigma_outliers << "\n";
}
