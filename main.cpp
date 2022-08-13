
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <regex>
#include <windows.h>

// wxWidgets is full of non-secure strcpy
#pragma warning(push)
#pragma warning(disable : 4996)
#include <wx/aboutdlg.h>
#include <wx/activityindicator.h>
#include <wx/listctrl.h>
#include <wx/thread.h>
#include <wx/valnum.h>
#include <wx/wx.h>
#pragma warning(pop)

#include "config.h"
#include "log.h"
#include "shell.h"
#include "types.h"

const wxString MY_APP_VERSION_STRING = "1.0";
const constexpr int appwidth = 500;
const constexpr int appheight = 800;

bool
DoesExist(std::string path)
{
  return std::filesystem::exists(path);
}

wxPoint
GetOrigin(const int w, const int h)
{
  int desktopWidth = GetSystemMetrics(SM_CXMAXIMIZED);
  int desktopHeight = GetSystemMetrics(SM_CYMAXIMIZED);
  return wxPoint((desktopWidth / 2) - (w / 2), (desktopHeight / 2) - (h / 2));
}

// container must be an irritable array of type T,
// where type T can be implicitly converted to wxString
template<typename T>
wxArrayString
BuildWxArrayString(const T container)
{
  wxArrayString array;
  for (const auto& text : container) {
    array.Add(wxString(text), 1); // 2nd param (1) is number_of_copies
  }
  return array;
}

Strings
GetFilePaths(std::string base_path, int depth)
{
  if (depth == 0) {
    return {}; // return empty if depth exhausted
  }
  Strings p;
  for (auto const& entry : std::filesystem::directory_iterator{ base_path }) {
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

std::string
EscapeForRegularExpression(const std::string& s)
{
  // Not all special regex characters are escaped
  // missing: [], ?, |
  // seems to have trouble escaping the []
  const std::regex metacharacters("[\.\$\^\{\}\(\)\?\*\+\-]");
  try {
    return std::regex_replace(s, metacharacters, "\\$&");
  } catch (std::regex_error error) {
    // logging is thread safe as 2009
    // https://wxwidgets.blogspot.com/2009/07/blogging-about-logging.html
    wxLogError(error.what());
    return "";
  }
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

class cFrame
  : public wxFrame
  , public wxThreadHelper
{
private:
  wxComboBox* directory_path_entry;
  wxTextCtrl* regex_pattern_entry;
  wxTextCtrl* recursive_depth;
  wxCheckBox* recursive_checkbox;
  wxCheckBox* text_match_checkbox;
  wxListView* search_results;
  wxButton* search_button;
  wxStaticText* results_counter_label;
  // wxActivityIndicator* activity_indicator;

  int m_value_recursion_depth; // used for validator

  std::string search_pattern_;
  std::string search_directory_;
  std::shared_ptr<config::Settings> settings;

  int search_results_index;

public:
  cFrame()
    : wxFrame(nullptr,
              wxID_ANY,
              "Find Directory With Regex",
              GetOrigin(appwidth, appheight),
              wxSize(appwidth, appheight))
  {

    //////////////////////////////////////////////////////////////////////
    //                            Menu Bar                              //
    //////////////////////////////////////////////////////////////////////
    wxMenu* menu_file = new wxMenu;
    menu_file->Append(wxID_SAVE, "Save\tCtrl-s", "Save settings.");
    menu_file->AppendSeparator();
    menu_file->Append(wxID_EXIT);

    wxMenu* menu_edit = new wxMenu;
    menu_edit->Append(wxID_EDIT, "Settings", "Edit settings file.");
    // TODO: make a clear shortcuts method
    // menu_edit->Append(wxID_EDIT, "Clear ShortcutS", "Clear Shortcuts.");

    wxMenu* menu_help = new wxMenu;
    menu_help->Append(wxID_HELP);
    menu_help->AppendSeparator();
    menu_help->Append(wxID_ABOUT);

    wxMenuBar* menu_bar = new wxMenuBar;
    menu_bar->Append(menu_file, "File");
    menu_bar->Append(menu_edit, "Edit");
    menu_bar->Append(menu_help, "Help");

    SetMenuBar(menu_bar);

    //////////////////////////////////////////////////////////////////////

    //  Text Validator for recursion depth
    wxIntegerValidator<int> int_depth_validator(&m_value_recursion_depth);
    int_depth_validator.SetRange(0, 10000);

    auto result = config::LoadFromFile("settings.toml");
    if (!result.success) {
      wxLogError("%s", result.msg);
    }
    settings = std::make_shared<config::Settings>(result.settings);

    auto bookmarks =
      BuildWxArrayString<std::set<std::string>>(settings->bookmarks);

    // Widget Creation
    auto panel = new wxPanel(this); // main panel for layout
    auto regex_pattern_entry_label =
      new wxStaticText(panel, wxID_ANY, "search pattern:");
    auto directory_path_entry_label =
      new wxStaticText(panel, wxID_ANY, "directory:");
    auto recursion_depth_label =
      new wxStaticText(panel, wxID_ANY, "0 = full recursion depth");
    regex_pattern_entry = new wxTextCtrl(panel,
                                         wxID_ANY,
                                         "",
                                         wxDefaultPosition,
                                         wxDefaultSize,
                                         wxTE_PROCESS_ENTER);
    directory_path_entry = new wxComboBox(panel,
                                          wxID_ANY,
                                          settings->default_search_path,
                                          wxDefaultPosition,
                                          wxDefaultSize,
                                          bookmarks,
                                          wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
    text_match_checkbox = new wxCheckBox(panel, wxID_ANY, "text search");
    recursive_checkbox =
      new wxCheckBox(panel, wxID_ANY, "recursively search child directories");
    recursive_depth = new wxTextCtrl(panel,
                                     wxID_ANY,
                                     "0",
                                     wxDefaultPosition,
                                     wxDefaultSize,
                                     0,
                                     int_depth_validator);
    search_button = new wxButton(panel, wxID_ANY, "Search");
    results_counter_label = new wxStaticText(panel, wxID_ANY, "");
    search_results = new wxListView(
      panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_LIST);

    // Set default values
    text_match_checkbox->SetValue(settings->use_text);
    recursive_checkbox->SetValue(settings->use_recursion);
    recursive_depth->ChangeValue(
      wxString::Format(wxT("%i"), settings->recursion_depth));

    // Layout Controls
    auto controls = new wxBoxSizer(wxHORIZONTAL);
    controls->Add(text_match_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    controls->Add(recursive_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    controls->Add(recursive_depth, 1, wxRIGHT, 5);
    controls->Add(
      recursion_depth_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    auto top = new wxBoxSizer(wxVERTICAL);
    top->Add(regex_pattern_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
    top->Add(regex_pattern_entry, 0, wxEXPAND | wxALL, 5);
    top->Add(directory_path_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
    top->Add(directory_path_entry, 0, wxEXPAND | wxALL, 5);
    top->Add(controls, 0, wxEXPAND | wxALL, 5);
    top->Add(search_button, 0, wxEXPAND | wxALL, 5);
    top->Add(results_counter_label, 0, wxEXPAND | wxLEFT, 5);
    top->Add(search_results, 1, wxEXPAND | wxALL, 5);
    panel->SetSizer(top);

    // Bind Keyboard Shortcuts
    wxAcceleratorEntry k1(wxACCEL_CTRL, WXK_CONTROL_S, wxID_SAVE);
    SetAcceleratorTable(wxAcceleratorTable(1, &k1));

    // Bind Events
    Bind(wxEVT_COMMAND_MENU_SELECTED, &cFrame::OnClose, this, wxID_EXIT);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &cFrame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &cFrame::OnHelp, this, wxID_HELP);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &cFrame::OnEdit, this, wxID_EDIT);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &cFrame::OnAbout, this, wxID_ABOUT);
    search_button->Bind(wxEVT_BUTTON, &cFrame::OnSearch, this);
    regex_pattern_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
    directory_path_entry->Bind(wxEVT_TEXT_ENTER, &cFrame::OnSearch, this);
    text_match_checkbox->Bind(wxEVT_CHECKBOX, &cFrame::OnOptionsText, this);
    recursive_checkbox->Bind(wxEVT_CHECKBOX, &cFrame::OnOptionsRecursion, this);
    recursive_depth->Bind(
      wxEVT_CHECKBOX, &cFrame::OnOptionsRecursionDepth, this);
    search_results->Bind(wxEVT_LIST_ITEM_SELECTED, &cFrame::OnItem, this);

    // Handle and display messages to text control widget sent from outside GUI
    // thread
    Bind(wxEVT_THREAD, [this](wxThreadEvent& event) {
      switch (event.GetInt()) {
        case message_code::search_result:
          search_results->InsertItem(search_results_index++, event.GetString());
          // In testing, matches were found (even on a network drive) much
          // faster that the list was being updated. I could never get the list
          // to update as matches were found (as does grepWin does). A queue of
          // update messages would pile up until the end making the overall
          // task slower.
          // search_results->UpdateWindowUI();
          break;
        case message_code::search_lump_results:
          for (auto& item : event.GetPayload<Strings>()) {
            search_results->InsertItem(search_results_index++, item);
          }
          break;
        case message_code::search_finished:
          search_button->SetLabel("Search");
          auto label =
            wxString::Format(wxT("%i matches found"), search_results_index);
          results_counter_label->SetLabel(label);
          results_counter_label->Show();
          break;
      }
    });
  }

  void OnOptionsText(wxCommandEvent& event)
  {
    settings->use_text = text_match_checkbox->GetValue();
  }

  void OnOptionsRecursion(wxCommandEvent& event)
  {
    settings->use_recursion = recursive_checkbox->GetValue();
  }

  void OnOptionsRecursionDepth(wxCommandEvent& event)
  {
    const wxString s_depth = recursive_depth->GetValue();
    settings->recursion_depth = wxAtoi(s_depth);
  }

  void UpdateResult(std::string result)
  {
    wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
    event->SetInt(message_code::search_result);
    event->SetString(result);
    this->QueueEvent(event);
    // VERY IMPORTANT: do not call any GUI function inside this thread, rather
    // use wxQueueEvent(). We used pointer 'this'
    // assuming it's safe; see OnClose()
  }

  wxThread::ExitCode Entry()
  {
    const auto use_text = settings->use_text;
    const auto use_recursion = settings->use_recursion;
    const auto recursion_depth = settings->recursion_depth;

    if (use_text) {
      search_pattern_ = EscapeForRegularExpression(search_pattern_);
    }

    // TODO: put the search call or iterator behind a function or something or
    // co_func so that way i can have a single search loop or multiple loops for
    // the different generators and a single function call

    // Check to see if the path exists with a timeout
    std::future<bool> future = std::async(DoesExist, search_directory_);
    SPDLOG_DEBUG("checking, please wait");
    std::chrono::milliseconds span(1000);
    if (future.wait_for(span) == std::future_status::ready) {
      if (future.get()) {
        SPDLOG_DEBUG("The path does exist.");
      } else {
        wxLogError("The path does not exist.");
        wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
        event->SetInt(message_code::search_finished);
        this->QueueEvent(event);
        return (wxThread::ExitCode)0;
      }
    } else {
      wxLogError("Couldn't access the path in a reasonable amount of "
                 "time.\nIt may be in-accessible or not exist.");
      wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
      event->SetInt(message_code::search_finished);
      this->QueueEvent(event);
      return (wxThread::ExitCode)0;
    }

    try {
      if (use_recursion && recursion_depth == 0) { // 0 == unrestricted depth
        std::regex r(search_pattern_, std::regex_constants::icase);
        std::smatch m;
        for (auto const& entry : std::filesystem::recursive_directory_iterator{
               search_directory_ }) {
          if (GetThread()->TestDestroy()) { // this is so ugly
            break;
          }
          std::string path = entry.path().generic_string();
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            UpdateResult(path); // push a single match to the results list
          }
        }
      } else if (use_recursion && recursion_depth > 1) {
        // a depth of (1) is the same as using no recursion therefore it is
        // handled in the else
        Strings matches;
        auto all_paths = GetFilePaths(search_directory_, recursion_depth);
        std::regex r(search_pattern_, std::regex_constants::icase);
        std::smatch m;
        for (auto const& path : all_paths) {
          if (GetThread()->TestDestroy()) {
            break;
          }
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            matches.push_back(path);
          }
        }
        // lump matches into a single message as an optimization
        wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
        event->SetInt(message_code::search_lump_results);
        event->SetPayload<Strings>(matches);
        this->QueueEvent(event);
      } else { // no recursion, only search the folder names in the directory
        std::regex r(search_pattern_, std::regex_constants::icase);
        std::smatch m;
        for (auto const& entry :
             std::filesystem::directory_iterator{ search_directory_ }) {
          if (GetThread()->TestDestroy()) {
            break;
          }
          std::string path = entry.path().generic_string();
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            UpdateResult(path); // push a single match to the results list
          }
        }
      }
      settings->AddBookmark(search_directory_);
      settings->Save();
    } catch (std::filesystem::filesystem_error& e) {
      // logging is thread safe as 2009
      // https://wxwidgets.blogspot.com/2009/07/blogging-about-logging.html
      wxLogError("%s", e.what());
    } catch (std::regex_error& e) {
      wxLogError("%s", e.what());
    }
    // post a search_finished message to my frame when complete
    wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
    event->SetInt(message_code::search_finished);
    this->QueueEvent(event);
    return (wxThread::ExitCode)0;
  }

  void OnSearch(wxCommandEvent& event)
  {
    // TODO: ping server availability before searching
    // also a stop button!
    // start a new search if thread not already searching
    if (!GetThread() || !(GetThread()->IsRunning())) {
      results_counter_label->SetLabel("searching...");
      search_results->DeleteAllItems();
      search_results_index = 0;
      SPDLOG_DEBUG("on search is entering");

      // get user data from panel widgets for thread
      search_pattern_ =
        std::string(regex_pattern_entry->GetLineText(0).mb_str());
      search_directory_ =
        std::string(directory_path_entry->GetValue().mb_str());

      /**
       * - gui does a bunch of set up work
       * - gui launches a thread
       * - every time a directory matches, a message is sent to the GUI with the
       * file path.
       * - once thread completes work, a final finish msg is sent to the gui
       *
       * does the gui need to be able to cancel the thread?
       */

      // We want to start a long task, but we don't want our GUI to block
      // while it's executed, so we use a thread to do it.
      // Use the thread specified in the thread helper.
      if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR) {
        wxLogError("Could not create the worker thread!");
        return;
      }
      if (GetThread()->Run() != wxTHREAD_NO_ERROR) {
        wxLogError("Could not run the worker thread!");
        return;
      }

      // after the thread is successfully running, now I can notify the user
      // that things are happening
      search_button->SetLabel("Stop");
      // launch widgets to display searching
    } else { // the thread is running so I must stop the current search
      search_button->SetLabel("Search");
      GetThread()->Delete();
    }
  }

  void OnItem(wxListEvent& event)
  {
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

    auto cmd = std::string("explorer'.exe' \"") + path + std::string("\"");
    SPDLOG_DEBUG("cmd string: {}", cmd);

    BOOL result = CreateProcessA(nullptr,
                                 const_cast<char*>(cmd.c_str()),
                                 nullptr,          // process attributes
                                 nullptr,          // thread attributes
                                 FALSE,            // don't inherit handles
                                 DETACHED_PROCESS, // process creation flags
                                 nullptr,
                                 nullptr,
                                 &start_up_info,
                                 &process_info);

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    if (result == 0) {
      wxLogError("Failed to start file explorer.\nError Code: %d", result);
      return;
    }
    if (settings->exit_on_search) {
      Close(true);
    }
  }

  void OnSave(wxCommandEvent&) { settings->Save(); }

  void OnClose(wxCommandEvent&)
  {
    // important: before terminating, we _must_ wait for our joinable
    // thread to end, if it's running; in fact it uses variables of this
    // instance and posts events to *this event handler
    if (GetThread() && // DoStartALongTask() may have not been called
        GetThread()->IsRunning())
      // GetThread()->Wait(); // wait for the thread to join
      //  delete the thread gracefully, TestDestroy() will return true
      GetThread()->Delete();
    Destroy();
  }

  void OnAbout(wxCommandEvent&)
  {
    wxAboutDialogInfo aboutInfo;
    aboutInfo.SetName("Find Project Directories");
    aboutInfo.SetVersion(MY_APP_VERSION_STRING);
    aboutInfo.SetCopyright("(C) 2022");
    // defining a website triggers the full-blown generic version to be used.
    // the generic version looks nicer.
    // aboutInfo.SetWebSite("http://myapp.org");
    aboutInfo.SetDescription("A application for quickly navigating to project "
                             "directories by name or "
                             "searching for projects in the archives.\n\n"
                             "Author: George Kuegler\n"
                             "E-mail: george@KueglerAssociates.net");
    wxAboutBox(aboutInfo);
  }

  void OnHelp(wxCommandEvent&)
  {
    // open the readme file with default editor
    // windows will prompt the user for a suitable program if not found
    auto failure = LaunchShellCommand(GetHandle(), "open", "readme.md");
    if (failure) {
      if (failure.value() == ERROR_FILE_NOT_FOUND) {
        wxLogError("Couldn't find the help 'readme.md' file usually included "
                   "in '.exe' directory.");
      }
    }
  }

  void OnEdit(wxCommandEvent&)
  {
    // open the help file with default editor
    // windows will prompt the user for a suitable program if not found
    auto failure = LaunchShellCommand(GetHandle(), "open", "settings.toml");
    if (failure) {
      if (failure.value() == ERROR_FILE_NOT_FOUND) {
        wxLogError("Couldn't find the settings file: 'settings.toml' usually "
                   "included in '.exe' directory.");
      }
    }
  }
};

//////////////////////////////////////////////////////////////////////
//                         Main Application                         //
//////////////////////////////////////////////////////////////////////

class cApp : public wxApp
{
public:
  cFrame* frame_ = nullptr;
  cApp(){};
  ~cApp(){};

  virtual bool OnInit()
  {
    // I am enabling logging only for debug mode.
    // My voice coding tools launch the release version of this application.
    // It doesn't have file write permission when this application is started
    // from the working directory of natlink.

    SetUpLogging();
    frame_ = new cFrame();
    frame_->Show();
    return true;
  }
  virtual int OnExit()
  {
    void FlushLogging();
    return 0;
  }
};

wxIMPLEMENT_APP(cApp);
