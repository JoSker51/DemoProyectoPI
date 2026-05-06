#ifndef EXTRACTO_LOADER_H
#define EXTRACTO_LOADER_H

#include <string>
#include <vector>
#include "data_structurer.h"

// Carga un ExtractoCompleto desde un archivo JSON que cumple
// schema/extracto_v1.schema.json
//
// Diseno: el JSON unificado es el contrato entre el extractor (que puede
// usar IA local) y el analizador (que solo usa formulas matematicas).
// Esta clase solo hace mapeo y validacion estructural ligera; la validacion
// formal contra el schema se delega a una herramienta externa (jsonschema CLI).
class ExtractoLoader {
public:
    // Carga el extracto desde un archivo JSON. Devuelve true si fue exitoso.
    // En caso de error, llena getErrors() con la lista de problemas encontrados.
    bool loadFromFile(const std::string& json_path, ExtractoCompleto& out);

    // Carga desde un string JSON ya en memoria.
    bool loadFromString(const std::string& json_text, ExtractoCompleto& out);

    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }

private:
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
};

#endif // EXTRACTO_LOADER_H
