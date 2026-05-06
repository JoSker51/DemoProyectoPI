#ifndef OCR_EXTRACTOR_H
#define OCR_EXTRACTOR_H

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "table_detector.h"

// Resultado individual del OCR para una region de imagen
struct OCRResult {
    std::string text;       // Texto reconocido
    double confidence;      // Nivel de confianza (0.0 a 1.0)
    cv::Rect bbox;          // Bounding box dentro de la imagen procesada
};

// Extractor de texto basado en Tesseract OCR (invocado via CLI).
// No usa redes neuronales propietarias ni servidores externos: solo el
// motor OCR clasico de Tesseract, llamado como subproceso por imagen.
//
// Requisito: tener `tesseract` en el PATH y los traineddata de espanol
// instalados (paquete tesseract-ocr-spa en Linux, o tessdata/spa.traineddata
// en Windows). Verificable con `tesseract --list-langs`.
class OCRExtractor {
public:
    OCRExtractor();
    ~OCRExtractor() = default;

    OCRExtractor(const OCRExtractor&) = delete;
    OCRExtractor& operator=(const OCRExtractor&) = delete;

    // Extraccion sobre la imagen completa: una llamada a Tesseract
    std::vector<OCRResult> extractAll(const cv::Mat& image);

    // Extrae texto de una region (recorte + Tesseract)
    std::vector<OCRResult> extractFromRegion(const cv::Mat& region);

    // Extrae texto de una sola celda (concatenado en un solo OCRResult)
    OCRResult extractFromCell(const cv::Mat& cell_img);

    // Extrae texto de un grid de tabla. Optimizacion: hace UNA sola llamada
    // a Tesseract sobre la imagen completa y asigna palabras a celdas por
    // interseccion de bbox.
    std::vector<std::vector<OCRResult>> extractFromGrid(
        const cv::Mat& img, const TableGrid& grid);

    // Verifica que `tesseract` este disponible en el PATH y que el idioma
    // requerido este instalado. Reemplaza el viejo startServer().
    bool startServer();   // Retrocompat: equivale a checkAvailable()
    void stopServer() {}  // Retrocompat: no-op (no hay proceso persistente)
    bool isServerRunning() const { return available_; }

    // Verifica si tesseract esta instalado y con el idioma deseado
    static bool isTesseractAvailable();
    static bool hasLanguage(const std::string& lang);

    // Configuracion
    void setLanguage(const std::string& lang) { lang_ = lang; }
    void setPSM(int psm) { psm_ = psm; }   // Page Segmentation Mode (default 6)
    void setOEM(int oem) { oem_ = oem; }   // OCR Engine Mode (default 3)

private:
    // Ejecuta tesseract sobre un archivo y devuelve el TSV crudo
    std::string runTesseractTSV(const std::string& image_path);

    // Parsea TSV de tesseract a OCRResults (uno por linea de texto)
    std::vector<OCRResult> parseTSV(const std::string& tsv);

    // Guarda imagen temporal y devuelve el path
    std::string saveTempImage(const cv::Mat& img, const std::string& prefix = "ocr");

    std::string temp_dir_;
    int         temp_counter_ = 0;
    std::string lang_         = "spa";
    int         psm_          = 6;   // Assume single uniform block of text
    int         oem_          = 3;   // Default (LSTM + legacy si disponible)
    bool        available_    = false;
};

#endif // OCR_EXTRACTOR_H
