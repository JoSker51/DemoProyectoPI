#ifndef DATA_STRUCTURER_H
#define DATA_STRUCTURER_H

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "ocr_extractor.h"

struct ResumenPortafolio {
    std::string fecha_extracto;
    std::string nombre_cliente;
    std::string nit;
    std::string direccion;
    std::string ciudad;
    std::string telefono;
    std::string asesor;
    std::string email_asesor;
    std::map<std::string, double> activos;
    double total_activos = 0.0;
    std::map<std::string, double> pasivos;
    double total_pasivos = 0.0;
    double total_portafolio = 0.0;
    nlohmann::json to_json() const;
};

struct InstrumentoRentaFija {
    std::string nemotecnico;
    std::string fecha_emision;
    std::string fecha_vencimiento;
    std::string fecha_compra;
    double tasa_facial = 0.0;
    std::string periodicidad;
    double valor_nominal = 0.0;
    double tasa_negociacion = 0.0;
    double tasa_valoracion = 0.0;
    double valor_mercado = 0.0;
    nlohmann::json to_json() const;
};

struct SaldoEfectivo {
    std::string cuenta;
    double saldo_disponible = 0.0;
    double saldo_canje = 0.0;
    double saldo_bloqueado = 0.0;
    double saldo_total = 0.0;
    nlohmann::json to_json() const;
};

struct FondoInversion {
    std::string nombre_fondo;
    std::string codigo;
    std::string nombre_inversionista;
    std::string identificacion;
    std::string objetivo_inversion;
    std::string administrador;
    double rentabilidad_valor_unidad = 0.0;
    double valor_unidad_final = 0.0;
    std::string fecha_inicio_extracto;
    std::string fecha_fin_extracto;
    double saldo_anterior = 0.0;
    double adiciones = 0.0;
    double retiros = 0.0;
    double rendimientos = 0.0;
    double nuevo_saldo = 0.0;
    double unidades = 0.0;
    std::map<std::string, double> rentabilidades_historicas;
    nlohmann::json to_json() const;
};

struct Transaction {
    std::string date;            // ISO 8601 (YYYY-MM-DD)
    std::string value_date;
    std::string description;
    std::string reference;
    std::string category;
    double debit = 0.0;          // monto positivo; 0 si no aplica
    double credit = 0.0;
    double balance_after = 0.0;
    bool has_balance_after = false;
    nlohmann::json to_json() const;
};

struct CuentaTransacciones {
    std::string account_id;
    std::string account_type;    // "ahorros" | "corriente" | "credito" | "inversion" | "otro"
    std::string title;
    std::string currency = "COP";
    std::vector<Transaction> items;
    double total_debits = 0.0;
    double total_credits = 0.0;
    double net = 0.0;
    double ending_balance = 0.0;
    bool has_totals = false;
    nlohmann::json to_json() const;
};

struct ExtractoCompleto {
    ResumenPortafolio resumen;
    std::vector<SaldoEfectivo> saldos_efectivo;
    std::vector<InstrumentoRentaFija> renta_fija;
    std::vector<FondoInversion> fondos;
    std::vector<CuentaTransacciones> transacciones;   // nuevo: secciones tipo transactions
    std::string source_currency = "COP";              // moneda global del extracto
    nlohmann::json to_json() const;
    bool saveToFile(const std::string& path) const;
};

struct VisualLine {
    int y_center;
    std::vector<OCRResult> blocks;
    std::string fullText() const;
    std::string leftText() const;
    std::string rightText() const;
    std::string moneyValue() const;
    std::string percentValue() const;
};

class DataStructurer {
public:
    static double parseColombianNumber(const std::string& text);
    static double parseColombianPercentage(const std::string& text);
    static std::string cleanOCRText(const std::string& text);
    static std::vector<VisualLine> groupIntoLines(
        const std::vector<OCRResult>& ocr_data, int y_tolerance = 20);
    static const VisualLine* findLineByLabel(
        const std::vector<VisualLine>& lines, const std::string& label);
    static const VisualLine* findLineByLabels(
        const std::vector<VisualLine>& lines,
        const std::vector<std::string>& labels);
    static std::string findNearestMoneyRight(
        const std::vector<OCRResult>& ocr_data,
        const OCRResult& label_block,
        int y_tolerance = 40);

    ResumenPortafolio buildResumen(const std::vector<OCRResult>& ocr_data);
    std::vector<InstrumentoRentaFija> buildRentaFija(
        const std::vector<std::vector<OCRResult>>& table_data);
    std::vector<InstrumentoRentaFija> buildRentaFijaFromLines(
        const std::vector<OCRResult>& ocr_data);
    std::vector<SaldoEfectivo> buildSaldosEfectivo(
        const std::vector<std::vector<OCRResult>>& table_data);
    FondoInversion buildFondo(const std::vector<OCRResult>& ocr_data);

    // Deteccion de tablas basada en bounding boxes del OCR
    // Agrupa bloques OCR en regiones separadas por gaps verticales,
    // luego estructura cada region como tabla (filas x columnas)
    struct OCRTable {
        std::string type;  // "renta_fija", "saldos", "fic_resumen", "unknown"
        std::vector<VisualLine> rows;
        int num_cols = 0;
        cv::Rect bounds;
    };
    std::vector<OCRTable> detectTablesFromOCR(const std::vector<OCRResult>& ocr_data,
                                               int img_width = 0);
    std::vector<std::vector<OCRResult>> ocrTableToGrid(const OCRTable& table);

    bool validate(const ExtractoCompleto& extracto);
    std::vector<std::string> getValidationErrors() const;

private:
    std::vector<std::string> validation_errors_;
    static std::string normalize(const std::string& text);
    static bool fuzzyMatch(const std::string& text, const std::string& keywords);
    static std::string extractMoney(const std::string& text);
    static bool hasMoney(const std::string& text);
    static bool hasPercent(const std::string& text);
};

#endif // DATA_STRUCTURER_H
