#include "extracto_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

// Helpers que devuelven valor por defecto si el campo es null o falta.
// Mantienen el codigo limpio sin try/catch en cada campo.

std::string js(const json& obj, const char* key, const std::string& def = "") {
    if (!obj.contains(key) || obj[key].is_null()) return def;
    if (obj[key].is_string()) return obj[key].get<std::string>();
    return def;
}

double jd(const json& obj, const char* key, double def = 0.0) {
    if (!obj.contains(key) || obj[key].is_null()) return def;
    if (obj[key].is_number()) return obj[key].get<double>();
    return def;
}

bool has_value(const json& obj, const char* key) {
    return obj.contains(key) && !obj[key].is_null();
}

// Convierte un porcentaje decimal del schema (0.0335 = 3.35%) al formato
// que usa el codigo legado en rentabilidades_historicas (% como 3.35).
double pct_to_legacy(const json& obj, const char* key) {
    if (!has_value(obj, key)) return 0.0;
    return obj[key].get<double>() * 100.0;
}

void mapResumen(const json& root, ResumenPortafolio& r) {
    const auto& holder = root.value("account_holder", json::object());
    const auto& period = root.value("period",         json::object());
    const auto& issuer = root.value("issuer",         json::object());
    const auto& summary = root.value("summary",       json::object());

    r.fecha_extracto = js(period, "end_date");
    r.nombre_cliente = js(holder, "full_name");
    r.nit            = js(holder, "id_number");
    r.direccion      = js(holder, "address");
    r.ciudad         = js(holder, "city");
    r.telefono       = js(holder, "phone");

    if (holder.contains("advisor") && !holder["advisor"].is_null()) {
        r.asesor       = js(holder["advisor"], "name");
        r.email_asesor = js(holder["advisor"], "email");
    }

    r.total_activos    = jd(summary, "total_assets");
    r.total_pasivos    = jd(summary, "total_liabilities");
    r.total_portafolio = has_value(summary, "net_worth")
                           ? jd(summary, "net_worth")
                           : r.total_activos;

    (void)issuer;  // disponible si despues queremos exponerlo
}

void mapCashBalance(const json& section, ExtractoCompleto& out) {
    double subtotal = 0.0;
    bool subtotal_present = false;
    for (const auto& it : section.value("items", json::array())) {
        SaldoEfectivo s;
        s.cuenta            = js(it, "account_name");
        s.saldo_disponible  = jd(it, "available");
        s.saldo_bloqueado   = jd(it, "blocked");
        s.saldo_canje       = jd(it, "in_transit");
        s.saldo_total       = jd(it, "total");
        out.saldos_efectivo.push_back(s);
        subtotal += s.saldo_total;
    }
    if (has_value(section, "subtotal")) {
        subtotal = jd(section, "subtotal");
        subtotal_present = true;
    }
    out.resumen.activos["Saldos en Efectivo"] = subtotal;
    (void)subtotal_present;
}

void mapFixedIncome(const json& section, ExtractoCompleto& out) {
    double subtotal = 0.0;
    for (const auto& it : section.value("items", json::array())) {
        InstrumentoRentaFija inst;
        inst.nemotecnico       = js(it, "instrument_id");
        inst.fecha_emision     = js(it, "issue_date");
        inst.fecha_vencimiento = js(it, "maturity_date");
        inst.fecha_compra      = js(it, "purchase_date");
        inst.tasa_facial       = jd(it, "face_rate");
        inst.tasa_negociacion  = jd(it, "negotiated_rate");
        inst.tasa_valoracion   = jd(it, "valuation_rate");
        inst.periodicidad      = js(it, "payment_frequency");
        inst.valor_nominal     = jd(it, "nominal_value");
        inst.valor_mercado     = jd(it, "market_value");
        out.renta_fija.push_back(inst);
        subtotal += inst.valor_mercado;
    }
    if (has_value(section, "subtotal")) {
        subtotal = jd(section, "subtotal");
    }
    out.resumen.activos["Renta Fija"] = subtotal;
}

void mapInvestmentFund(const json& section, ExtractoCompleto& out) {
    FondoInversion f;
    const auto& fund      = section.value("fund",      json::object());
    const auto& movs      = section.value("movements", json::object());
    const auto& holder    = json::object();  // unused, kept for parity

    f.nombre_fondo   = js(fund, "name");
    f.codigo         = js(fund, "code");
    f.administrador  = js(fund, "administrator");
    f.objetivo_inversion = js(fund, "objective");

    f.saldo_anterior = jd(movs, "previous_balance");
    f.adiciones      = jd(movs, "additions");
    f.retiros        = jd(movs, "withdrawals");
    f.rendimientos   = jd(movs, "returns");
    f.nuevo_saldo    = jd(movs, "new_balance");
    f.unidades       = jd(movs, "units");
    f.valor_unidad_final = jd(movs, "unit_value_end");

    // Datos del titular del fondo (mismos que account_holder global)
    f.nombre_inversionista = "";
    f.identificacion       = "";
    (void)holder;

    if (section.contains("historical_returns") && !section["historical_returns"].is_null()) {
        const auto& hr = section["historical_returns"];
        f.rentabilidades_historicas["Mensual"]   = pct_to_legacy(hr, "monthly");
        f.rentabilidades_historicas["Semestral"] = pct_to_legacy(hr, "semi_annual");
        f.rentabilidades_historicas["YTD"]       = pct_to_legacy(hr, "ytd");
        f.rentabilidades_historicas["Ultimo Año"]= pct_to_legacy(hr, "last_year");
        f.rentabilidades_historicas["2 Años"]    = pct_to_legacy(hr, "last_2_years_annualized");
        f.rentabilidades_historicas["3 Años"]    = pct_to_legacy(hr, "last_3_years_annualized");
    }

    out.fondos.push_back(f);
    out.resumen.activos["FIC"] = f.nuevo_saldo;
}

void mapTransactions(const json& section, ExtractoCompleto& out) {
    CuentaTransacciones c;
    c.account_id   = js(section, "account_id");
    c.account_type = js(section, "account_type");
    c.title        = js(section, "title");
    c.currency     = js(section, "currency", "COP");

    for (const auto& it : section.value("items", json::array())) {
        Transaction t;
        t.date         = js(it, "date");
        t.value_date   = js(it, "value_date");
        t.description  = js(it, "description");
        t.reference    = js(it, "reference");
        t.category     = js(it, "category");
        t.debit        = jd(it, "debit");
        t.credit       = jd(it, "credit");
        if (has_value(it, "balance_after")) {
            t.balance_after     = jd(it, "balance_after");
            t.has_balance_after = true;
        }
        c.items.push_back(t);
    }

    if (section.contains("totals") && !section["totals"].is_null()) {
        const auto& tot = section["totals"];
        c.total_debits    = jd(tot, "debits");
        c.total_credits   = jd(tot, "credits");
        c.net             = jd(tot, "net");
        c.ending_balance  = jd(tot, "ending_balance");
        c.has_totals      = true;
    } else {
        for (const auto& it : c.items) {
            c.total_debits  += it.debit;
            c.total_credits += it.credit;
        }
        c.net = c.total_credits - c.total_debits;
        c.has_totals = true;
    }

    out.transacciones.push_back(c);
}

}  // namespace

bool ExtractoLoader::loadFromFile(const std::string& json_path, ExtractoCompleto& out) {
    errors_.clear();
    warnings_.clear();

    std::ifstream file(json_path);
    if (!file.is_open()) {
        errors_.push_back("No se pudo abrir el archivo: " + json_path);
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return loadFromString(ss.str(), out);
}

bool ExtractoLoader::loadFromString(const std::string& json_text, ExtractoCompleto& out) {
    errors_.clear();
    warnings_.clear();

    json root;
    try {
        root = json::parse(json_text);
    } catch (const std::exception& e) {
        errors_.push_back(std::string("JSON invalido: ") + e.what());
        return false;
    }

    // Validacion minima de version. La validacion formal se hace fuera (jsonschema).
    std::string version = root.value("schema_version", "");
    if (version != "1.0") {
        warnings_.push_back("schema_version no es '1.0' (encontrada: '" + version + "')");
    }

    out = ExtractoCompleto{};
    out.source_currency = root.value("currency", std::string("COP"));

    mapResumen(root, out.resumen);

    if (!root.contains("sections") || !root["sections"].is_array()) {
        errors_.push_back("Falta el array 'sections' o tiene tipo invalido.");
        return false;
    }

    int unknown_count = 0;
    for (const auto& section : root["sections"]) {
        const std::string type = js(section, "section_type");
        if      (type == "cash_balance")     mapCashBalance(section, out);
        else if (type == "fixed_income")     mapFixedIncome(section, out);
        else if (type == "investment_fund")  mapInvestmentFund(section, out);
        else if (type == "transactions")     mapTransactions(section, out);
        else {
            ++unknown_count;
            warnings_.push_back("Tipo de seccion desconocido (ignorado): " + type);
        }
    }

    // Recalcular total_portafolio si summary no lo trajo
    if (out.resumen.total_portafolio == 0.0) {
        double t = 0.0;
        for (const auto& [_, v] : out.resumen.activos) t += v;
        out.resumen.total_portafolio = t;
        if (out.resumen.total_activos == 0.0) out.resumen.total_activos = t;
    }

    return errors_.empty();
}
