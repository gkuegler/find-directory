#ifndef FINDIR_LOG_H
#define FINDIR_LOG_H
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

void
SetUpLogging()
{
  auto logger = spdlog::basic_logger_mt("main", "log.txt", true);
  logger->set_level(spdlog::level::trace);
  spdlog::set_default_logger(std::move(logger));
  spdlog::flush_every(std::chrono::seconds(1));
  spdlog::info("----- start of log file ------");
}

void
FlushLogging()
{
  spdlog::get("main")->flush();
}

#endif /* FINDIR_LOG_H */
