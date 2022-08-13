#ifndef FINDIR_CONFIG_H
#define FINDIR_CONFIG_H

#include <filesystem>
#include <set>
#include <string>
#define TOML11_PRESERVE_COMMENTS_BY_DEFAULT
#include <toml.hpp>
#include <vector>

#include "log.h"
#include "types.h"

const constexpr int MAXIMUM_FILE_PATH = 512;
using StringsContainer = std::set<std::string>;

// template <typename T, typename C>
StringsContainer
MakeContainer(std::vector<toml::value> container)
{
  StringsContainer out;
  for (auto& value : container) {
    out.insert(value.as_string());
  }
  return out;
}

std::string
GetFullPath(std::string filename)
{
  char full_path[MAXIMUM_FILE_PATH];
  auto result = GetModuleFileNameA(nullptr, full_path, MAXIMUM_FILE_PATH);
  std::filesystem::path path(full_path);
  auto file_path = path.parent_path().string() + "\\" + filename;
  SPDLOG_DEBUG(full_path);
  SPDLOG_DEBUG(file_path);
  return file_path;
}

namespace config {

class Settings
{
public:
  std::string file_name_ = "settings.toml";
  std::set<std::string> bookmarks = {};
  std::string default_search_path = "";
  bool use_text = false;
  bool use_recursion = false;
  int recursion_depth = 0;
  bool exit_on_search = true;

  Settings() = delete;
  /**
   * Loads toml user data into config class.
   * Will throw detailed errors from toml library if any of the following occur
   *   - std::runtime_error - failed to open file
   *   - toml::syntax_error - failed to parse file into toml object
   *   - toml::type_error - failed conversion from 'toml::find'
   *   - std::out_of_range - a table or value is not found
   */
  Settings(std::string filename, bool default_construct = false)
  {
    file_name_ = GetFullPath(filename);

    if (default_construct) {
      // Return default constructed settings with a filename.
      // This  settings object assumes it has a filename.
      return;
    } else {
      // Parse the main file
      const auto data = toml::parse<toml::preserve_comments>(file_name_);

      exit_on_search = toml::find_or<bool>(data, "exit_on_search", true);
      use_text = toml::find_or<bool>(data, "use_text", false);
      use_recursion = toml::find_or<bool>(data, "use_recursion", false);
      recursion_depth = toml::find_or<int>(data, "recursion_depth", 0);
      default_search_path =
        toml::find_or<std::string>(data, "default_search_path", "");

      auto& paths = toml::find(data, "bookmarks").as_array();
      bookmarks = MakeContainer(paths);
    }
  }

  /**
   * Save current user data to disk.
   * Builds a toml object.
   * Uses toml supplied serializer via ostream operators to write to file.
   */
  void Save()
  {
    const toml::value top_table{
      { "exit_on_search", exit_on_search },
      { "use_text", use_text },
      { "use_recursion", use_recursion },
      { "recursion_depth", recursion_depth },
      { "default_search_path", default_search_path },
      { "bookmarks", bookmarks },
    };

    std::fstream file(file_name_, std::ios_base::out);
    file << top_table << std::endl;
    file.close();
  }

  void AddBookmark(std::string path) { bookmarks.insert(path); }

  std::vector<std::string> GetBookmarks()
  {
    std::vector<std::string> r(bookmarks.begin(), bookmarks.end());
    return r;
  }
};

using ConfigReturn = struct
{
  bool success;
  std::string msg;
  Settings settings;
};

ConfigReturn
LoadFromFile(std::string filename)
{
  std::string err_msg = "lorem ipsum";
  try {
    // return a successfully parsed and validated config file
    return ConfigReturn{ true, "", Settings(filename, false) };
  } catch (const toml::syntax_error& ex) {
    err_msg = std::format(
      "Syntax error in toml file: \"{}\"\nSee error message below for hints "
      "on how to fix.\n{}",
      filename,
      ex.what());
  } catch (const toml::type_error& ex) {
    err_msg = std::format("Incorrect type when parsing toml file \"{}\".\n\n{}",
                          filename,
                          ex.what());
  } catch (const std::out_of_range& ex) {
    err_msg = std::format(
      "Missing data in toml file \"{}\".\n\n{}", filename, ex.what());
  } catch (const std::runtime_error& ex) {
    // err_msg = std::format("Failed to open \"{}\"", filename);
    //  I want to ignore when the file was missing and return default blank
    //  config
    return ConfigReturn{ true, err_msg, Settings(filename, true) };
  } catch (...) {
    err_msg = std::format(
      "Exception has gone unhandled loading \"{}\" and verifying values.",
      filename);
  }
  // return a default config object with a message explaining failure
  return ConfigReturn{ false, err_msg, Settings(filename, true) };
}
} // namespace config
#endif /* FINDIR_CONFIG_H */
