#pragma once

// common routines for printing auxiliary infromation

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

namespace rvm_utils {

inline std::string printRegNumber(unsigned i) {
  std::ostringstream OS;
  OS << std::setw(2) << std::setfill('0') << i;
  return OS.str();
}

inline std::string tohs(uint64_t V, unsigned Width = 16) {
  std::ostringstream OS;
  OS << "0x";
  OS << std::hex << std::setw(Width) << std::setfill('0') << V;
  return OS.str();
}

inline std::string tohs(const std::vector<char> &V) {
  std::ostringstream OS;
  OS << "0x";
  for (auto CurIt = V.rbegin(), EndIt = V.rend(); CurIt != EndIt; ++CurIt) {
    unsigned char Byte = *CurIt;
    OS << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned>(Byte);
  }
  return OS.str();
}

} // namespace rvm_utils
