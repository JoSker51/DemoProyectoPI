#ifndef GRAPH_GENERATOR_H
#define GRAPH_GENERATOR_H

#include <string>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>
#include "data_structurer.h"
#include "statistical_analyzer.h"

// =============================================================
// GraphGenerator - genera 6 graficas financieras profesionales
// usando ONLY OpenCV. Cada grafica esta respaldada por una o mas
// metricas calculadas en StatisticalAnalyzer/AdvancedMetrics.
// =============================================================
class GraphGenerator {
public:
    // Genera todas las graficas y retorna las rutas de los PNGs
    std::vector<std::string> generateAll(
        const ExtractoCompleto& extracto,
        const AnalysisResult&   analysis,
        const std::string&      output_dir);

    // ===== Las 6 graficas nuevas =====

    // 1. Yield Curve: tasa de valoracion vs dias a vencimiento.
    //    Cada CDT es un punto. Linea de regresion lineal opcional.
    std::string generateYieldCurve(
        const std::vector<InstrumentoRentaFija>& cdts,
        const std::string& output_path);

    // 2. Maturity Ladder: monto $ en cada bucket de vencimiento.
    //    Apoya planeacion de liquidez.
    std::string generateMaturityLadder(
        const AdvancedMetrics& adv,
        const std::string& output_path);

    // 3. Pareto de Concentracion: barras (% por holding) ordenadas de mayor a
    //    menor + linea acumulada. Visualiza HHI.
    std::string generateParetoConcentracion(
        const ExtractoCompleto& extracto,
        const AdvancedMetrics&  adv,
        const std::string& output_path);

    // 4. Boxplot de tasas de valoracion: cuartiles, mediana, whiskers, outliers.
    std::string generateBoxplotTasas(
        const std::vector<InstrumentoRentaFija>& cdts,
        const AnalysisResult& analysis,
        const std::string& output_path);

    // 5. Drawdown chart: linea de portafolio + area roja bajo curva de maximos.
    std::string generateDrawdownChart(
        const std::vector<std::map<std::string, double>>& serie,
        const AdvancedMetrics& adv,
        const std::string& output_path);

    // 6. Dashboard ejecutivo: tabla render con los KPIs principales.
    //    Formato "one-pager para el jefe".
    std::string generateDashboardKPIs(
        const ExtractoCompleto& extracto,
        const AnalysisResult&   analysis,
        const std::string& output_path);

private:
    // ----- Utilidades de dibujo -----
    static std::string formatNumber(double value);
    static std::string formatMoney(double value);
    static std::string formatPct(double value, int decimals = 2);
    static void drawTitle(cv::Mat& img, const std::string& title);
    static void drawSubtitle(cv::Mat& img, const std::string& sub, int y = 60);
    static void drawBar(cv::Mat& img, int x, int y, int width, int height,
                        const cv::Scalar& color);
    static void drawLabel(cv::Mat& img, const std::string& text,
                          int x, int y, double scale = 0.5,
                          const cv::Scalar& color = cv::Scalar(0, 0, 0),
                          int thickness = 1);
    // Ejes basicos: marca origen, lineas X/Y, ticks. Devuelve area dibujable.
    static cv::Rect drawAxes(cv::Mat& img, int margin_left, int margin_right,
                              int margin_top, int margin_bottom);
    // Dibuja una linea de regresion lineal (least squares) sobre puntos (x, y)
    // dentro del rect plot. Devuelve true si la calculo.
    static bool drawRegressionLine(cv::Mat& img, const cv::Rect& plot,
                                    const std::vector<cv::Point2d>& pts_data,
                                    double xmin, double xmax,
                                    double ymin, double ymax,
                                    double& out_slope, double& out_intercept,
                                    double& out_r2);

    // Paleta de colores
    static std::vector<cv::Scalar> getPalette();
    // Todos los cv::Scalar son BGR (OpenCV nativo).
    static cv::Scalar colorPositive() { return cv::Scalar( 96, 174,  39); } // verde
    static cv::Scalar colorNegative() { return cv::Scalar( 60,  76, 231); } // rojo
    static cv::Scalar colorNeutral()  { return cv::Scalar(166, 165, 149); } // gris
    static cv::Scalar colorAccent()   { return cv::Scalar(185, 128,  41); } // azul
    static cv::Scalar colorWarning()  { return cv::Scalar( 18, 156, 243); } // naranja
};

#endif // GRAPH_GENERATOR_H
