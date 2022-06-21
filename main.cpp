#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <regex>

// wxWidgets is full of strcpy
#pragma warning(push)
#pragma warning(disable : 4996)
#include <wx/listctrl.h>
#include <wx/valnum.h>
#include <wx/wx.h>
#pragma warning(pop)

#include "config.h"
#include "types.h"

const constexpr int appwidth = 500;
const constexpr int appheight = 800;

wxPoint GetOrigin(const int w, const int h) {
  int desktopWidth = GetSystemMetrics(SM_CXMAXIMIZED);
  int desktopHeight = GetSystemMetrics(SM_CYMAXIMIZED);
  return wxPoint((desktopWidth / 2) - (w / 2), (desktopHeight / 2) - (h / 2));
}

// container must be an irritable array of type T,
// where type T can be implicitly converted to wxString
template <typename T>
wxArrayString BuildWxArrayString(const T container) {
  wxArrayString array;
  for (const auto& text : container) {
    array.Add(wxString(text), 1);
  }
  return array;
}

// assumed depth should be >1
Strings GetFilePaths(std::string base_path, int depth) {
  if (depth == 0) {
    return {};  // return empty default
  }
  // wxASSERT_MSG(depth > 1, "recursion depth should be greater than 1");
  Strings p;
  // 1st level
  for (auto const& entry : std::filesystem::directory_iterator{base_path}) {
    if (entry.is_directory()) {
      auto folder = entry.path().generic_string();
      p.push_back(folder);
      // use recursion
      auto paths = GetFilePaths(folder, depth - 1);
      p.insert(p.end(), paths.begin(), paths.end());
    }
  }
  return p;
}

// used for directory_iterator and recursive_directory_iterator
// template <class T>
// Strings UnrollIterator(T iterator) {
//  Strings s;
//  for (const std::filesystem::directory_path_entry& elem : iterator) {
//    s.push_back(elem.path().generic_string());
//  }
//  return s;
//}

std::string EscapeForRegularExpression(const std::string& s) {
  // TODO: expand the replacement to all special characters
  // const std::regex metacharacters("[\.\^\$\-\+\(\)\[\]\{\}\|\?\*]");
  const std::regex metacharacters("[\.\$]");
  // return std::regex_replace(s, metacharacters, "\\$&");
  try {
    return std::regex_replace(s, metacharacters, "\\$&");
  } catch (std::regex_error error) {
    wxLogError(error.what());
    return "";
  }
}

class cFrame : public wxFrame {
 private:
  wxComboBox* directory_path_entry;
  wxTextCtrl* regex_pattern_entry;
  wxTextCtrl* recursive_depth;
  wxCheckBox* recursive_checkbox;
  wxCheckBox* text_match_checkbox;
  wxListView* search_results;
  int m_value_recursion_depth;
  std::shared_ptr<config::Settings> settings;

 public:
  cFrame()
      : wxFrame(nullptr, wxID_ANY, "Find Directory With Regex",
                GetOrigin(appwidth, appheight), wxSize(appwidth, appheight)) {
    // clang-format off
      // Text Validator for recursion depth
      wxIntegerValidator<int> int_depth_validator(&m_value_recursion_depth);
      int_depth_validator.SetRange(0, 10);

      auto result = config::LoadFromFile("settings.toml");
      if (!result.success){
        wxLogError("%s", result.msg);
      }
      settings = std::make_shared<config::Settings>(result.settings);

      auto bookmarks = BuildWxArrayString<std::set<std::string >>(settings->bookmarks);

      // Widget Creation
      auto panel = new wxPanel(this);  // main panel for layout
      auto regex_pattern_entry_label = new wxStaticText(panel, wxID_ANY, "search pattern:");
      auto directory_path_entry_label = new wxStaticText(panel, wxID_ANY, "directory:");
      auto recursion_depth_label = new wxStaticText(panel, wxID_ANY, "0 = full recursion depth");
      regex_pattern_entry = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
      directory_path_entry = new wxComboBox(panel, wxID_ANY, settings->default_search_path, wxDefaultPosition, wxDefaultSize, bookmarks, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
      recursive_depth = new wxTextCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, 0, int_depth_validator);
      recursive_checkbox = new wxCheckBox(panel, wxID_ANY, "recursively search child directories");
      text_match_checkbox = new wxCheckBox(panel, wxID_ANY, "text search");
      search_results = new wxListView(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_LIST);
      auto search_button = new wxButton(panel, wxID_ANY, "Search");

      //Set default values
      text_match_checkbox->SetValue(settings->use_text);
      recursive_checkbox->SetValue(settings->use_recursion);
      recursive_depth->ChangeValue(wxString::Format(wxT("%i"), settings->recursion_depth));

      // Layout Controls
      auto controls = new wxBoxSizer(wxHORIZONTAL);
      controls->Add(text_match_checkbox, 0, wxRIGHT, 5);
      controls->Add(recursive_checkbox, 0, wxRIGHT, 5);
      controls->Add(recursive_depth, 1, wxRIGHT, 5);
      controls->Add(recursion_depth_label, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);

      auto top = new wxBoxSizer(wxVERTICAL);
      top->Add(regex_pattern_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
      top->Add(regex_pattern_entry, 0, wxEXPAND | wxALL, 5);
      top->Add(directory_path_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
      top->Add(directory_path_entry, 0, wxEXPAND | wxALL, 5);
      top->Add(controls, 0, wxEXPAND | wxALL, 5);
      top->Add(search_button, 0, wxEXPAND | wxALL, 5);
      top->Add(search_results, 1, wxEXPAND | wxALL, 5);
      panel->SetSizer(top);

      // Bind Events
      search_button->Bind(wxEVT_BUTTON, &cFrame::OnSearch, this);
      regex_pattern_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
      directory_path_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
      search_results->Bind(wxEVT_LIST_ITEM_SELECTED, &cFrame::OnItem, this);
    // clang-format on
  }
  void OnSearch(wxCommandEvent& event) {
    search_results->DeleteAllItems();
    SPDLOG_DEBUG("on search is entering");

    // get user data from panel widgets
    auto text = std::string(regex_pattern_entry->GetLineText(0).mb_str());
    const auto path =
        // std::string(directory_path_entry->GetLineText(0).mb_str());
        std::string(directory_path_entry->GetValue().mb_str());
    const auto use_text = text_match_checkbox->GetValue();
    const auto use_recursion = recursive_checkbox->GetValue();
    const wxString s_depth = recursive_depth->GetValue();
    const auto recursion_depth = wxAtoi(s_depth);

    //  escape all regex characters to search for literal text
    if (use_text) {
      text = EscapeForRegularExpression(text);
    }

    try {
      int i = 0;  // list display insertion index for each found path
      if (use_recursion && recursion_depth == 0) {
        std::regex r(text, std::regex_constants::icase);
        std::smatch m;
        for (auto const& entry :
             std::filesystem::recursive_directory_iterator{path}) {
          std::string path = entry.path().generic_string();
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            search_results->InsertItem(i++, path);
          }
        }
      } else if (use_recursion && recursion_depth > 1) {
        auto paths = GetFilePaths(path, recursion_depth);
        std::regex r(text, std::regex_constants::icase);
        std::smatch m;
        for (auto const& path : paths) {
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            search_results->InsertItem(i++, path);
          }
        }
      } else {  // don't use recursion
        std::regex r(text, std::regex_constants::icase);
        std::smatch m;
        for (auto const& entry : std::filesystem::directory_iterator{path}) {
          std::string path = entry.path().generic_string();
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            search_results->InsertItem(i++, path);
          }
        }
      }

      // save state if search is valid
      // SPDLOG_DEBUG("adding bookmark");
      settings->use_text = use_text;
      settings->use_recursion = use_recursion;
      settings->recursion_depth = recursion_depth;
      settings->AddBookmark(path);
      settings->Save();

    } catch (std::filesystem::filesystem_error& e) {
      wxLogError("%s", e.what());
    } catch (std::regex_error& e) {
      wxLogError("%s", e.what());
    }
    SPDLOG_DEBUG("on search is exiting");
  }
  void OnRecursive(wxCommandEvent& event) {
    wxLogError("functionality not implemented yet");
    recursive_checkbox->SetValue(false);
  }

  void OnTextOption(wxCommandEvent& event) {
    wxLogError("functionality not implemented yet");
    text_match_checkbox->SetValue(false);
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
    SPDLOG_DEBUG("cmd string: {}", cmd);

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
    // I am enabling logging only for debug mode.
    // My voice coding tools launch the release version of this application.
    // It doesn't have file write permission when this application is started
    // from the working directory of natlink.

#ifdef _DEBUG
    auto logger =
        spdlog::basic_logger_mt("main", "log-find-directory.txt", true);
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));
    spdlog::flush_every(std::chrono::seconds(1));
    spdlog::info("----- start of log file ------");
#endif
    frame_ = new cFrame();
    frame_->Show();
    return true;
  }
  virtual int OnExit() {
#ifdef _DEBUG
    spdlog::get("main")->flush();
#endif
    return 0;
  }
};

wxIMPLEMENT_APP(cApp);
