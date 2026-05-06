#ifndef IMAGE_PREPROCESSOR_H
#define IMAGE_PREPROCESSOR_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

enum class BinarizeMethod {
    OTSU,
    ADAPTIVE,
    MANUAL,
    SAUVOLA      // mejor para iluminacion variable (foto celular)
};

// Reporte del preprocesamiento. Lo que el sistema "decidio" hacer y la
// calidad de la imagen detectada. Util para mostrar al usuario.
struct PreprocessReport {
    bool   document_detected     = false;
    double sharpness_score       = 0.0;     // varianza de Laplaciano; >100 = nitida
    double rotation_corrected_deg= 0.0;     // rotacion fina (Hough) aplicada
    int    coarse_rotation_deg   = 0;       // 0/90/180/270 aplicada antes del fine
    double shape_score           = 0.0;     // 0-1: que tan rectangular es el contorno
    int    original_width        = 0;
    int    original_height       = 0;
    int    final_width           = 0;
    int    final_height          = 0;
    std::vector<std::string> steps_applied;
    std::string warning;                     // si la calidad es baja
};

class ImagePreprocessor {
public:
    // ============================================================
    // PIPELINE PRINCIPAL: enhanceForOCR
    //
    // Mapea 1-a-1 al pipeline visto en clase:
    //   1. Binarizacion           (Otsu para deteccion de contornos)
    //   2. Deteccion de bordes    (Canny con thresholds dinamicos)
    //   3. Extraccion de contornos(findContours + filtrado por area)
    //   4. Convex Hull + Area     (cv::convexHull, mejora robustez)
    //   5. Clasificacion          (approxPolyDP -> rectangulo si 4 vertices y angulos ~90)
    //   6. Hough / Ajuste         (HoughLinesP para correccion de rotacion fina)
    //
    // Y los pasos de "post-rectificacion" para OCR:
    //   - Grayscale, CLAHE, median blur, upscale con cap de tamano
    //
    // Tambien agrega correccion de orientacion gruesa (0/90/180/270 grados)
    // antes de la rotacion fina, para fotos volteadas.
    // ============================================================
    cv::Mat enhanceForOCR(const cv::Mat& input, PreprocessReport* report = nullptr);

    // ----- Detector + clasificador de documento -----
    // Detecta el cuadrilatero del documento en la imagen. Usa:
    //   binarizacion -> bordes -> contornos -> convex hull -> clasificacion
    // Devuelve los 4 vertices ordenados [TL, TR, BR, BL] o vacio si no
    // se reconoce un rectangulo claro. shape_score (0-1) mide que tan
    // rectangular es el mejor contorno encontrado.
    std::vector<cv::Point2f> detectDocumentCorners(const cv::Mat& img,
                                                    double* shape_score = nullptr);

    // ----- Correccion de perspectiva -----
    cv::Mat correctPerspective(const cv::Mat& img,
                                const std::vector<cv::Point2f>& corners);

    // ----- Correccion de orientacion gruesa (0/90/180/270 grados) -----
    // Para fotos tomadas de costado o boca-abajo. Combina dos fuentes:
    //   1. Tesseract OSD (--psm 0) si esta disponible — autoritativo,
    //      distingue 0/90/180/270 incluso si las lineas horizontales son
    //      ambiguas.
    //   2. Heuristica Hough (mas lineas horizontales = orientacion correcta)
    //      como fallback si OSD no decide o falla.
    cv::Mat correctCoarseOrientation(const cv::Mat& img, int* applied_deg = nullptr);

    // Detecta orientacion via Tesseract OSD. Devuelve 0/90/180/270 (grados
    // de rotacion CW que hay que aplicar para enderezar) o -1 si OSD fallo
    // o no esta disponible.
    int detectOrientationOSD(const cv::Mat& img);

    // ----- Auto-rotacion fina con Hough -----
    cv::Mat autoRotateHough(const cv::Mat& img, double* out_angle = nullptr);

    // ----- Evaluacion de nitidez (varianza Laplaciano) -----
    double assessSharpness(const cv::Mat& img);

    // ----- Operaciones individuales -----
    cv::Mat toGrayscale(const cv::Mat& img);
    cv::Mat binarize(const cv::Mat& gray, BinarizeMethod method = BinarizeMethod::ADAPTIVE,
                     int manual_threshold = 127);
    cv::Mat denoise(const cv::Mat& img, int kernel_size = 3);
    cv::Mat enhanceContrast(const cv::Mat& gray, double clip_limit = 2.0,
                            cv::Size tile_size = cv::Size(8, 8));
    cv::Mat sharpenIfBlurry(const cv::Mat& img, double sharpness);
    cv::Mat deskew(const cv::Mat& img);

    // Backwards-compat
    cv::Mat fullPreprocess(const cv::Mat& img);

    // Debug: si se setea, guarda imagenes intermedias. Por default
    // el pipeline NO guarda nada (evita llenar disco / morir con
    // imagenes grandes).
    void setDebugDir(const std::string& dir) { debug_dir_ = dir; debug_counter_ = 0; }
    void clearDebugDir() { debug_dir_.clear(); }

    // Tope superior para evitar imagenes monstruo (e.g., 600x4000 → upscale
    // x2.5 → 1500x10000). Default 4000 px en el lado largo.
    void setMaxDimension(int px) { max_dim_ = px; }

private:
    std::string debug_dir_;
    int         debug_counter_ = 0;
    int         max_dim_       = 4000;       // cap de seguridad

    void saveStage(const cv::Mat& img, const std::string& name);

    // Sauvola (mejor que adaptive Gaussian para iluminacion irregular)
    cv::Mat sauvolaBinarize(const cv::Mat& gray, int window = 25, double k = 0.2);

    // Cuenta lineas casi horizontales (en grados) detectadas via Hough.
    // Lo usamos para la correccion de orientacion gruesa.
    int    countHorizontalLines(const cv::Mat& img);

    // Score 0-1 de "que tan rectangular es" un contorno aproximado a 4 vertices.
    // 1 = rectangulo perfecto (lados paralelos, angulos 90).
    double rectangularityScore(const std::vector<cv::Point2f>& quad);

    // Ordenar 4 puntos como [TL, TR, BR, BL]
    std::vector<cv::Point2f> sortCorners(const std::vector<cv::Point2f>& pts);
};

#endif // IMAGE_PREPROCESSOR_H
