#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <wx/wx.h>
#include <wx/grid.h>
#include <wx/gauge.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/statbmp.h>
#include <wx/filedlg.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include "data_structurer.h"
#include "statistical_analyzer.h"

class InvestmentApp : public wxApp {
public:
    bool OnInit() override;
};

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    wxButton* btn_seleccionar_pdf_;
    wxButton* btn_procesar_;
    wxButton* btn_exportar_csv_;
    wxGauge* progress_bar_;
    wxTextCtrl* txt_log_;
    wxStaticText* lbl_status_;
    wxNotebook* notebook_;
    wxGrid* grid_resumen_;
    wxGrid* grid_renta_fija_;
    wxGrid* grid_fondos_;
    wxListBox* list_graficas_;
    wxStaticBitmap* panel_grafica_;
    wxScrolledWindow* scroll_grafica_;
    wxGrid* grid_analisis_;
    wxStaticBitmap* panel_preview_;
    wxScrolledWindow* scroll_preview_;
    wxStaticText* lbl_preview_info_;

    std::string current_pdf_path_;
    std::string current_pdf_password_;
    ExtractoCompleto current_extracto_;
    AnalysisResult current_analysis_;
    std::vector<std::string> graph_paths_;
    std::atomic<bool> processing_;

    void createMenuBar();
    void createLayout();
    wxPanel* createResumenTab(wxNotebook* parent);
    wxPanel* createRentaFijaTab(wxNotebook* parent);
    wxPanel* createFondosTab(wxNotebook* parent);
    wxPanel* createGraficasTab(wxNotebook* parent);
    wxPanel* createAnalisisTab(wxNotebook* parent);
    wxPanel* createPreviewTab(wxNotebook* parent);
    void showPreview(const std::string& image_path);

    static bool isImageFile(const std::string& path);
    static bool isPdfFile(const std::string& path);

    void OnSelectPDF(wxCommandEvent& event);
    void OnProcess(wxCommandEvent& event);
    void OnExportCSV(wxCommandEvent& event);
    void OnSelectGraph(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnProcessComplete(wxCommandEvent& event);

    void processExtracto();
    void clearResultsDisplay();    // Limpia todas las pestanas / widgets de resultado
    void updateResumenDisplay(const ExtractoCompleto& extracto);
    void updateRentaFijaDisplay(const ExtractoCompleto& extracto);
    void updateFondosDisplay(const ExtractoCompleto& extracto);
    void updateAnalisisDisplay(const AnalysisResult& analysis);
    void showGraph(const std::string& image_path);
    void setProgress(int percentage);
    void logMessage(const std::string& msg);

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_SELECT_PDF = wxID_HIGHEST + 1,
    ID_PROCESS,
    ID_EXPORT_CSV,
    ID_SELECT_GRAPH,
    ID_PROCESS_COMPLETE
};

#endif // UI_MANAGER_H
