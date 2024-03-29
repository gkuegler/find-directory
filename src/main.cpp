/**
 *
 * License: MIT
 *
 * Author: George Kuegler
 * E-mail: georgekuegler@gmail.com
 *
 */

// TODO: add command line option to open with specific direct to search
// TODO: copy project name/path to the clipboard (for the purpose
//  of file transfer tool)

// Fix wxWidgets and linking on windows.
#pragma comment(lib, "comctl32")
#pragma comment(lib, "Rpcrt4")

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <regex>

// wxWidgets is full of non-secure strcpy
#pragma warning(push)
#pragma warning(disable : 4996)
#define wxUSE_SOCKETS 0
#include <wx/aboutdlg.h>
#include <wx/activityindicator.h>
#include <wx/listctrl.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/valnum.h>
#include <wx/wx.h>
#pragma warning(pop)

// Need to include Windows header after
// wxWidgets due to winsock2 incompatibility.
#include <windows.h>

#include "config.h"
#include "log.h"
#include "shell.h"
#include "types.h"

const wxString MY_APP_VERSION_STRING = "1.3";
const wxString MY_APP_DATE = __DATE__;
const constexpr int default_app_width = 550;
const constexpr int default_app_height = 800;

wxPoint
GetOrigin(const int w, const int h)
{
  int desktop_width = GetSystemMetrics(SM_CXMAXIMIZED);
  int desktop_height = GetSystemMetrics(SM_CYMAXIMIZED);
  return wxPoint((desktop_width / 2) - (w / 2),
                 (desktop_height / 2) - (h / 2));
}

// requirement: type T<S> must be an iterable container where S can be
// implicitly converted to a wxString.
// This function is a bit inefficient, but it's typically only used at
// startup.
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
  for (auto const& entry :
       std::filesystem::directory_iterator{ base_path }) {
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

class Frame
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
  // FUTURE: wheel control to show progress on long searches.
  // wxActivityIndicator* activity_indicator;

  int m_value_recursion_depth; // used for validator

  std::string search_pattern_;
  std::string search_directory_;
  std::shared_ptr<config::Settings> settings;

  int search_results_index;

public:
  Frame(const wxString& default_ptrn,
        const wxString& default_search_folder)
    : wxFrame(nullptr,
              wxID_ANY,
              "Find Directory With Regex",
              GetOrigin(default_app_width, default_app_height),
              wxSize(default_app_width, default_app_height))
  {

    //////////////////////////////////////////////////////////////////////
    //                            Menu Bar //
    //////////////////////////////////////////////////////////////////////
    wxMenu* menu_file = new wxMenu;
    menu_file->Append(wxID_SAVE, "Save\tCtrl-s", "Save settings.");
    menu_file->AppendSeparator();
    menu_file->Append(wxID_EXIT);

    wxMenu* menu_edit = new wxMenu;
    menu_edit->Append(wxID_EDIT, "Settings", "Edit settings file.");
    // TODO: make a clear shortcuts method
    // menu_edit->Append(wxID_EDIT, "Clear ShortcutS", "Clear
    // Shortcuts.");

    wxMenu* menu_help = new wxMenu;
    // menu_help->Append(wxID_HELP);
    // TODO: package readme help file into executable?
    // menu_help->AppendSeparator();
    menu_help->Append(wxID_ABOUT);

    wxMenuBar* menu_bar = new wxMenuBar;
    menu_bar->Append(menu_file, "File");
    menu_bar->Append(menu_edit, "Edit");
    menu_bar->Append(menu_help, "Help");

    SetMenuBar(menu_bar);

    //////////////////////////////////////////////////////////////////////

    //  Text Validator for recursion depth
    wxIntegerValidator<int> int_depth_validator(
      &m_value_recursion_depth);
    int_depth_validator.SetRange(0, 10000);

    // Load from file will prefix executable directory to form absolute
    // path.
    auto result = config::LoadFromFile("find-directory-settings.toml");
    if (!result.success) {
      wxLogError("%s", result.msg);
    }

    // TODO: what happens if this fails?
    settings = std::make_shared<config::Settings>(result.settings);

    auto bookmarks =
      BuildWxArrayString<std::set<std::string>>(settings->bookmarks);

    // main panel for layout
    auto panel = new wxPanel(this);

    auto regex_pattern_entry_label =
      new wxStaticText(panel, wxID_ANY, "search pattern:");
    auto directory_path_entry_label =
      new wxStaticText(panel, wxID_ANY, "directory:");
    auto recursion_depth_label = new wxStaticText(
      panel, wxID_ANY, "0 = unlimited recursion depth");

    regex_pattern_entry = new wxTextCtrl(panel,
                                         wxID_ANY,
                                         default_ptrn,
                                         wxDefaultPosition,
                                         wxDefaultSize,
                                         wxTE_PROCESS_ENTER);

    directory_path_entry = new wxComboBox(
      panel,
      wxID_ANY,
      !default_search_folder.empty() ? default_search_folder
                                     : settings->default_search_path,
      wxDefaultPosition,
      wxDefaultSize,
      bookmarks,
      wxCB_DROPDOWN | wxTE_PROCESS_ENTER);

    text_match_checkbox =
      new wxCheckBox(panel, wxID_ANY, "text search");
    recursive_checkbox = new wxCheckBox(
      panel, wxID_ANY, "recursively search child directories");
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
    controls->Add(
      text_match_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    controls->Add(
      recursive_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    controls->Add(recursive_depth, 1, wxRIGHT, 5);
    controls->Add(
      recursion_depth_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    auto top = new wxBoxSizer(wxVERTICAL);
    top->Add(regex_pattern_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
    top->Add(regex_pattern_entry, 0, wxEXPAND | wxALL, 5);
    top->Add(
      directory_path_entry_label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
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
    Bind(wxEVT_COMMAND_MENU_SELECTED, &Frame::OnClose, this, wxID_EXIT);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &Frame::OnSave, this, wxID_SAVE);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &Frame::OnHelp, this, wxID_HELP);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &Frame::OnEdit, this, wxID_EDIT);
    Bind(
      wxEVT_COMMAND_MENU_SELECTED, &Frame::OnAbout, this, wxID_ABOUT);
    search_button->Bind(wxEVT_BUTTON, &Frame::OnSearch, this);
    regex_pattern_entry->Bind(wxEVT_TEXT_ENTER, &Frame::OnSearch, this);
    directory_path_entry->Bind(
      wxEVT_TEXT_ENTER, &Frame::OnSearch, this);
    search_results->Bind(
      wxEVT_LIST_ITEM_SELECTED, &Frame::OnItem, this);

    // Handle and display messages to text control widget sent from
    // outside GUI thread
    Bind(wxEVT_THREAD, [this](wxThreadEvent& event) {
      switch (event.GetInt()) {
        case message_code::search_result:
          search_results->InsertItem(search_results_index++,
                                     event.GetString());
          // In testing, matches were found (even on a network drive)
          // much faster that the list was being updated. I could never
          // get the list to update as matches were found (as does
          // grepWin does). A queue of update messages would pile up
          // until the end making the overall task slower.
          // search_results->UpdateWindowUI();
          break;
        case message_code::search_lump_results:
          for (auto& item : event.GetPayload<Strings>()) {
            search_results->InsertItem(search_results_index++, item);
          }
          break;
        case message_code::search_finished:
          search_button->SetLabel("Search");
          auto label = wxString::Format(wxT("%i matches found"),
                                        search_results_index);
          results_counter_label->SetLabel(label);
          results_counter_label->Show();
          break;
      }
    });

    // Automatically start a search if 'default_ptrn' contains a value.
    // default_ptrn will be non-empty if a search pattern was provided
    // as a commandline argument.
    if (!default_ptrn.empty() and
        !directory_path_entry->IsTextEmpty()) {
      this->OnSearch(wxCommandEvent());
    }
  }

  void UpdateResult(std::string result)
  {
    wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
    event->SetInt(message_code::search_result);
    event->SetString(result);
    this->QueueEvent(event);
    // VERY IMPORTANT: do not call any GUI function inside this thread,
    // rather use wxQueueEvent(). We used pointer 'this' assuming it's
    // safe; see OnClose()
  }

  wxThread::ExitCode Entry()
  {
    const auto use_text = settings->use_text;
    const auto use_recursion = settings->use_recursion;
    const auto recursion_depth = settings->recursion_depth;

    if (use_text) {
      search_pattern_ = EscapeForRegularExpression(search_pattern_);
    }

    // TODO: put the search call or iterator behind a function or
    // something or co_func so that way i can have a single search loop
    // or multiple loops for the different generators and a single
    // function call

    // Check to see if the path exists with a timeout
    std::future<bool> future = std::async(
      [](std::filesystem::path sd) {
        return std::filesystem::exists(sd);
      },
      std::filesystem::path(search_directory_));

    SPDLOG_DEBUG("checking, please wait");

    if (future.wait_for(std::chrono::milliseconds(1000)) ==
        std::future_status::ready) {
      if (future.get()) {
        SPDLOG_DEBUG("The path does exist.");
      } else {
        wxLogError("The path does not exist.");
        wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
        event->SetInt(message_code::search_finished);
        this->QueueEvent(event);
        return static_cast<wxThread::ExitCode>(0);
      }
    } else {
      wxLogError("Couldn't access the path in a reasonable amount of "
                 "time.\nIt may be in-accessible or not exist.");
      wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD);
      event->SetInt(message_code::search_finished);
      this->QueueEvent(event);
      return static_cast<wxThread::ExitCode>(0);
    }

    try {
      if (use_recursion &&
          recursion_depth == 0) { // 0 == unrestricted depth
        std::regex r(search_pattern_, std::regex_constants::icase);
        std::smatch m;
        for (auto const& entry :
             std::filesystem::recursive_directory_iterator{
               search_directory_ }) {
          if (GetThread()->TestDestroy()) { // this is so ugly
            break;
          }
          std::string path = entry.path().generic_string();
          if (std::regex_search(path, m, r)) {
            SPDLOG_DEBUG("path found: {}", path);
            UpdateResult(
              path); // push a single match to the results list
          }
        }
      } else if (use_recursion && recursion_depth > 1) {
        // a depth of (1) is the same as using no recursion therefore it
        // is handled in the else
        Strings matches;
        auto all_paths =
          GetFilePaths(search_directory_, recursion_depth);
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
      } else {
        // no recursion, only search the folder names in the
        // directory
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
            UpdateResult(
              path); // push a single match to the results list
          }
        }
      }
      // add searchpath to dropdown
      settings->AddBookmark(search_directory_);
      // settings->Save();  // I do not want to save settings

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
    return static_cast<wxThread::ExitCode>(0);
  }

  void OnSearch(const wxCommandEvent&)
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
       * - every time a directory matches, a message is sent to the GUI
       * with the file path.
       * - once thread completes work, a final finish msg is sent to the
       * gui
       *
       * does the gui need to be able to cancel the thread?
       */

      // We want to start a long task, but we don't want our GUI to
      // block while it's executed, so we use a thread to do it. Use the
      // thread specified in the thread helper.
      if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR) {
        wxLogError("Could not create the worker thread!");
        return;
      }
      if (GetThread()->Run() != wxTHREAD_NO_ERROR) {
        wxLogError("Could not run the worker thread!");
        return;
      }

      // after the thread is successfully running, now I can notify the
      // user that things are happening
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
    // std::string path = "L:\\C24-11 Dunkin, 103-105 Elm Street, New
    // Canaan";

    // path library returns '/' in pathnames
    // windows CreateProcessA call does not accept '/' on cmd line, they
    // are interpreted as switches
    std::replace(path.begin(), path.end(), '/', '\\');

    STARTUPINFOA start_up_info;
    ZeroMemory(&start_up_info, sizeof(start_up_info));
    start_up_info.cb = sizeof(STARTUPINFOA);

    // out structure from create process call
    PROCESS_INFORMATION process_info;

    auto cmd =
      std::string("explorer.exe \"") + path + std::string("\"");
    SPDLOG_DEBUG("cmd string: {}", cmd);

    BOOL result =
      CreateProcessA(nullptr,
                     const_cast<char*>(cmd.c_str()),
                     nullptr,          // process attributes
                     nullptr,          // thread attributes
                     FALSE,            // don't inherit handles
                     DETACHED_PROCESS, // process creation flags
                     nullptr,
                     nullptr,
                     &start_up_info,
                     &process_info);

    if (process_info.hProcess) {
      CloseHandle(process_info.hProcess);
    }
    if (process_info.hThread) {
      CloseHandle(process_info.hThread);
    }

    if (result == 0) {
      wxLogError("Failed to start file explorer.\nError Code: %d",
                 GetLastError());
      return;
    }
    if (settings->exit_on_search) {
      Close(true);
    }
  }

  void OnSave(wxCommandEvent&)
  {
    settings->use_text = text_match_checkbox->GetValue();
    settings->use_recursion = recursive_checkbox->GetValue();
    const wxString s_depth = recursive_depth->GetValue();
    settings->recursion_depth = wxAtoi(s_depth);
    settings->Save();
  }

  void OnClose(wxCommandEvent&)
  {
    // important: before terminating, we _must_ wait for our joinable
    // thread to end, if it's running; in fact it uses variables of this
    // instance and posts events to *this event handler
    if (GetThread() && // DoStartALongTask() may have not been called
        GetThread()->IsRunning())
      // GetThread()->Wait(); // wait for the thread to join
      // delete the thread gracefully, TestDestroy() will return true
      GetThread()->Delete();
    Destroy();
  }

  void OnAbout(wxCommandEvent&)
  {
    wxAboutDialogInfo aboutInfo;
    aboutInfo.SetName("Find Project Directories");
    aboutInfo.SetVersion(MY_APP_VERSION_STRING, wxString());
    aboutInfo.SetCopyright("(C) 2022");
    // defining a website triggers the full-blown generic version to be
    // used. the generic version looks nicer.
    // aboutInfo.SetWebSite("http://myapp.org");
    aboutInfo.SetDescription(wxString::Format(
      "A application for quickly navigating to project "
      "directories by name or "
      "searching for projects in the archives.\n\n"
      "Author: George Kuegler\n"
      "E-mail: george@KueglerAssociates.net\n\nBuilt On: %s",
      MY_APP_DATE));
    wxAboutBox(aboutInfo);
  }

  void OnHelp(wxCommandEvent&)
  {
    // open the readme file with default editor
    // windows will prompt the user for a suitable program if not found
    auto failure = LaunchShellCommand(GetHandle(), "open", "readme.md");
    if (failure) {
      if (failure.value() == ERROR_FILE_NOT_FOUND) {
        wxLogError(
          "Couldn't find the help 'readme.md' file usually included "
          "in '.exe' directory.");
      }
    }
  }

  void OnEdit(wxCommandEvent&)
  {
    // open the help file with default editor
    // windows will prompt the user for a suitable program if not found
    // auto path = GetFullPath(settings->file_name_);
    const auto path = settings->file_path_;
    auto failure = LaunchShellCommand(GetHandle(), "open", path);
    if (failure) {
      if (failure.value() == ERROR_FILE_NOT_FOUND) {
        wxLogError("Couldn't find the settings file: '%s' usually "
                   "included in '.exe' directory.",
                   path);
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
  Frame* frame = nullptr;
  cApp(){};
  ~cApp(){};

  virtual bool OnInit()
  {
    // I am enabling logging only for debug mode.
    // My voice coding tools launch the release version of this
    // application. It doesn't have file write permission when this
    // application is started from the working directory of natlink.
#ifdef _DEBUG
    SetUpLogging();
#endif

    // Parse CMD line options.
    const auto arg_count = wxTheApp->argc;
    SPDLOG_DEBUG("argument count: {}", arg_count);

    const wxString default_ptrn =
      arg_count > 1 ? wxTheApp->argv[1] : wxString("");

    const wxString default_search_folder =
      arg_count > 2 ? wxTheApp->argv[2] : wxString("");

    frame = new Frame(default_ptrn, default_search_folder);
    frame->Show();
    return true;
  }
  virtual int OnExit()
  {
#ifdef _DEBUG
    void FlushLogging();
#endif
    return 0;
  }
};

wxIMPLEMENT_APP(cApp);
