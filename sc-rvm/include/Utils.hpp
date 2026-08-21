// Common file to store utils that are used in several places
#ifndef SC_RVM_INCLUDE_UTILS_HPP
#define SC_RVM_INCLUDE_UTILS_HPP

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace rvm_utils {

class Logger final {
  FILE *fd = nullptr;
  static void close_if_needed(FILE *d) {
    if (d && d != stderr && d != stdout)
      std::fclose(d);
  }

public:
  Logger(const std::optional<std::string_view> &path = std::nullopt) {
    if (!path)
      return;
    if (path->empty()) {
      fd = stdout;
    } else if (*path == "-") {
      fd = stderr;
    } else {
      fd = std::fopen(std::string(*path).c_str(), "w");
      if (!fd) {
        std::cerr << "failed to open debug log file \"" << *path
                  << "\": " << std::strerror(errno) << "\n";
        exit(EXIT_FAILURE);
      }
    }
  }

  Logger(Logger &&other) { fd = std::exchange(other.fd, nullptr); };

  Logger &operator=(Logger &&rhs) {
    close_if_needed(fd);
    fd = std::exchange(rhs.fd, nullptr);
    return *this;
  }
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  ~Logger() { close_if_needed(fd); };

  template <typename F> void log_fn(F &&fn) {
    assert(fd);
    std::stringstream ss;
    fn(ss);
    std::fputs(ss.str().c_str(), fd);
  }
  bool is_enabled() const { return fd; }
  // Use carefully. Might cause data races
  FILE *get_fd_unsafe() { return fd; }
};

#define SPIKE_DEBUG(LOG, X)                                                    \
  do {                                                                         \
    if (LOG.is_enabled()) {                                                    \
      LOG.log_fn(X);                                                           \
    }                                                                          \
  } while (0)

// FIXME: Have some sort of error reporting facilities instead of this
// jank.
[[noreturn, gnu::noinline]] inline void
report_fatal_error(std::string Message) {
  std::cerr << "riscv-isa-sim error: " << Message << "\n";
  exit(EXIT_FAILURE);
}
} // namespace rvm_utils
#endif
