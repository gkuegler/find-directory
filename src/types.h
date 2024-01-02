#ifndef FINDIR_TYPES_H
#define FINDIR_TYPES_H

using Strings = std::vector<std::string>;

namespace message_code {
enum message_code_ {
  log_error,
  search_result,
  search_lump_results,
  search_finished
};
}  // namespace message_code

#endif /* FINDIR_TYPES_H */
