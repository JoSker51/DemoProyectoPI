#ifndef STANDARD_PARSER_H
#define STANDARD_PARSER_H

#include <vector>
#include <string>
#include "ocr_extractor.h"
#include "data_structurer.h"

// =============================================================================
// StandardParser
//
// Parser para el "Template Estandar v1" que produce el generador en
// tools/generate_test_extracts.py. Layout fijo y conocido:
//
//   1. Header con titulo "EXTRACTO DE INVERSIONES" y "Periodo: ..."
//   2. Bloque cliente (Cliente / NIT/CC / Direccion / Ciudad / Asesor)
//   3. Tabla "RESUMEN DEL PORTAFOLIO"
//   4. Tabla "RENTA FIJA - DETALLE"
//   5. Bloques "FONDO DE INVERSION: ..." (uno por fondo)
//   6. Tabla "SALDOS EN EFECTIVO"
//
// La deteccion del template es por la presencia de "EXTRACTO DE INVERSIONES"
// en el OCR. Si se detecta, parseAll() llena ExtractoCompleto con todo lo
// extraible y devuelve true. Si no, devuelve false y el caller cae al
// pipeline antiguo (A&V).
// =============================================================================
class StandardParser {
public:
    // True si el OCR contiene el marcador del template estandar.
    static bool detectStandardTemplate(const std::vector<OCRResult>& ocr_data);

    // Llena ExtractoCompleto a partir del OCR de UNA sola pagina (los
    // PDFs del template son single-page). Devuelve true si se reconocio
    // y se parseo el template.
    static bool parseAll(const std::vector<OCRResult>& ocr_data,
                          ExtractoCompleto& out);
};

#endif // STANDARD_PARSER_H
