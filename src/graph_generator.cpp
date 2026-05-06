#include "graph_generator.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

// =============================================================
// PALETAS Y FORMATEO
// =============================================================
std::vector<cv::Scalar> GraphGenerator::getPalette() {
    return {
        cv::Scalar(185, 128, 41),   // Azul (BGR)
        cv::Scalar(96, 174, 39),    // Verde
        cv::Scalar(60, 76, 231),    // Rojo
        cv::Scalar(18, 156, 243),   // Naranja
        cv::Scalar(173, 68, 142),   // Purpura
        cv::Scalar(219, 152, 52),   // Azul claro
        cv::Scalar(113, 204, 46),   // Verde claro
        cv::Scalar(34, 126, 230),   // Naranja oscuro
        cv::Scalar(166, 165, 149),  // Gris
        cv::Scalar(94, 73, 52),     // Azul oscuro
    };
}

std::string GraphGenerator::formatNumber(double value) {
    std::ostringstream oss;
    if (std::abs(value) >= 1e9)      oss << std::fixed << std::setprecision(2) << (value / 1e9) << "B";
    else if (std::abs(value) >= 1e6) oss << std::fixed << std::setprecision(2) << (value / 1e6) << "M";
    else if (std::abs(value) >= 1e3) oss << std::fixed << std::setprecision(1) << (value / 1e3) << "K";
    else                             oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

std::string GraphGenerator::formatMoney(double value) {
    return "$" + formatNumber(value);
}

std::string GraphGenerator::formatPct(double value, int decimals) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << value << "%";
    return oss.str();
}

void GraphGenerator::drawTitle(cv::Mat& img, const std::string& title) {
    cv::Size ts = cv::getTextSize(title, cv::FONT_HERSHEY_DUPLEX, 0.85, 2, nullptr);
    int x = (img.cols - ts.width) / 2;
    cv::putText(img, title, cv::Point(x, 35), cv::FONT_HERSHEY_DUPLEX,
                0.85, cv::Scalar(30, 30, 30), 2, cv::LINE_AA);
}

void GraphGenerator::drawSubtitle(cv::Mat& img, const std::string& sub, int y) {
    cv::Size ts = cv::getTextSize(sub, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, nullptr);
    int x = (img.cols - ts.width) / 2;
    cv::putText(img, sub, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX,
                0.5, cv::Scalar(90, 90, 90), 1, cv::LINE_AA);
}

void GraphGenerator::drawBar(cv::Mat& img, int x, int y, int w, int h,
                              const cv::Scalar& color) {
    cv::rectangle(img, cv::Point(x, y), cv::Point(x + w, y + h), color, cv::FILLED);
    cv::rectangle(img, cv::Point(x, y), cv::Point(x + w, y + h),
                  cv::Scalar(40, 40, 40), 1);
}

void GraphGenerator::drawLabel(cv::Mat& img, const std::string& text,
                                int x, int y, double scale, const cv::Scalar& color,
                                int thickness) {
    cv::putText(img, text, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX,
                scale, color, thickness, cv::LINE_AA);
}

cv::Rect GraphGenerator::drawAxes(cv::Mat& img, int ml, int mr, int mt, int mb) {
    int x0 = ml,             y0 = mt;
    int x1 = img.cols - mr,  y1 = img.rows - mb;
    // grid sutil
    cv::Scalar grid(230, 230, 230);
    for (int i = 1; i < 5; ++i) {
        int yy = y0 + (y1 - y0) * i / 5;
        cv::line(img, cv::Point(x0, yy), cv::Point(x1, yy), grid, 1, cv::LINE_AA);
    }
    // ejes
    cv::line(img, cv::Point(x0, y0), cv::Point(x0, y1), cv::Scalar(60, 60, 60), 2);
    cv::line(img, cv::Point(x0, y1), cv::Point(x1, y1), cv::Scalar(60, 60, 60), 2);
    return cv::Rect(x0, y0, x1 - x0, y1 - y0);
}

bool GraphGenerator::drawRegressionLine(cv::Mat& img, const cv::Rect& plot,
                                         const std::vector<cv::Point2d>& pts,
                                         double xmin, double xmax,
                                         double ymin, double ymax,
                                         double& slope, double& intercept,
                                         double& r2) {
    if (pts.size() < 2) return false;
    double n = static_cast<double>(pts.size());
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
    for (const auto& p : pts) {
        sx  += p.x;       sy  += p.y;
        sxx += p.x * p.x; sxy += p.x * p.y;
        syy += p.y * p.y;
    }
    double den = n * sxx - sx * sx;
    if (den == 0) return false;
    slope     = (n * sxy - sx * sy) / den;
    intercept = (sy - slope * sx) / n;
    double ss_tot = syy - sy * sy / n;
    double ss_res = 0;
    for (const auto& p : pts) {
        double pred = slope * p.x + intercept;
        ss_res += (p.y - pred) * (p.y - pred);
    }
    r2 = ss_tot > 0 ? 1 - ss_res / ss_tot : 0;

    // Dibujar la linea de extremo a extremo del plot
    auto map_x = [&](double x) {
        return plot.x + static_cast<int>((x - xmin) / (xmax - xmin) * plot.width);
    };
    auto map_y = [&](double y) {
        return plot.y + plot.height -
               static_cast<int>((y - ymin) / (ymax - ymin) * plot.height);
    };
    cv::Point p1(map_x(xmin), map_y(slope * xmin + intercept));
    cv::Point p2(map_x(xmax), map_y(slope * xmax + intercept));
    cv::line(img, p1, p2, cv::Scalar(60, 76, 231), 2, cv::LINE_AA);
    return true;
}

// =============================================================
// 1. YIELD CURVE
// =============================================================
std::string GraphGenerator::generateYieldCurve(
    const std::vector<InstrumentoRentaFija>& cdts,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 1. Yield Curve...\n";
    int W = 900, H = 600;
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(255, 255, 255));
    drawTitle(img, "Yield Curve - Tasa de Valoracion vs Vencimiento");
    drawSubtitle(img, "Cada CDT como punto. Linea roja: tendencia (regresion lineal).");

    if (cdts.empty()) {
        drawLabel(img, "(Sin datos de renta fija)", 50, H / 2, 0.7, cv::Scalar(120, 120, 120));
        cv::imwrite(output_path, img);
        return output_path;
    }

    // Calcular dias a vencimiento. Si fechas vacias -> X = indice
    std::vector<cv::Point2d> data;
    auto today_t = std::time(nullptr);
    std::tm tn_struct = *std::localtime(&today_t);
    auto days_to = [&](const std::string& iso) -> int {
        if (iso.size() < 10) return -1;
        std::tm tm = {};
        try {
            tm.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
            tm.tm_mon  = std::stoi(iso.substr(5, 2)) - 1;
            tm.tm_mday = std::stoi(iso.substr(8, 2));
        } catch (...) { return -1; }
        std::time_t t = std::mktime(&tm);
        std::tm tn_local = tn_struct;
        std::time_t now = std::mktime(&tn_local);
        return static_cast<int>(std::difftime(t, now) / 86400);
    };

    bool dates_degenerate = true;
    for (size_t i = 0; i < cdts.size(); ++i) {
        int d = days_to(cdts[i].fecha_vencimiento);
        if (d < 0) d = static_cast<int>(i + 1) * 90;  // fallback escalonado
        data.emplace_back(static_cast<double>(d), cdts[i].tasa_valoracion);
    }
    // Si todas las X son iguales (datos degenerados), espaciarlas artificialmente
    {
        double mn = data.front().x, mx = data.front().x;
        for (auto& p : data) { mn = std::min(mn, p.x); mx = std::max(mx, p.x); }
        if (mx - mn < 1.0) {
            // espaciar uniformemente, mantener un texto base
            double base = mn;
            for (size_t i = 0; i < data.size(); ++i)
                data[i].x = base + i * 30;  // 30 dias de separacion artificial
        } else {
            dates_degenerate = false;
        }
    }

    double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
    for (auto& p : data) {
        xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
    }
    double xpad = std::max(1.0, (xmax - xmin) * 0.15);
    double ypad = std::max(0.5, (ymax - ymin) * 0.25);
    xmin = std::max(0.0, xmin - xpad); xmax += xpad;
    ymin = std::max(0.0, ymin - ypad); ymax += ypad;

    cv::Rect plot = drawAxes(img, 110, 60, 90, 90);
    drawLabel(img, "Dias a vencimiento", plot.x + plot.width / 2 - 70,
              plot.y + plot.height + 60, 0.55, cv::Scalar(60, 60, 60));
    drawLabel(img, "Tasa Valoracion (%)", 15, plot.y + plot.height / 2,
              0.55, cv::Scalar(60, 60, 60));

    // Eje X: ticks unicos enteros
    std::set<int> seen_x;
    for (int i = 0; i <= 5; ++i) {
        double xv = xmin + (xmax - xmin) * i / 5;
        int xint  = static_cast<int>(std::round(xv));
        if (!seen_x.insert(xint).second) continue;
        int xpix = plot.x + plot.width * i / 5;
        drawLabel(img, std::to_string(xint), xpix - 12, plot.y + plot.height + 22, 0.45);
        cv::line(img, cv::Point(xpix, plot.y + plot.height),
                       cv::Point(xpix, plot.y + plot.height + 5),
                       cv::Scalar(60, 60, 60), 1);
    }
    // Marcas eje Y
    for (int i = 0; i <= 5; ++i) {
        double yv = ymin + (ymax - ymin) * i / 5;
        int ypix = plot.y + plot.height - plot.height * i / 5;
        drawLabel(img, formatPct(yv, 1), plot.x - 75, ypix + 4, 0.45);
    }

    // Regresion solo si los datos no son degenerados
    double slope = 0, intercept = 0, r2 = 0;
    bool reg_ok = false;
    if (!dates_degenerate)
        reg_ok = drawRegressionLine(img, plot, data, xmin, xmax, ymin, ymax,
                                     slope, intercept, r2);

    // Puntos
    auto map_x = [&](double x) {
        return plot.x + static_cast<int>((x - xmin) / (xmax - xmin) * plot.width);
    };
    auto map_y = [&](double y) {
        return plot.y + plot.height -
               static_cast<int>((y - ymin) / (ymax - ymin) * plot.height);
    };
    auto palette = getPalette();

    // Renombrar nemotecnicos duplicados con sufijo (#N)
    std::map<std::string, int> name_count;
    std::vector<std::string> labels;
    for (size_t i = 0; i < cdts.size(); ++i) {
        std::string nm = cdts[i].nemotecnico.empty() ? "CDT" : cdts[i].nemotecnico;
        name_count[nm]++;
        labels.push_back(name_count[nm] > 1 ? nm + " #" + std::to_string(name_count[nm]) : nm);
    }
    // Si todos terminaron con sufijo, retroactivamente al primero
    for (size_t i = 0; i < cdts.size(); ++i) {
        const std::string& nm = cdts[i].nemotecnico;
        if (name_count[nm] > 1 && labels[i] == nm) labels[i] = nm + " #1";
    }

    for (size_t i = 0; i < data.size(); ++i) {
        cv::Point pt(map_x(data[i].x), map_y(data[i].y));
        cv::circle(img, pt, 9, palette[i % palette.size()], cv::FILLED, cv::LINE_AA);
        cv::circle(img, pt, 9, cv::Scalar(40, 40, 40), 1, cv::LINE_AA);
        // Etiqueta arriba del punto, no a la derecha (evita overlap)
        cv::Size lblSz = cv::getTextSize(labels[i], cv::FONT_HERSHEY_SIMPLEX, 0.42, 1, nullptr);
        drawLabel(img, labels[i], pt.x - lblSz.width / 2, pt.y - 14, 0.42,
                  cv::Scalar(60, 60, 60));
    }

    // Footer: en una linea, lejos del label "Dias a vencimiento"
    std::ostringstream oss;
    if (reg_ok) {
        oss << "Regresion: y = " << std::fixed << std::setprecision(4) << slope
            << "x + " << std::setprecision(2) << intercept
            << "   |   R^2 = " << std::setprecision(3) << r2;
    } else {
        oss << "(Datos insuficientes para regresion: fechas iguales o faltantes)";
    }
    drawLabel(img, oss.str(), 60, H - 12, 0.45,
              reg_ok ? cv::Scalar(60, 76, 231) : cv::Scalar(120, 120, 120), 1);

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// 2. MATURITY LADDER
// =============================================================
std::string GraphGenerator::generateMaturityLadder(
    const AdvancedMetrics& adv,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 2. Maturity Ladder...\n";
    int W = 900, H = 600;  // mas alto para dar espacio a labels y footer
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(255, 255, 255));
    drawTitle(img, "Maturity Ladder - Vencimientos por Bucket");
    drawSubtitle(img, "Distribucion de monto por horizonte temporal. Util para liquidez.");

    if (adv.vencimientos_buckets.empty()) {
        drawLabel(img, "(Sin datos de vencimientos)", 50, H / 2, 0.7,
                  cv::Scalar(120, 120, 120));
        cv::imwrite(output_path, img);
        return output_path;
    }

    // Orden canonico de buckets (alfabetico funciona mal aqui)
    std::vector<std::string> buckets;
    for (const auto& [k, _] : adv.vencimientos_buckets) buckets.push_back(k);
    // ordenar por extraccion del primer numero
    auto first_num = [](const std::string& s) {
        for (size_t i = 0; i < s.size(); ++i)
            if (std::isdigit(s[i])) {
                size_t j = i;
                while (j < s.size() && std::isdigit(s[j])) ++j;
                try { return std::stoi(s.substr(i, j - i)); } catch (...) { return 0; }
            }
        return 0;
    };
    std::sort(buckets.begin(), buckets.end(),
              [&](const auto& a, const auto& b) {
                  return first_num(a) < first_num(b);
              });

    cv::Rect plot = drawAxes(img, 100, 60, 90, 130);  // mas margen abajo
    drawLabel(img, "Bucket de vencimiento", plot.x + plot.width / 2 - 90,
              plot.y + plot.height + 80, 0.55, cv::Scalar(60, 60, 60));
    drawLabel(img, "Monto", 30, plot.y + plot.height / 2, 0.55, cv::Scalar(60, 60, 60));

    double maxv = 0;
    for (const auto& b : buckets) maxv = std::max(maxv, adv.vencimientos_buckets.at(b));
    if (maxv == 0) maxv = 1;

    int n = static_cast<int>(buckets.size());
    // Cap el ancho de barra para que no se vea desproporcionada cuando hay 1-2 buckets
    int max_bar = std::min(120, plot.width / std::max(n, 4));
    int bar_w = std::max(40, max_bar);
    auto palette = getPalette();
    for (int i = 0; i < n; ++i) {
        double v = adv.vencimientos_buckets.at(buckets[i]);
        int h    = static_cast<int>(plot.height * (v / maxv));
        int xc   = plot.x + (plot.width * (2 * i + 1)) / (2 * n);
        int xb   = xc - bar_w / 2;
        int yb   = plot.y + plot.height - h;
        drawBar(img, xb, yb, bar_w, h, palette[i % palette.size()]);
        // valor encima
        std::string mlabel = formatMoney(v);
        cv::Size sz = cv::getTextSize(mlabel, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, nullptr);
        drawLabel(img, mlabel, xc - sz.width / 2, yb - 8, 0.5,
                  cv::Scalar(40, 40, 40), 1);
        // bucket label primero (mas cerca del eje)
        cv::Size sz3 = cv::getTextSize(buckets[i], cv::FONT_HERSHEY_SIMPLEX, 0.55, 1, nullptr);
        drawLabel(img, buckets[i], xc - sz3.width / 2, plot.y + plot.height + 25, 0.55,
                  cv::Scalar(40, 40, 40), 1);
        // count debajo
        std::string sub = "(" + std::to_string(adv.vencimientos_count.at(buckets[i]))
                          + " CDT)";
        cv::Size sz2 = cv::getTextSize(sub, cv::FONT_HERSHEY_SIMPLEX, 0.42, 1, nullptr);
        drawLabel(img, sub, xc - sz2.width / 2, plot.y + plot.height + 45, 0.42,
                  cv::Scalar(120, 120, 120));
    }

    // Footer: dias promedio
    std::ostringstream oss;
    oss << "Dias promedio ponderados a vencimiento: "
        << std::fixed << std::setprecision(0) << adv.dias_promedio_vencimiento
        << " dias  ("
        << std::setprecision(2) << adv.dias_promedio_vencimiento / 365.0 << " anos)";
    drawLabel(img, oss.str(), 80, H - 20, 0.5, colorAccent(), 1);

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// 3. PARETO DE CONCENTRACION
// =============================================================
std::string GraphGenerator::generateParetoConcentracion(
    const ExtractoCompleto& extracto,
    const AdvancedMetrics&  adv,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 3. Pareto Concentracion...\n";
    int W = 950, H = 580;
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(255, 255, 255));
    drawTitle(img, "Pareto de Concentracion del Portafolio");
    std::ostringstream sub;
    sub << "HHI = " << std::fixed << std::setprecision(0) << adv.hhi
        << " (" << adv.hhi_categoria << ")  |  Top-1: "
        << std::setprecision(1) << adv.top1_exposure_pct << "%  |  Top-3: "
        << adv.top3_exposure_pct << "%  |  Top-5: " << adv.top5_exposure_pct << "%";
    drawSubtitle(img, sub.str());

    // Reunir holdings
    struct Holding { std::string name; double value; };
    std::vector<Holding> hs;
    std::map<std::string, int> name_count;
    auto unique_name = [&](std::string nm) {
        if (nm.empty()) nm = "RF";
        name_count[nm]++;
        if (name_count[nm] > 1) nm += " #" + std::to_string(name_count[nm]);
        return nm;
    };
    for (const auto& cdt : extracto.renta_fija)
        hs.push_back({unique_name(cdt.nemotecnico), cdt.valor_mercado});
    for (const auto& f : extracto.fondos)
        if (f.nuevo_saldo > 0)
            hs.push_back({f.nombre_fondo.empty() ? "FIC" : f.nombre_fondo, f.nuevo_saldo});
    for (size_t i = 0; i < extracto.saldos_efectivo.size(); ++i)
        if (extracto.saldos_efectivo[i].saldo_total > 0)
            hs.push_back({"Efectivo " + std::to_string(i + 1),
                          extracto.saldos_efectivo[i].saldo_total});

    if (hs.empty()) {
        drawLabel(img, "(Sin holdings)", 50, H / 2, 0.7, cv::Scalar(120, 120, 120));
        cv::imwrite(output_path, img);
        return output_path;
    }
    // Re-fix nemotecnicos repetidos: si hay alguno con #N pero el primero no tiene #1,
    // anadir #1 al primero para consistencia
    {
        std::map<std::string, int> seen;
        for (auto& h : hs) {
            // Quitar sufijo #N si existe
            auto pos = h.name.rfind(" #");
            std::string base = (pos != std::string::npos) ? h.name.substr(0, pos) : h.name;
            seen[base]++;
        }
        std::map<std::string, int> idx;
        for (auto& h : hs) {
            auto pos = h.name.rfind(" #");
            std::string base = (pos != std::string::npos) ? h.name.substr(0, pos) : h.name;
            if (seen[base] > 1) {
                idx[base]++;
                h.name = base + " #" + std::to_string(idx[base]);
            }
        }
    }
    std::sort(hs.begin(), hs.end(),
              [](const Holding& a, const Holding& b) { return a.value > b.value; });

    double total = 0; for (auto& h : hs) total += h.value;
    if (total == 0) total = 1;

    cv::Rect plot = drawAxes(img, 110, 130, 90, 110);
    drawLabel(img, "% Individual", 18, plot.y + plot.height / 2, 0.5,
              cv::Scalar(60, 60, 60));
    drawLabel(img, "% Acum.", plot.x + plot.width + 55,
              plot.y + plot.height / 2, 0.5, cv::Scalar(60, 76, 231));

    int n = static_cast<int>(hs.size());
    int bar_w = std::max(20, plot.width / (n * 2));
    auto palette = getPalette();

    // Eje izquierdo: % de cada holding (max 100)
    auto map_y_left = [&](double pct) {
        return plot.y + plot.height -
               static_cast<int>(plot.height * (pct / 100.0));
    };
    // Marcas eje Y izq
    for (int i = 0; i <= 5; ++i) {
        double v = i * 20.0;
        int yp = map_y_left(v);
        drawLabel(img, formatPct(v, 0), plot.x - 50, yp + 4, 0.45);
    }
    // Marcas eje Y der (acumulado)
    for (int i = 0; i <= 5; ++i) {
        double v = i * 20.0;
        int yp = map_y_left(v);
        drawLabel(img, formatPct(v, 0), plot.x + plot.width + 8, yp + 4, 0.45,
                  cv::Scalar(60, 76, 231));
    }

    // Barras + linea acumulada
    std::vector<cv::Point> cum_pts;
    double accum = 0;
    for (int i = 0; i < n; ++i) {
        double pct = (hs[i].value / total) * 100.0;
        accum += pct;
        int xc = plot.x + (plot.width * (2 * i + 1)) / (2 * n);
        int xb = xc - bar_w / 2;
        int yb = map_y_left(pct);
        int hb = plot.y + plot.height - yb;
        drawBar(img, xb, yb, bar_w, hb, palette[i % palette.size()]);
        drawLabel(img, formatPct(pct, 1), xb, yb - 6, 0.4, cv::Scalar(40, 40, 40));
        std::string nm = hs[i].name;
        if (nm.size() > 10) nm = nm.substr(0, 10) + "..";
        drawLabel(img, nm, xb - 4, plot.y + plot.height + 18, 0.4,
                  cv::Scalar(60, 60, 60));
        cum_pts.emplace_back(xc, map_y_left(accum));
    }
    // Linea acumulada
    for (size_t i = 1; i < cum_pts.size(); ++i)
        cv::line(img, cum_pts[i - 1], cum_pts[i], cv::Scalar(60, 76, 231), 2, cv::LINE_AA);
    for (auto& p : cum_pts)
        cv::circle(img, p, 5, cv::Scalar(60, 76, 231), cv::FILLED, cv::LINE_AA);

    // Linea umbral 80% (regla Pareto)
    int y80 = map_y_left(80.0);
    cv::line(img, cv::Point(plot.x, y80), cv::Point(plot.x + plot.width, y80),
             cv::Scalar(120, 120, 120), 1, cv::LINE_AA);
    drawLabel(img, "80%", plot.x + plot.width - 50, y80 - 5, 0.45,
              cv::Scalar(120, 120, 120));

    // Footer interpretativo
    std::string interp;
    if      (adv.hhi_categoria == "bajo")      interp = "Diversificacion saludable";
    else if (adv.hhi_categoria == "moderado")  interp = "Concentracion moderada - vigilar";
    else                                        interp = "Alta concentracion - riesgo";
    drawLabel(img, interp, 80, H - 20, 0.55,
              adv.hhi_categoria == "alto" ? colorNegative()
              : adv.hhi_categoria == "bajo" ? colorPositive() : colorWarning(), 1);

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// 4. BOXPLOT DE TASAS
// =============================================================
std::string GraphGenerator::generateBoxplotTasas(
    const std::vector<InstrumentoRentaFija>& cdts,
    const AnalysisResult& analysis,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 4. Boxplot Tasas...\n";
    int W = 700, H = 550;
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(255, 255, 255));
    drawTitle(img, "Boxplot - Distribucion de Tasas de Valoracion");
    drawSubtitle(img, "Cuartiles, mediana y outliers (k-sigma).");

    std::vector<double> v;
    for (const auto& c : cdts) v.push_back(c.tasa_valoracion);
    if (v.size() < 2) {
        drawLabel(img, "(No hay suficientes datos)", 50, H / 2, 0.7,
                  cv::Scalar(120, 120, 120));
        cv::imwrite(output_path, img);
        return output_path;
    }
    std::sort(v.begin(), v.end());

    double q1     = v[v.size() / 4];
    double median = v.size() % 2 == 0
                      ? (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2
                      : v[v.size() / 2];
    double q3     = v[3 * v.size() / 4];
    double iqr    = q3 - q1;
    double lo_w   = std::max(v.front(), q1 - 1.5 * iqr);
    double hi_w   = std::min(v.back(),  q3 + 1.5 * iqr);

    double mu     = analysis.tasas_valoracion.mean;
    double ic_lo  = analysis.avanzadas.ci95_lower;
    double ic_hi  = analysis.avanzadas.ci95_upper;

    cv::Rect plot = drawAxes(img, 130, 130, 90, 110);
    double ymin = std::min({lo_w, ic_lo}) - 0.5;
    double ymax = std::max({hi_w, ic_hi}) + 0.5;
    auto map_y = [&](double y) {
        return plot.y + plot.height -
               static_cast<int>(plot.height * (y - ymin) / (ymax - ymin));
    };
    // Marcas eje Y
    for (int i = 0; i <= 5; ++i) {
        double yv = ymin + (ymax - ymin) * i / 5;
        drawLabel(img, formatPct(yv, 2), plot.x - 70, map_y(yv) + 4, 0.45);
        cv::line(img, cv::Point(plot.x - 5, map_y(yv)), cv::Point(plot.x, map_y(yv)),
                 cv::Scalar(60, 60, 60), 1);
    }

    int box_x = plot.x + plot.width / 2 - 60;
    int box_w = 120;
    // Caja Q1-Q3
    cv::rectangle(img, cv::Point(box_x, map_y(q3)),
                       cv::Point(box_x + box_w, map_y(q1)),
                       colorAccent(), cv::FILLED);
    cv::rectangle(img, cv::Point(box_x, map_y(q3)),
                       cv::Point(box_x + box_w, map_y(q1)),
                       cv::Scalar(40, 40, 40), 2);
    // Mediana (linea horizontal blanca)
    cv::line(img, cv::Point(box_x, map_y(median)),
                  cv::Point(box_x + box_w, map_y(median)),
                  cv::Scalar(255, 255, 255), 3);
    // Whiskers
    int xc = box_x + box_w / 2;
    cv::line(img, cv::Point(xc, map_y(q3)), cv::Point(xc, map_y(hi_w)),
             cv::Scalar(40, 40, 40), 2);
    cv::line(img, cv::Point(xc, map_y(q1)), cv::Point(xc, map_y(lo_w)),
             cv::Scalar(40, 40, 40), 2);
    cv::line(img, cv::Point(box_x + 30, map_y(hi_w)),
                   cv::Point(box_x + box_w - 30, map_y(hi_w)),
                   cv::Scalar(40, 40, 40), 2);
    cv::line(img, cv::Point(box_x + 30, map_y(lo_w)),
                   cv::Point(box_x + box_w - 30, map_y(lo_w)),
                   cv::Scalar(40, 40, 40), 2);
    // Outliers
    for (double val : v) {
        if (val < lo_w || val > hi_w) {
            cv::circle(img, cv::Point(xc, map_y(val)), 6, colorNegative(),
                       cv::FILLED, cv::LINE_AA);
        }
    }
    // Linea de la media (verde punteada)
    int ym = map_y(mu);
    for (int x = box_x - 30; x < box_x + box_w + 30; x += 6)
        cv::line(img, cv::Point(x, ym), cv::Point(x + 3, ym), colorPositive(), 2);
    // IC 95% (banda gris)
    cv::rectangle(img, cv::Point(plot.x + 20, map_y(ic_hi)),
                       cv::Point(plot.x + plot.width - 20, map_y(ic_lo)),
                       cv::Scalar(220, 220, 220), 1);

    // Etiquetas en el lado derecho. Para evitar overlap, las apilamos
    // verticalmente segun su orden si estan demasiado juntas.
    int lbl_x = box_x + box_w + 20;
    struct LabelEntry { int y; std::string text; cv::Scalar color; };
    std::vector<LabelEntry> entries = {
        {map_y(q3),     "Q3 = "      + formatPct(q3, 2),     cv::Scalar(40, 40, 40)},
        {map_y(median), "Mediana = " + formatPct(median, 2), cv::Scalar(40, 40, 40)},
        {map_y(mu),     "Media = "   + formatPct(mu, 2),     colorPositive()},
        {map_y(q1),     "Q1 = "      + formatPct(q1, 2),     cv::Scalar(40, 40, 40)},
        {map_y(hi_w),   "Whisker max",                       cv::Scalar(120, 120, 120)},
        {map_y(lo_w),   "Whisker min",                       cv::Scalar(120, 120, 120)},
    };
    // Ordenar por Y y separar etiquetas que esten a < 18px de su vecina
    std::sort(entries.begin(), entries.end(),
              [](const LabelEntry& a, const LabelEntry& b) { return a.y < b.y; });
    for (size_t i = 1; i < entries.size(); ++i) {
        int min_gap = 18;
        if (entries[i].y - entries[i - 1].y < min_gap) {
            entries[i].y = entries[i - 1].y + min_gap;
        }
    }
    for (const auto& e : entries)
        drawLabel(img, e.text, lbl_x, e.y + 4, 0.45, e.color, 1);

    // Footer: estadisticos
    std::ostringstream oss;
    oss << "n=" << v.size() << "  IQR=" << std::fixed << std::setprecision(3) << iqr
        << "  CV=" << std::setprecision(3) << analysis.avanzadas.cv_tasas
        << "  Skew=" << std::setprecision(3) << analysis.avanzadas.skewness_tasas
        << "  Kurt=" << analysis.avanzadas.kurtosis_tasas;
    drawLabel(img, oss.str(), 60, H - 20, 0.5, cv::Scalar(60, 60, 60), 1);

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// 5. DRAWDOWN CHART
// =============================================================
std::string GraphGenerator::generateDrawdownChart(
    const std::vector<std::map<std::string, double>>& serie,
    const AdvancedMetrics& adv,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 5. Drawdown Chart...\n";
    int W = 1000, H = 600;
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(255, 255, 255));
    drawTitle(img, "Evolucion del Portafolio y Drawdown");
    std::ostringstream sub;
    sub << "Max Drawdown: " << std::fixed << std::setprecision(2) << adv.max_drawdown_pct
        << "%  |  Drawdown actual: " << adv.current_drawdown_pct << "%"
        << "  |  Crecimiento periodo: " << adv.crecimiento_periodo_pct << "%";
    drawSubtitle(img, sub.str());

    std::vector<double> vs;
    for (const auto& pt : serie) {
        auto it = pt.find("total_portafolio");
        if (it != pt.end() && it->second > 0) vs.push_back(it->second);
    }
    if (vs.size() < 2) {
        drawLabel(img, "(Serie temporal insuficiente)", 50, H / 2, 0.7,
                  cv::Scalar(120, 120, 120));
        cv::imwrite(output_path, img);
        return output_path;
    }

    // Calcular running max
    std::vector<double> peaks(vs.size());
    peaks[0] = vs[0];
    for (size_t i = 1; i < vs.size(); ++i)
        peaks[i] = std::max(peaks[i - 1], vs[i]);

    cv::Rect plot = drawAxes(img, 110, 60, 90, 90);
    double ymin = *std::min_element(vs.begin(), vs.end()) * 0.97;
    double ymax = *std::max_element(peaks.begin(), peaks.end()) * 1.03;
    auto map_x = [&](size_t i) {
        return plot.x + static_cast<int>(plot.width * static_cast<double>(i) /
                                          (vs.size() - 1));
    };
    auto map_y = [&](double v) {
        return plot.y + plot.height -
               static_cast<int>(plot.height * (v - ymin) / (ymax - ymin));
    };

    // Marcas eje Y
    for (int i = 0; i <= 5; ++i) {
        double yv = ymin + (ymax - ymin) * i / 5;
        drawLabel(img, formatMoney(yv), plot.x - 90, map_y(yv) + 4, 0.4);
    }
    // Marcas eje X
    int n = static_cast<int>(vs.size());
    int xticks = std::min(n, 6);
    for (int i = 0; i < xticks; ++i) {
        int idx = i * (n - 1) / (xticks - 1);
        int xp  = map_x(idx);
        drawLabel(img, "T" + std::to_string(idx), xp - 8, plot.y + plot.height + 18, 0.4);
    }

    // Area de drawdown (entre peak y valor) - rojo claro
    std::vector<cv::Point> region;
    for (size_t i = 0; i < vs.size(); ++i) region.emplace_back(map_x(i), map_y(peaks[i]));
    for (int i = static_cast<int>(vs.size()) - 1; i >= 0; --i)
        region.emplace_back(map_x(i), map_y(vs[i]));
    std::vector<std::vector<cv::Point>> regs = { region };
    cv::fillPoly(img, regs, cv::Scalar(180, 180, 250));

    // Linea de peaks (gris discontinua)
    for (size_t i = 1; i < vs.size(); ++i)
        cv::line(img, cv::Point(map_x(i - 1), map_y(peaks[i - 1])),
                       cv::Point(map_x(i),     map_y(peaks[i])),
                       cv::Scalar(150, 150, 150), 1, cv::LINE_AA);
    // Linea de valores (azul)
    for (size_t i = 1; i < vs.size(); ++i)
        cv::line(img, cv::Point(map_x(i - 1), map_y(vs[i - 1])),
                       cv::Point(map_x(i),     map_y(vs[i])),
                       colorAccent(), 2, cv::LINE_AA);
    // Puntos
    for (size_t i = 0; i < vs.size(); ++i)
        cv::circle(img, cv::Point(map_x(i), map_y(vs[i])), 4, colorAccent(),
                   cv::FILLED, cv::LINE_AA);

    drawLabel(img, "Area roja = drawdown (perdida desde maximo)",
              plot.x + 20, plot.y + 25, 0.5, cv::Scalar(80, 80, 80));

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// 6. DASHBOARD EJECUTIVO (KPIs)
// =============================================================
std::string GraphGenerator::generateDashboardKPIs(
    const ExtractoCompleto& extracto,
    const AnalysisResult&   analysis,
    const std::string& output_path) {

    std::cout << "[GraphGenerator] 6. Dashboard KPIs...\n";
    int W = 1100, H = 700;
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(248, 248, 250));
    drawTitle(img, "Dashboard Ejecutivo - Resumen Financiero");
    drawSubtitle(img, "Cliente: " + extracto.resumen.nombre_cliente +
                       "  |  Fecha: " + extracto.resumen.fecha_extracto);

    const auto& a = analysis.avanzadas;

    struct KPI {
        std::string label;
        std::string value;
        std::string sub;
        cv::Scalar color;
    };
    auto fmt_num = [](double v, int dec) {
        std::ostringstream o; o << std::fixed << std::setprecision(dec) << v; return o.str();
    };

    // Sharpe: si > 1.5 verde, [0, 1.5] amarillo, < 0 rojo
    cv::Scalar sharpe_color = a.sharpe_fic > 1.5 ? colorPositive()
                              : a.sharpe_fic >= 0  ? colorWarning()
                                                    : colorNegative();

    std::vector<KPI> kpis = {
        {"TOTAL PORTAFOLIO",       formatMoney(extracto.resumen.total_portafolio),
         "valor de mercado",       colorAccent()},
        {"YIELD PONDERADO",        formatPct(a.yield_ponderado_pct, 2),
         "TIR del portafolio",     colorPositive()},
        {"DURACION MODIFICADA",    fmt_num(a.duracion_modificada, 2) + " anos",
         "sensibilidad a tasas",   colorWarning()},
        {"HHI CONCENTRACION",      formatNumber(a.hhi),
         a.hhi_categoria,          a.hhi_categoria == "alto" ? colorNegative()
                                  : a.hhi_categoria == "bajo" ? colorPositive()
                                  : colorWarning()},
        {"TOP-3 EXPOSURE",         formatPct(a.top3_exposure_pct, 1),
         "concentrado en 3 holdings", colorNeutral()},
        {"SHARPE FIC",             fmt_num(a.sharpe_fic, 2),
         "rendimiento ajust. riesgo", sharpe_color},
        {"MAX DRAWDOWN",           formatPct(a.max_drawdown_pct, 2),
         "peor caida historica",   a.max_drawdown_pct < -5 ? colorNegative() : colorWarning()},
        {"RETORNO REAL",           formatPct(a.retorno_real_pct, 2),
         "ajustado por inflacion", a.retorno_real_pct > 0 ? colorPositive() : colorNegative()},
    };

    // Layout 4 columnas x 2 filas
    int cols = 4, rows = 2;
    int padding = 20;
    int card_w  = (W - padding * (cols + 1)) / cols;
    int card_h  = 130;
    int start_y = 90;

    for (size_t i = 0; i < kpis.size(); ++i) {
        int r = static_cast<int>(i) / cols;
        int c = static_cast<int>(i) % cols;
        int x = padding + c * (card_w + padding);
        int y = start_y + r * (card_h + padding);

        // Tarjeta
        cv::rectangle(img, cv::Point(x, y), cv::Point(x + card_w, y + card_h),
                      cv::Scalar(255, 255, 255), cv::FILLED);
        cv::rectangle(img, cv::Point(x, y), cv::Point(x + card_w, y + card_h),
                      cv::Scalar(220, 220, 220), 1);
        // Banda de color izq
        cv::rectangle(img, cv::Point(x, y), cv::Point(x + 6, y + card_h),
                      kpis[i].color, cv::FILLED);
        // Label arriba
        drawLabel(img, kpis[i].label, x + 18, y + 25, 0.45,
                  cv::Scalar(120, 120, 120), 1);
        // Valor grande
        cv::Size vts = cv::getTextSize(kpis[i].value, cv::FONT_HERSHEY_DUPLEX,
                                        1.0, 2, nullptr);
        int vx = x + card_w / 2 - vts.width / 2;
        cv::putText(img, kpis[i].value, cv::Point(vx, y + 75),
                    cv::FONT_HERSHEY_DUPLEX, 1.0, kpis[i].color, 2, cv::LINE_AA);
        // Sub
        cv::Size sts = cv::getTextSize(kpis[i].sub, cv::FONT_HERSHEY_SIMPLEX,
                                        0.42, 1, nullptr);
        int sx = x + card_w / 2 - sts.width / 2;
        drawLabel(img, kpis[i].sub, sx, y + 105, 0.42,
                  cv::Scalar(120, 120, 120), 1);
    }

    // Footer con configuracion usada
    std::ostringstream foot;
    foot << "Parametros: tasa libre = "
         << formatPct(a.config_usada.tasa_libre_riesgo * 100, 2)
         << "  |  inflacion = "
         << formatPct(a.config_usada.inflacion_anual * 100, 2)
         << "  |  benchmark = "
         << formatPct(a.config_usada.benchmark_rendto * 100, 2);
    drawLabel(img, foot.str(), 30, H - 30, 0.5, cv::Scalar(120, 120, 120));

    cv::imwrite(output_path, img);
    return output_path;
}

// =============================================================
// generateAll - llama a las 6 nuevas
// =============================================================
std::vector<std::string> GraphGenerator::generateAll(
    const ExtractoCompleto& extracto,
    const AnalysisResult&   analysis,
    const std::string& output_dir) {

    std::cout << "[GraphGenerator] Generando 6 graficas en: " << output_dir << std::endl;
    std::vector<std::string> paths;

    paths.push_back(generateYieldCurve(
        extracto.renta_fija,           output_dir + "/01_yield_curve.png"));
    paths.push_back(generateMaturityLadder(
        analysis.avanzadas,            output_dir + "/02_maturity_ladder.png"));
    paths.push_back(generateParetoConcentracion(
        extracto, analysis.avanzadas,  output_dir + "/03_pareto_concentracion.png"));
    paths.push_back(generateBoxplotTasas(
        extracto.renta_fija, analysis, output_dir + "/04_boxplot_tasas.png"));
    paths.push_back(generateDrawdownChart(
        analysis.serie_temporal, analysis.avanzadas,
        output_dir + "/05_drawdown.png"));
    paths.push_back(generateDashboardKPIs(
        extracto, analysis,            output_dir + "/06_dashboard_kpis.png"));

    std::cout << "[GraphGenerator] " << paths.size() << " graficas generadas\n";
    return paths;
}
