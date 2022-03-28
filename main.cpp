#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <regex>
// wxWidgets in full of strcpy
#pragma warning(disable : 4996)
#include <wx/textdlg.h>
#include <wx/wx.h>

//#include <locale>

const constexpr int appwidth = 500;
const constexpr int appheight = 100;

wxPoint GetOrigin(const int w, const int h) {
  int desktopWidth = GetSystemMetrics(SM_CXMAXIMIZED);
  int desktopHeight = GetSystemMetrics(SM_CYMAXIMIZED);
  return wxPoint((desktopWidth / 2) - (w / 2), (desktopHeight / 2) - (h / 2));
}

class cFrame : public wxFrame {
 public:
  cFrame()
      : wxFrame(nullptr, wxID_ANY, "Example Title",
                GetOrigin(appwidth, appheight), wxSize(appwidth, appheight)) {
    auto panel = new wxPanel(this, wxID_ANY);

    auto* top = new wxBoxSizer(wxVERTICAL);

    panel->SetSizer(top);

    // Fit not required for panel to expand to frame
    panel->GetSizer()->Fit(this);
    panel->Fit();
  };
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
    /*frame_ = new cFrame();
    frame_->Show();*/
    auto logger = spdlog::basic_logger_mt("main", "log.txt", true);
    spdlog::set_default_logger(std::move(logger));
    spdlog::info("this is a test");

    wxTextEntryDialog dlg(nullptr, "Enter a regex string.");
    if (dlg.ShowModal() != wxID_OK) {
      return false;  // closes the application
    }

    auto text = std::string(dlg.GetValue().mb_str());
    spdlog::info("value from dialogue -> {}", text);

    const std::filesystem::path path{L"L:\\"};
    for (auto const& entry : std::filesystem::directory_iterator{path}) {
      std::string out = entry.path().u8string();
      std::regex r(text, std::regex_constants::icase);
      std::smatch m;
      if (std::regex_search(out, m, r)) {
        spdlog::info("{}", out);
      }
    }

    return false;
  }
  // virtual int OnExit();
};

wxIMPLEMENT_APP(cApp);
