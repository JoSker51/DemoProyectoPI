#include "image_preprocessor.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <array>
#include <regex>

namespace fs = std::filesystem;

// =============================================================
// HELPERS PRIVADOS
// =============================================================
void ImagePreprocessor::saveStage(const cv::Mat& img, const std::string& name) {
    if (debug_dir_.empty()) return;
    fs::create_directories(debug_dir_);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d_", debug_counter_++);
    std::string path = debug_dir_ + "/" + buf + name + ".png";
    cv::imwrite(path, img);
}

// Ordena 4 puntos en orden [TL, TR, BR, BL] usando suma/diferencia (truco CV)
std::vector<cv::Point2f> ImagePreprocessor::sortCorners(
    const std::vector<cv::Point2f>& pts) {

    std::vector<cv::Point2f> out(4);
    auto by_sum  = [](const cv::Point2f& a, const cv::Point2f& b) { return (a.x + a.y) < (b.x + b.y); };
    auto by_diff = [](const cv::Point2f& a, const cv::Point2f& b) { return (a.y - a.x) < (b.y - b.x); };
    out[0] = *std::min_element(pts.begin(), pts.end(), by_sum);   // TL
    out[2] = *std::max_element(pts.begin(), pts.end(), by_sum);   // BR
    out[1] = *std::min_element(pts.begin(), pts.end(), by_diff);  // TR
    out[3] = *std::max_element(pts.begin(), pts.end(), by_diff);  // BL
    return out;
}

// Score 0-1 de "que tan rectangular es" un quad (4 vertices ya ordenados).
// Mide cuanto se desvian los 4 angulos internos de 90 grados.
double ImagePreprocessor::rectangularityScore(const std::vector<cv::Point2f>& quad) {
    if (quad.size() != 4) return 0.0;
    double total_dev = 0;
    for (int i = 0; i < 4; ++i) {
        cv::Point2f a = quad[(i + 3) % 4];
        cv::Point2f b = quad[i];
        cv::Point2f c = quad[(i + 1) % 4];
        cv::Point2f v1 = a - b, v2 = c - b;
        double dot   = v1.x * v2.x + v1.y * v2.y;
        double n1    = std::sqrt(v1.x * v1.x + v1.y * v1.y);
        double n2    = std::sqrt(v2.x * v2.x + v2.y * v2.y);
        if (n1 < 1e-6 || n2 < 1e-6) return 0.0;
        double cos_a = dot / (n1 * n2);
        cos_a = std::max(-1.0, std::min(1.0, cos_a));
        double ang   = std::acos(cos_a) * 180.0 / CV_PI;
        total_dev   += std::abs(ang - 90.0);
    }
    // Penalizacion: 0 grados deviation -> score 1; 45+ -> score 0
    double score = 1.0 - std::min(total_dev / 180.0, 1.0);
    return score;
}

// =============================================================
// 1. EVALUACION DE NITIDEZ
// =============================================================
double ImagePreprocessor::assessSharpness(const cv::Mat& img) {
    cv::Mat gray = toGrayscale(img);
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    return sigma[0] * sigma[0];
}

// =============================================================
// DETECCION + CLASIFICACION DE DOCUMENTO
// Pipeline del slide:
//   binarizar (Otsu) -> bordes (Canny) -> contornos (findContours) ->
//   convex hull -> clasificacion (approxPolyDP a 4 vertices)
// =============================================================
std::vector<cv::Point2f> ImagePreprocessor::detectDocumentCorners(
    const cv::Mat& img, double* shape_score_out) {

    cv::Mat gray = toGrayscale(img);
    saveStage(gray, "doc_gray");

    // Suavizar para que Canny no detecte ruido como borde
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);

    // === 1. Binarizacion (Otsu) - util cuando el fondo contrasta con el doc ===
    cv::Mat bin;
    cv::threshold(blurred, bin, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
    saveStage(bin, "doc_otsu");

    // === 2. Deteccion de bordes (Canny con thresholds basados en mediana) ===
    cv::Mat edges;
    double med = 0.5 * 255;
    cv::Canny(blurred, edges, med * 0.66, med * 1.33);
    saveStage(edges, "doc_canny");

    // Dilatar para conectar bordes rotos (tipico en fotos)
    cv::Mat dilated;
    cv::dilate(edges, dilated, cv::Mat::ones(3, 3, CV_8U));

    // === 3. Extraccion de contornos ===
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(dilated, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    double img_area = static_cast<double>(img.cols) * img.rows;
    std::vector<cv::Point2f> best_quad;
    double best_score = 0.0;
    double best_area  = 0.0;

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < img_area * 0.20) continue;   // al menos 20% de la imagen

        // === 4. Convex Hull (mas robusto a contornos con muescas) ===
        std::vector<cv::Point> hull;
        cv::convexHull(c, hull);
        double hull_area = cv::contourArea(hull);
        if (hull_area < img_area * 0.20) continue;

        // === 5. Clasificacion: approxPolyDP a 4 vertices ===
        std::vector<cv::Point> approx;
        double peri = cv::arcLength(hull, true);
        cv::approxPolyDP(hull, approx, 0.02 * peri, true);

        // Si el hull no aproxima a 4 con eps=0.02, intentar con eps mas laxo
        if (approx.size() != 4) {
            cv::approxPolyDP(hull, approx, 0.05 * peri, true);
        }
        if (approx.size() != 4) continue;
        if (!cv::isContourConvex(approx)) continue;

        std::vector<cv::Point2f> quad;
        for (const auto& p : approx) quad.emplace_back(p.x, p.y);
        quad = sortCorners(quad);

        // Score de rectangularidad (angulos cercanos a 90)
        double rect_score = rectangularityScore(quad);
        double combined   = rect_score * (hull_area / img_area);
        if (combined > best_score) {
            best_score = combined;
            best_area  = hull_area;
            best_quad  = quad;
        }
    }

    if (!best_quad.empty()) {
        if (!debug_dir_.empty()) {
            cv::Mat vis = img.clone();
            std::vector<cv::Point> ip;
            for (auto& p : best_quad) ip.emplace_back(p.x, p.y);
            cv::polylines(vis, ip, true, cv::Scalar(0, 255, 0), 4);
            saveStage(vis, "doc_detected");
        }
        if (shape_score_out)
            *shape_score_out = rectangularityScore(best_quad);
        return best_quad;
    }

    if (shape_score_out) *shape_score_out = 0.0;
    return {};
}

// =============================================================
// CORRECCION DE PERSPECTIVA
// =============================================================
cv::Mat ImagePreprocessor::correctPerspective(
    const cv::Mat& img, const std::vector<cv::Point2f>& corners) {

    if (corners.size() != 4) return img.clone();

    auto dist = [](const cv::Point2f& a, const cv::Point2f& b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    };
    int W = static_cast<int>(std::max(dist(corners[0], corners[1]),
                                       dist(corners[3], corners[2])));
    int H = static_cast<int>(std::max(dist(corners[0], corners[3]),
                                       dist(corners[1], corners[2])));
    if (W < 50 || H < 50) return img.clone();

    std::vector<cv::Point2f> dst = {
        {0, 0}, {static_cast<float>(W - 1), 0},
        {static_cast<float>(W - 1), static_cast<float>(H - 1)},
        {0, static_cast<float>(H - 1)}
    };
    cv::Mat M = cv::getPerspectiveTransform(corners, dst);
    cv::Mat warped;
    cv::warpPerspective(img, warped, M, cv::Size(W, H), cv::INTER_CUBIC);
    saveStage(warped, "doc_warped");
    return warped;
}

// =============================================================
// CORRECCION DE ORIENTACION GRUESA (0/90/180/270 grados)
// Heuristica: la orientacion correcta es la que tiene MAS lineas
// horizontales detectadas por Hough. (En texto, las lineas de baseline
// son horizontales; cuando volteas la foto 90 grados, esas lineas pasan
// a ser verticales y dejan de contarse como "horizontales".)
// =============================================================
int ImagePreprocessor::countHorizontalLines(const cv::Mat& img) {
    cv::Mat gray = toGrayscale(img);
    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);
    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 80,
                     img.cols * 0.15, 20);
    int count = 0;
    for (const auto& l : lines) {
        double dx = l[2] - l[0];
        double dy = l[3] - l[1];
        double ang = std::atan2(dy, dx) * 180.0 / CV_PI;
        while (ang >  90) ang -= 180;
        while (ang < -90) ang += 180;
        if (std::abs(ang) < 20) count++;        // casi horizontal
    }
    return count;
}

// =============================================================
// Detector de orientacion via Tesseract OSD (--psm 0).
// Output esperado:
//   Page number: 0
//   Orientation in degrees: 90
//   Rotate: 270
//   Orientation confidence: 4.21
//   Script: Latin
//   Script confidence: 1.49
// El campo "Rotate: N" indica los grados CW que hay que aplicar para
// enderezar la imagen. N en {0, 90, 180, 270}.
// Devuelve N o -1 si OSD fallo (e.g. osd.traineddata no instalado).
// =============================================================
int ImagePreprocessor::detectOrientationOSD(const cv::Mat& img) {
#ifdef _WIN32
    #define POPEN_FN  _popen
    #define PCLOSE_FN _pclose
#else
    #define POPEN_FN  popen
    #define PCLOSE_FN pclose
#endif
    // Guardar imagen temporal
    std::string tmp = "output/images/temp_ocr/osd_probe.png";
    fs::create_directories("output/images/temp_ocr");
    cv::imwrite(tmp, img);
    std::string cmd = "tesseract \"" + tmp + "\" stdout --psm 0 2>&1";
    std::array<char, 4096> buf;
    std::string out;
    FILE* p = POPEN_FN(cmd.c_str(), "r");
    if (!p) { fs::remove(tmp); return -1; }
    while (fgets(buf.data(), static_cast<int>(buf.size()), p) != nullptr)
        out += buf.data();
    PCLOSE_FN(p);
    fs::remove(tmp);

    // Parsear "Rotate: N"
    static const std::regex re(R"(Rotate:\s*(\d+))");
    std::smatch m;
    if (std::regex_search(out, m, re)) {
        int deg = std::stoi(m[1]);
        // Tambien parsear confianza
        static const std::regex re_conf(R"(Orientation confidence:\s*([\d\.]+))");
        std::smatch cm;
        double conf = 0;
        if (std::regex_search(out, cm, re_conf)) conf = std::stod(cm[1]);
        std::cout << "[ImagePreprocessor] OSD: rotate=" << deg
                  << "° confidence=" << conf << "\n";
        // Threshold: 0.5 es buen compromiso — fotos con fondo texturado
        // (madera, mesa) dan confianza menor que escaneos limpios pero
        // aun asi es señal valida si OSD distingue una orientacion clara.
        if (conf < 0.5) return -1;
        return deg;
    }
    if (out.find("osd.traineddata") != std::string::npos ||
        out.find("Could not initialize") != std::string::npos) {
        std::cout << "[ImagePreprocessor] OSD no disponible "
                     "(falta osd.traineddata).\n";
    }
    return -1;
}

cv::Mat ImagePreprocessor::correctCoarseOrientation(const cv::Mat& img,
                                                     int* applied_deg) {
    // 1) Intentar Tesseract OSD primero — autoritativo
    int osd_deg = detectOrientationOSD(img);
    if (osd_deg >= 0 && osd_deg != 0) {
        cv::Mat out;
        if (osd_deg == 90)
            cv::rotate(img, out, cv::ROTATE_90_CLOCKWISE);
        else if (osd_deg == 180)
            cv::rotate(img, out, cv::ROTATE_180);
        else if (osd_deg == 270)
            cv::rotate(img, out, cv::ROTATE_90_COUNTERCLOCKWISE);
        else
            out = img.clone();
        if (applied_deg) *applied_deg = osd_deg;
        saveStage(out, "doc_coarse_rotated_osd");
        std::cout << "[ImagePreprocessor] Orientacion gruesa por OSD: "
                  << osd_deg << "°\n";
        return out;
    }
    if (osd_deg == 0) {
        // OSD dijo que ya esta bien orientada
        if (applied_deg) *applied_deg = 0;
        return img.clone();
    }

    // 2) Fallback: heuristica Hough (solo distingue 90/270 vs 0/180)
    int best_rot = 0;
    int orig_count = countHorizontalLines(img);
    int best_count = orig_count;

    // Solo rotamos si la mejora es SIGNIFICATIVA (>= 50% mas lineas
    // horizontales). Esto evita falsos positivos en imagenes ya
    // correctamente orientadas (e.g. PDFs renderizados), donde una
    // rotacion 180° no cambia el conteo materialmente pero la heuristica
    // ingenua podria flipearla por ruido. Para fotos de celular giradas
    // 90/270° la diferencia es enorme (el texto pasa de horizontal a
    // vertical) y la rotacion sí se aplica.
    const double MIN_GAIN = 1.5;
    for (int rot : {90, 180, 270}) {
        cv::Mat rotated;
        if (rot == 90)       cv::rotate(img, rotated, cv::ROTATE_90_CLOCKWISE);
        else if (rot == 180) cv::rotate(img, rotated, cv::ROTATE_180);
        else                 cv::rotate(img, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
        int c = countHorizontalLines(rotated);
        if (c > best_count && c > orig_count * MIN_GAIN) {
            best_count = c;
            best_rot   = rot;
        }
    }

    if (applied_deg) *applied_deg = best_rot;
    if (best_rot == 0) return img.clone();

    cv::Mat out;
    if (best_rot == 90)       cv::rotate(img, out, cv::ROTATE_90_CLOCKWISE);
    else if (best_rot == 180) cv::rotate(img, out, cv::ROTATE_180);
    else                      cv::rotate(img, out, cv::ROTATE_90_COUNTERCLOCKWISE);
    saveStage(out, "doc_coarse_rotated");
    std::cout << "[ImagePreprocessor] Orientacion gruesa corregida: "
              << best_rot << " grados\n";
    return out;
}

// =============================================================
// HOUGH / AJUSTE FINO DE ROTACION
// =============================================================
cv::Mat ImagePreprocessor::autoRotateHough(const cv::Mat& img, double* out_angle) {
    cv::Mat gray = toGrayscale(img);
    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180,
                     100, img.cols * 0.3, 20);

    if (lines.empty()) {
        if (out_angle) *out_angle = 0;
        return img.clone();
    }

    std::vector<double> angles;
    for (const auto& l : lines) {
        double dx = l[2] - l[0];
        double dy = l[3] - l[1];
        double ang = std::atan2(dy, dx) * 180.0 / CV_PI;
        while (ang >  45) ang -= 90;
        while (ang < -45) ang += 90;
        if (std::abs(ang) < 30) angles.push_back(ang);
    }
    if (angles.empty()) {
        if (out_angle) *out_angle = 0;
        return img.clone();
    }

    std::sort(angles.begin(), angles.end());
    double median_angle = angles[angles.size() / 2];

    if (out_angle) *out_angle = median_angle;
    if (std::abs(median_angle) < 0.2) return img.clone();

    std::cout << "[ImagePreprocessor] Rotacion fina: " << median_angle << " grados\n";

    cv::Point2f center(img.cols / 2.0f, img.rows / 2.0f);
    cv::Mat M = cv::getRotationMatrix2D(center, median_angle, 1.0);
    cv::Rect2f bbox = cv::RotatedRect(center, img.size(), median_angle).boundingRect2f();
    M.at<double>(0, 2) += bbox.width  / 2.0 - center.x;
    M.at<double>(1, 2) += bbox.height / 2.0 - center.y;

    cv::Mat rotated;
    cv::warpAffine(img, rotated, M, bbox.size(), cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    saveStage(rotated, "doc_fine_rotated");
    return rotated;
}

// =============================================================
// SAUVOLA BINARIZATION
// =============================================================
cv::Mat ImagePreprocessor::sauvolaBinarize(const cv::Mat& gray, int window, double k) {
    cv::Mat input = gray;
    if (input.channels() > 1) input = toGrayscale(input);
    input.convertTo(input, CV_32F);

    cv::Mat mean, sqmean;
    cv::boxFilter(input, mean, CV_32F, cv::Size(window, window));
    cv::boxFilter(input.mul(input), sqmean, CV_32F, cv::Size(window, window));
    cv::Mat variance = sqmean - mean.mul(mean);
    cv::Mat stddev;
    cv::sqrt(cv::max(variance, 0), stddev);

    double R = 128.0;
    cv::Mat threshold_map = mean.mul(1.0 + k * (stddev / R - 1.0));

    // Vectorizado en lugar de pixel-por-pixel
    cv::Mat binary;
    cv::compare(input, threshold_map, binary, cv::CMP_GT);
    return binary;
}

// =============================================================
// SHARPEN si la imagen viene borrosa (unsharp mask)
// =============================================================
cv::Mat ImagePreprocessor::sharpenIfBlurry(const cv::Mat& img, double sharpness) {
    if (sharpness >= 100.0) return img.clone();      // ya nitida
    cv::Mat blurred, sharpened;
    cv::GaussianBlur(img, blurred, cv::Size(0, 0), 3);
    cv::addWeighted(img, 1.5, blurred, -0.5, 0, sharpened);
    saveStage(sharpened, "step_sharpen");
    return sharpened;
}

// =============================================================
// PIPELINE PRINCIPAL: enhanceForOCR
// =============================================================
cv::Mat ImagePreprocessor::enhanceForOCR(const cv::Mat& input, PreprocessReport* report) {
    PreprocessReport local;
    PreprocessReport& r = report ? *report : local;

    r.original_width  = input.cols;
    r.original_height = input.rows;
    saveStage(input, "00_input");

    // --- 0. Evaluacion de nitidez ---
    r.sharpness_score = assessSharpness(input);
    r.steps_applied.push_back("sharpness=" + std::to_string(r.sharpness_score));
    if (r.sharpness_score < 50) {
        r.warning = "Imagen borrosa (varianza Laplaciano = " +
                    std::to_string(r.sharpness_score) +
                    "). El OCR puede fallar; sugerir re-foto.";
        std::cerr << "[ImagePreprocessor] WARNING: " << r.warning << "\n";
    }

    cv::Mat working = input.clone();

    // --- 1. Orientacion gruesa (0/90/180/270) ---
    int coarse = 0;
    working = correctCoarseOrientation(working, &coarse);
    r.coarse_rotation_deg = coarse;
    if (coarse != 0)
        r.steps_applied.push_back("coarse_rot_" + std::to_string(coarse));

    // --- 2-5. Binarizacion -> Bordes -> Contornos -> Convex Hull -> Clasificacion ---
    double shape_score = 0;
    auto corners = detectDocumentCorners(working, &shape_score);
    r.shape_score = shape_score;

    if (!corners.empty() && shape_score > 0.6) {
        r.document_detected = true;
        r.steps_applied.push_back("perspective_corrected");
        working = correctPerspective(working, corners);
        std::cout << "[ImagePreprocessor] Doc detectado y rectificado: "
                  << working.cols << "x" << working.rows
                  << " (rect_score=" << shape_score << ")\n";
    } else if (!corners.empty()) {
        std::cout << "[ImagePreprocessor] Cuadrilatero detectado pero poco rectangular ("
                  << shape_score << "). No se rectifica.\n";
    } else {
        std::cout << "[ImagePreprocessor] No se detecto documento; usando imagen completa.\n";
    }

    // --- 6. Hough / Ajuste fino de rotacion ---
    double angle = 0;
    working = autoRotateHough(working, &angle);
    r.rotation_corrected_deg = angle;
    if (std::abs(angle) > 0.2)
        r.steps_applied.push_back("fine_rot_" + std::to_string(angle));

    // --- Post: grayscale + sharpen si borroso + CLAHE + denoise ---
    cv::Mat gray = toGrayscale(working);
    saveStage(gray, "step_grayscale");
    r.steps_applied.push_back("grayscale");

    cv::Mat sharp = sharpenIfBlurry(gray, r.sharpness_score);
    if (r.sharpness_score < 100.0) r.steps_applied.push_back("unsharp_mask");

    cv::Mat enhanced = enhanceContrast(sharp);
    saveStage(enhanced, "step_clahe");
    r.steps_applied.push_back("clahe");

    cv::Mat denoised = denoise(enhanced, 3);
    saveStage(denoised, "step_denoised");
    r.steps_applied.push_back("median_blur");

    // --- Pipeline adaptativo: para imagenes chicas, upscaling mas agresivo ---
    // Imagenes razonables (min_side >= 1500): target 1500 (conservador).
    // Imagenes chicas (min_side < 1500): target 2500 para ayudar al OCR.
    int pre_min_side = std::min(denoised.cols, denoised.rows);
    int min_side_target = (pre_min_side < 1500) ? 2500 : 1500;
    int min_side = pre_min_side;
    int max_side = std::max(denoised.cols, denoised.rows);
    if (min_side < min_side_target) {
        double scale = static_cast<double>(min_side_target) / min_side;
        // Cap: que el lado mas largo no supere max_dim_
        if (max_side * scale > max_dim_) {
            scale = static_cast<double>(max_dim_) / max_side;
            std::cout << "[ImagePreprocessor] Upscale capado a max_dim=" << max_dim_ << "\n";
        }
        if (scale > 1.01) {
            cv::Mat upscaled;
            // Lanczos preserva mejor los bordes del texto que cubic.
            cv::resize(denoised, upscaled, cv::Size(), scale, scale, cv::INTER_LANCZOS4);
            // Tras un upscale fuerte, los bordes del texto quedan suaves.
            // Aplicamos unsharp mask para recuperar nitidez (esto es
            // critico para que Tesseract pueda leer el texto upscaled).
            cv::Mat blurred, sharpened;
            cv::GaussianBlur(upscaled, blurred, cv::Size(0, 0), 1.0);
            cv::addWeighted(upscaled, 1.6, blurred, -0.6, 0, sharpened);
            denoised = sharpened;
            saveStage(denoised, "step_upscaled");
            r.steps_applied.push_back("upscaled_lanczos_x" + std::to_string(scale));
        }
    } else if (max_side > max_dim_) {
        // Imagen ya muy grande: bajarla a max_dim_ por el lado largo
        double scale = static_cast<double>(max_dim_) / max_side;
        cv::Mat shrunk;
        cv::resize(denoised, shrunk, cv::Size(), scale, scale, cv::INTER_AREA);
        denoised = shrunk;
        saveStage(denoised, "step_downscaled");
        r.steps_applied.push_back("downscaled_x" + std::to_string(scale));
    }

    // --- Binarizacion Sauvola SOLO para imagenes pequenas/dificiles ---
    // En imagenes grandes y limpias (PDFs renderizados a 300 DPI),
    // Tesseract funciona mejor con grayscale (preserva anti-aliasing).
    // Para imagenes pequenas o con artefactos (screenshots, fotos), Sauvola
    // ayuda a separar texto del fondo. Threshold: si la fuente era chica,
    // binarizamos.
    if (pre_min_side < 1500) {
        cv::Mat binary = sauvolaBinarize(denoised, 25, 0.2);
        saveStage(binary, "step_binary");
        r.steps_applied.push_back("sauvola_binarize");
        denoised = binary;
    }

    r.final_width  = denoised.cols;
    r.final_height = denoised.rows;
    saveStage(denoised, "99_final");

    return denoised;
}

// =============================================================
// API ANTERIOR
// =============================================================
cv::Mat ImagePreprocessor::toGrayscale(const cv::Mat& img) {
    if (img.channels() == 1) return img.clone();
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat ImagePreprocessor::binarize(const cv::Mat& gray, BinarizeMethod method,
                                     int manual_threshold) {
    cv::Mat binary;
    cv::Mat input = gray;
    if (input.channels() > 1) input = toGrayscale(input);

    switch (method) {
        case BinarizeMethod::OTSU:
            cv::threshold(input, binary, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
            break;
        case BinarizeMethod::ADAPTIVE:
            cv::adaptiveThreshold(input, binary, 255,
                                   cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                   cv::THRESH_BINARY, 15, 10);
            break;
        case BinarizeMethod::MANUAL:
            cv::threshold(input, binary, manual_threshold, 255, cv::THRESH_BINARY);
            break;
        case BinarizeMethod::SAUVOLA:
            binary = sauvolaBinarize(input);
            break;
    }
    return binary;
}

cv::Mat ImagePreprocessor::denoise(const cv::Mat& img, int kernel_size) {
    if (kernel_size % 2 == 0) kernel_size++;
    cv::Mat denoised;
    cv::medianBlur(img, denoised, kernel_size);
    return denoised;
}

cv::Mat ImagePreprocessor::enhanceContrast(const cv::Mat& gray, double clip_limit,
                                            cv::Size tile_size) {
    cv::Mat input = gray;
    if (input.channels() > 1) input = toGrayscale(input);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clip_limit, tile_size);
    cv::Mat enhanced;
    clahe->apply(input, enhanced);
    return enhanced;
}

cv::Mat ImagePreprocessor::deskew(const cv::Mat& img) {
    return autoRotateHough(img);
}

cv::Mat ImagePreprocessor::fullPreprocess(const cv::Mat& img) {
    PreprocessReport rep;
    cv::Mat enhanced = enhanceForOCR(img, &rep);
    std::cout << "[ImagePreprocessor] Pipeline aplico: ";
    for (const auto& s : rep.steps_applied) std::cout << s << " ";
    std::cout << "\n";
    if (!rep.warning.empty())
        std::cout << "[ImagePreprocessor] " << rep.warning << "\n";
    return enhanced;
}
