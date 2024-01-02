#ifndef FINDIR_SHELL_H
#define FINDIR_SHELL_H
#include "log.h"
#include <optional>
#include <string>
#include <windows.h>

/**
 * Any return value will be used as a constructor for the optional.
 * */
std::optional<INT_PTR>
LaunchShellCommand(HWND handle,
                   std::string verb,
                   std::string file_path,
                   std::string parameters = "",
                   std::string directory = "")
{
  HINSTANCE result = ShellExecuteA(
    handle,
    verb.c_str(),
    file_path.c_str(),
    parameters.empty() ? nullptr : parameters.c_str(), // lpParameters,
    directory.empty() ? nullptr : directory.c_str(),   // lpDirectory,
    SW_SHOWNORMAL);

  // a Microsoft backward compatibility thing
  // INT_PTR: an integer guaranteed to be the length of the pointer
  INT_PTR real_result = reinterpret_cast<INT_PTR>(result);

  if (real_result > 32) {
    spdlog::info("the shell execute operation succeeded");
    return {}; // return empty optional on success
  } else {
    switch (real_result) {
      case 0:
        SPDLOG_DEBUG("the operating system is out of memory");
        break;
      case ERROR_FILE_NOT_FOUND:
        SPDLOG_DEBUG("The system cannot find the file specified.");
        break;
      case ERROR_PATH_NOT_FOUND:
        SPDLOG_DEBUG("ERROR_BAD_FORMAT");
        break;
      case SE_ERR_ACCESSDENIED:
        SPDLOG_DEBUG("SE_ERR_ACCESSDENIED");
        break;
      case SE_ERR_ASSOCINCOMPLETE:
        SPDLOG_DEBUG("SE_ERR_ASSOCINCOMPLETE");
        break;
      case SE_ERR_DDEBUSY:
        SPDLOG_DEBUG("SE_ERR_DDEBUSY");
        break;
      case SE_ERR_DDEFAIL:
        SPDLOG_DEBUG("SE_ERR_DDEFAIL");
        break;
      case SE_ERR_DDETIMEOUT:
        SPDLOG_DEBUG("SE_ERR_DDETIMEOUT");
        break;
      case SE_ERR_DLLNOTFOUND:
        SPDLOG_DEBUG("SE_ERR_DLLNOTFOUND");
        break;
      // The SE_ERR_NOASSOC error is returned when there is no default program
      // for the file extension. In modern Windows, when the verb "open" is
      // used, a dialogue is automatically presented to the user to specify the
      // desired program to open the file with.
      case SE_ERR_NOASSOC:
        SPDLOG_DEBUG("No associated program for the file specified was found.");
        break;
      case SE_ERR_OOM:
        SPDLOG_DEBUG("SE_ERR_OOM");
        break;
      case SE_ERR_SHARE:
        SPDLOG_DEBUG("SE_ERR_SHARE");
        break;
      default:
        SPDLOG_DEBUG("no case was selected");
        break;
    }
    return real_result;
  }
}

#endif /* FINDIR_SHELL_H */
