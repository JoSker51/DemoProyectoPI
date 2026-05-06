#ifndef FINANCE_CONFIG_H
#define FINANCE_CONFIG_H

#include <string>

// Parametros financieros configurables. Se cargan desde
// data/finance_config.json (con defaults si no existe).
//
// Filosofia: TODO es configurable, NADA es magia. El usuario / analista
// puede ajustar los presets sin recompilar para sensibilizar el analisis.
struct FinanceConfig {
    // --- Datos de referencia macro ---
    double tasa_libre_riesgo  = 0.0925;   // IBR/DTF anual decimal (default 9.25%)
    double inflacion_anual    = 0.0480;   // IPC anual decimal (default 4.80%)
    double benchmark_rendto   = 0.1100;   // Rendimiento referencia (default 11%)

    // --- Umbrales de concentracion (Indice HHI) ---
    double hhi_bajo  = 1500;   // < 1500 = diversificado
    double hhi_alto  = 2500;   // > 2500 = alta concentracion

    // --- Buckets de vencimiento (en dias) ---
    int dias_corto  = 90;
    int dias_medio  = 365;
    int dias_largo  = 1095;

    // --- Outliers ---
    double sigma_outliers = 2.0;

    // Carga desde JSON. Si el archivo no existe, retorna defaults sin error.
    static FinanceConfig load(const std::string& json_path);

    // Imprime los valores activos (para log).
    void print() const;
};

#endif // FINANCE_CONFIG_H
