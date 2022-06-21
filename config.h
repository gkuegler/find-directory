#ifndef FINDIR_CONFIG_H
#define FINDIR_CONFIG_H

//#include <fmt/format.h>

#include <set>
#include <string>
#define TOML11_PRESERVE_COMMENTS_BY_DEFAULT
#include <toml.hpp>
#include <vector>

#include "types.h"

using StringsContainer = std::set<std::string>;

// template <typename T, typename C>
StringsContainer MakeContainer(std::vector<toml::value> container) {
  StringsContainer out;
  for (auto &value : container) {
    out.insert(value.as_string());
  }
  return out;
}

namespace config {

class Settings {
 public:
  std::string file_name_ = "settings.toml";
  std::set<std::string> bookmarks = {};
  std::string default_search_path = "";
  bool use_text = false;
  bool use_recursion = false;
  int recursion_depth = 0;

  /**
   * Loads toml user data into config class.
   * Will throw detailed errors from toml library if any of the following occur
   *   - std::runtime_error - failed to open file
   *   - toml::syntax_error - failed to parse file into toml object
   *   - toml::type_error - failed conversion from 'toml::find'
   *   - std::out_of_range - a table or value is not found
   */
  Settings(std::string filename) {
    file_name_ = filename;

    // Parse the main file
    const auto data = toml::parse<toml::preserve_comments>(filename);

    use_text = toml::find<bool>(data, "use_text");
    use_recursion = toml::find<bool>(data, "use_recursion");
    recursion_depth = toml::find<int>(data, "recursion_depth");
    default_search_path = toml::find<std::string>(data, "default_search_path");

    auto &paths = toml::find(data, "bookmarks").as_array();
    bookmarks = MakeContainer(paths);
  }
  Settings(){};

  /**
   * Save current user data to disk.
   * Builds a toml object.
   * Uses toml supplied serializer via ostream operators to write to file.
   */
  void Save() {
    const toml::value top_table{
        {"use_text", use_text},
        {"use_recursion", use_recursion},
        {"recursion_depth", recursion_depth},
        {"default_search_path", default_search_path},
        {"bookmarks", bookmarks},
    };

    std::fstream file(file_name_, std::ios_base::out);
    file << top_table << std::endl;
    file.close();
  }

  void AddBookmark(std::string path) { bookmarks.insert(path); }

  std::vector<std::string> GetBookmarks() {
    std::vector<std::string> r(bookmarks.begin(), bookmarks.end());
    return r;
  }
};

using ConfigReturn = struct {
  bool success;
  std::string msg;
  Settings settings;
};

ConfigReturn LoadFromFile(std::string filename) {
  std::string err_msg = "lorem ipsum";
  try {
    auto settings = Settings(filename);
    // return a successfully parsed and validated config file
    return ConfigReturn{true, "", settings};
  } catch (const toml::syntax_error &ex) {
    err_msg = std::format(
        "Syntax error in toml file: \"{}\"\nSee error message below for hints "
        "on how to fix.\n{}",
        filename, ex.what());
  } catch (const toml::type_error &ex) {
    err_msg = std::format("Incorrect type when parsing toml file \"{}\".\n\n{}",
                          filename, ex.what());
  } catch (const std::out_of_range &ex) {
    err_msg = std::format("Missing data in toml file \"{}\".\n\n{}", filename,
                          ex.what());
  } catch (const std::runtime_error &ex) {
    // err_msg = std::format("Failed to open \"{}\"", filename);
    //  I want to ignore when the file was missing and return default blank
    //  config
    // TODO: this is a bug for default constructed settings objects, filename!
    return ConfigReturn{true, err_msg, Settings()};
  } catch (...) {
    err_msg = std::format(
        "Exception has gone unhandled loading \"{}\" and verifying values.",
        filename);
  }

  // return a default config object with a message explaining failure
  // TODO: this is a bug for default constructed settings objects, filename!
  return ConfigReturn{false, err_msg, Settings()};
}
}  // namespace config
#endif /* FINDIR_CONFIG_H */
