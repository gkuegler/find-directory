#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <regex>

// wxWidgets is full of strcpy
#pragma warning(push)
#pragma warning(disable : 4996)
#include <wx/listctrl.h>
#include <wx/wx.h>
#pragma warning(pop)

const constexpr int appwidth = 500;
const constexpr int appheight = 800;

wxPoint GetOrigin(const int w, const int h) {
  int desktopWidth = GetSystemMetrics(SM_CXMAXIMIZED);
  int desktopHeight = GetSystemMetrics(SM_CYMAXIMIZED);
  return wxPoint((desktopWidth / 2) - (w / 2), (desktopHeight / 2) - (h / 2));
}

class cFrame : public wxFrame {
  wxTextCtrl* directory_entry;
  wxTextCtrl* regex_entry;
  wxCheckBox* recursive_checkbox;
  wxListView* search_results;

 public:
  cFrame()
      : wxFrame(nullptr, wxID_ANY, "Find Directory With Regex",
                GetOrigin(appwidth, appheight), wxSize(appwidth, appheight)) {
    // clang-format off
    auto panel = new wxPanel(this);  // main panel for layout
    auto regex_entry_label = new wxStaticText(panel, wxID_ANY, "pattern:");
    auto directory_entry_label = new wxStaticText(panel, wxID_ANY, "directory:");
    regex_entry = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    directory_entry = new wxTextCtrl(panel, wxID_ANY, "L:\\", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    recursive_checkbox = new wxCheckBox(panel, wxID_ANY, "recursively search child directories");
    search_results = new wxListView(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_LIST);
    auto search_button = new wxButton(panel, wxID_ANY, "Search");

    // Layout Controls
    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(regex_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
    top->Add(regex_entry, 0, wxEXPAND | wxALL, 5);
    top->Add(directory_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
    top->Add(directory_entry, 0, wxEXPAND | wxALL, 5);
    top->Add(recursive_checkbox, 0, wxEXPAND | wxALL, 5);
    top->Add(search_button, 0, wxEXPAND | wxALL, 5);
    top->Add(search_results, 1, wxEXPAND | wxALL, 5);
    panel->SetSizer(top);

    // Bind Events
    search_button->Bind(wxEVT_BUTTON, &cFrame::OnSearch, this);
    regex_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
    directory_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
    recursive_checkbox->Bind(wxEVT_CHECKBOX, &cFrame::OnRecursive, this);
    search_results->Bind(wxEVT_LIST_ITEM_SELECTED, &cFrame::OnItem, this);
    // clang-format on
  }
  void OnSearch(wxCommandEvent& event) {
    search_results->DeleteAllItems();
    spdlog::trace("on search is entering");
    auto text = std::string(regex_entry->GetLineText(0).mb_str());
    auto path = std::string(directory_entry->GetLineText(0).mb_str());

    try {
      int i = 0;
      for (auto const& entry : std::filesystem::directory_iterator{path}) {
        std::string path = entry.path().generic_string();
        std::regex r(text, std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(path, m, r)) {
          spdlog::debug("path found: {}", path);
          search_results->InsertItem(i++, path);
        }
      }
    } catch (std::filesystem::filesystem_error& e) {
      wxLogError("%s", e.what());
    } catch (std::regex_error& e) {
      wxLogError("%s", e.what());
    }
    spdlog::trace("on search is exiting");
  }
  void OnRecursive(wxCommandEvent& event) {
    wxLogError("functionality not implemented yet");
    recursive_checkbox->SetValue(false);
  }
  void OnItem(wxListEvent& event) {
    // get path from list box selection
    auto path = std::string(event.GetItem().GetText().mb_str());
    // test string
    // std::string path = "L:\\C24-11 Dunkin, 103-105 Elm Street, New Canaan";

    // path library returns '/' in pathnames
    // windows CreateProcessA call does not accept '/' on cmd line, they are
    // interpreted as switches
    std::replace(path.begin(), path.end(), '/', '\\');

    STARTUPINFOA start_up_info;
    ZeroMemory(&start_up_info, sizeof(start_up_info));
    start_up_info.cb = sizeof(STARTUPINFOA);

    // out structure from create process call
    PROCESS_INFORMATION process_info;

    auto cmd = std::string("explorer.exe \"") + path + std::string("\"");
    spdlog::info("cmd string: {}", cmd);

    BOOL result =
        CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
                       nullptr,           // process attributes
                       nullptr,           // thread attributes
                       FALSE,             // don't inherit handles
                       DETACHED_PROCESS,  // process creation flags
                       nullptr, nullptr, &start_up_info, &process_info);

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    if (result == 0) {
      wxLogError("Failed to start file explorer.\nError Code: %d", result,
                 GetLastError());
    } else {
      Close(true);
    }
  }
};

//////////////////////////////////////////////////////////////////////
//                         Main Application                         //
//////////////////////////////////////////////////////////////////////

class cApp : public wxApp {
 public:
  cFrame* frame_ = nullptr;
  cApp(){};
  ~cApp(){};

  virtual bool OnInit() {
    auto logger =
        spdlog::basic_logger_mt("main", "log-find-directory.txt", true);
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
    spdlog::flush_every(std::chrono::seconds(1));
    spdlog::info("----- start of log file ------");
    frame_ = new cFrame();
    frame_->Show();
    return true;
  }
  virtual int OnExit() {
    spdlog::get("main")->flush();
    return 0;
  }
};

wxIMPLEMENT_APP(cApp);
