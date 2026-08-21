#include "Simulator.hpp"
#include "RISCVModel/RVM.h"

#include <algorithm>
#include <decode_macros.h>
#include <fesvr/elfloader.h>

#include <PrintUtils.hpp>
#include <Utils.hpp>

#include <array>
#include <climits>
#include <limits>
#include <string_view>

namespace rvm {
namespace {

using rvm::maybe_illegal_mem_cfg_t;

// FIXME: it's a copy-paste from spike_main/spike.cc
static bool sort_mem_region(const maybe_illegal_mem_cfg_t &a,
                            const maybe_illegal_mem_cfg_t &b) {
  if (a.get_base() == b.get_base())
    return (a.get_size() < b.get_size());
  else
    return (a.get_base() < b.get_base());
}

static bool check_mem_overlap(const maybe_illegal_mem_cfg_t &L,
                              const maybe_illegal_mem_cfg_t &R) {
  // We also want to merge consecutive memory regions.
  return std::max(L.get_base(), R.get_base()) <=
         std::min(L.get_base() + L.get_size(), R.get_base() + R.get_size());
}

// FIXME: it's a copy-paste from spike_main/spike.cc
static bool
check_if_merge_covers_64bit_space(const maybe_illegal_mem_cfg_t &L,
                                  const maybe_illegal_mem_cfg_t &R) {
  if (!check_mem_overlap(L, R))
    return false;

  auto start = std::min(L.get_base(), R.get_base());
  auto end = std::max(L.get_inclusive_end(), R.get_inclusive_end());

  return (start == 0ull) && (end == std::numeric_limits<uint64_t>::max());
}

// FIXME: it's a copy-paste from spike_main/spike.cc
static maybe_illegal_mem_cfg_t
merge_mem_regions(const maybe_illegal_mem_cfg_t &L,
                  const maybe_illegal_mem_cfg_t &R) {
  // one can merge only intersecting regions
  assert(check_mem_overlap(L, R));

  const auto merged_base = std::min(L.get_base(), R.get_base());
  const auto merged_end_incl =
      std::max(L.get_inclusive_end(), R.get_inclusive_end());
  const auto merged_size = merged_end_incl - merged_base + 1;

  return maybe_illegal_mem_cfg_t(merged_base, merged_size);
}

// FIXME: it's a copy-paste from spike_main/spike.cc
// check the user specified memory regions and merge the overlapping or
// eliminate the containing parts
static std::vector<maybe_illegal_mem_cfg_t>
merge_overlapping_memory_regions(std::vector<maybe_illegal_mem_cfg_t> mems) {
  if (mems.empty())
    return {};

  std::sort(mems.begin(), mems.end(), sort_mem_region);

  std::vector<maybe_illegal_mem_cfg_t> merged_mem;
  merged_mem.push_back(mems.front());

  for (auto mem_it = std::next(mems.begin()); mem_it != mems.end(); ++mem_it) {
    const auto &mem_int = *mem_it;
    if (!check_mem_overlap(merged_mem.back(), mem_int)) {
      merged_mem.push_back(mem_int);
      continue;
    }
    // there is a weird corner case preventing two memory regions from being
    // merged: if the resulting size of a region is 2^64 bytes - currently,
    // such regions are not representable by mem_cfg_t class (because the
    // actual size field is effectively a 64 bit value)
    // so we create two smaller memory regions that total for 2^64 bytes as
    // a workaround
    if (check_if_merge_covers_64bit_space(merged_mem.back(), mem_int)) {
      merged_mem.clear();
      merged_mem.push_back(maybe_illegal_mem_cfg_t(0ull, 0ull - PGSIZE));
      merged_mem.push_back(maybe_illegal_mem_cfg_t(0ull - PGSIZE, PGSIZE));
      break;
    }
    merged_mem.back() = merge_mem_regions(merged_mem.back(), mem_int);
  }

  return merged_mem;
}

std::vector<rvm::mem_region_t> make_mems(const std::vector<mem_cfg_t> &layout) {
  std::vector<maybe_illegal_mem_cfg_t> arg;
  for (auto region : layout)
    arg.push_back({region.get_base(), region.get_size()});
  auto merged_layout = merge_overlapping_memory_regions(std::move(arg));
  std::vector<rvm::mem_region_t> mems;
  mems.reserve(merged_layout.size());
  std::transform(merged_layout.begin(), merged_layout.end(),
                 std::back_inserter(mems), [](const auto &cfg) {
                   size_t size = cfg.get_inclusive_end() - cfg.get_base() + 1;
                   return std::make_pair(cfg.get_base(),
                                         std::make_unique<mem_t>(size));
                 });
  return mems;
}

static uint64_t align_mem_down(uint64_t addr) {
  return (addr >> PGSHIFT) << PGSHIFT;
}

static std::string print_memory_region(uint64_t start, uint64_t sz) {
  std::ostringstream OS;
  OS << rvm_utils::tohs(start) << " 0x" << std::hex << sz;
  return OS.str();
}

static mem_cfg_t align_mem_cfg(const ::RVMMemoryRegion &mem,
                               rvm_utils::Logger &debug_logger) {
  uint64_t origin_base = mem.Start;
  uint64_t origin_size = mem.Size;
  uint64_t origin_end = origin_base + origin_size;

  // If origin_end overflowed to 0, that means the region covers the whole 64
  // bit address space.
  if (origin_end < origin_base && origin_end > 0)
    throw invalid_mem_cfg_t(mem, "memory region is too large");

  uint64_t aligned_base = align_mem_down(origin_base);
  assert(aligned_base <= origin_base);
  uint64_t aligned_end = align_mem_down(origin_end);

  // If we had to align down.
  if (aligned_end < origin_end)
    // This can overflow to zero and that's ok.
    aligned_end += PGSIZE;

  uint64_t aligned_size = aligned_end - aligned_base;

  SPIKE_DEBUG(debug_logger, [&](auto &os) {
    if ((aligned_base != origin_base) || (aligned_size != origin_size)) {
      os << "warning: unaligned memory region specified for "
         << (mem.Name ? mem.Name : "<unnamed>") << ":\n";
      os << "  requested - " << print_memory_region(mem.Start, mem.Size)
         << "\n";
      os << "  adjusted  - " << print_memory_region(aligned_base, aligned_size)
         << "\n";
    }
  });

  if (!mem_cfg_t::check_if_supported(aligned_base, aligned_size))
    throw invalid_mem_cfg_t(
        mem, "adjusted memory region is unexpectedly unsupported");

  return {aligned_base, aligned_size};
}

// NOTE: this one is taken from C++20
template <class To, class From>
std::enable_if_t<sizeof(To) == sizeof(From) &&
                     std::is_trivially_copyable_v<From> &&
                     std::is_trivially_copyable_v<To>,
                 To>
// constexpr support needs compiler magic
std_bit_cast(const From &src) noexcept {
  static_assert(std::is_trivially_constructible_v<To>,
                "This implementation additionally requires "
                "destination type to be trivially constructible");

  To dst;
  std::memcpy(&dst, &src, sizeof(To));
  return dst;
}

std::string serialize_trap(const trap_t &c_trap) {
  // trap_t do not have proper const qualifiers
  trap_t &trap = const_cast<trap_t &>(c_trap);
  std::ostringstream out_s;
  out_s << trap.name() << " (";
  out_s << "gva: " << trap.has_gva();

  auto dump_optional_with_prefix = [&out_s](const char *prefix, bool present,
                                            reg_t value) {
    out_s << ", " << prefix << ": ";
    if (present) {
      out_s << rvm_utils::tohs(value);
    } else {
      out_s << "missing";
    }
  };
  dump_optional_with_prefix("tval", trap.has_tval(), trap.get_tval());
  dump_optional_with_prefix("tval2", trap.has_tval2(), trap.get_tval2());
  dump_optional_with_prefix("tval", trap.has_tinst(), trap.get_tinst());
  out_s << ")";
  return out_s.str();
}

} // anonymous namespace

invalid_mem_cfg_t::invalid_mem_cfg_t(::RVMMemoryRegion cfg,
                                     std::string_view error) {
  std::ostringstream ss;
  ss << "invalid memory region specified ("
     << (cfg.Name ? cfg.Name : "<unnamed>") << " "
     << print_memory_region(cfg.Start, cfg.Size) << "): " << error;
  error_msg = ss.str();
}

Simulator::Simulator(const char *isa_str, bool allow_misaligned_access,
                     const std::vector<::RVMMemoryRegion> &dram_config,
                     rvm_utils::Logger &&logger,
                     rvm_utils::Logger &&debug_logger)
    : Log(std::move(logger)), DebugLog(std::move(debug_logger)), memif(this) {

  if (!isa_str) {
    isa_str = "RV64IMAFDCV";
    std::cerr << "warning: ISA_STRING is not specified, defaulting to <"
              << isa_str << ">\n";
  }

  sout_.rdbuf(std::cerr.rdbuf());

  std::vector<size_t> hart_ids = {{0}};
  std::string real_isa = isa_str;
  // FIXME: add zicclsm to RVM interface and remove this hack
  if (allow_misaligned_access)
    real_isa += "_zicclsm";
  SPIKE_DEBUG(DebugLog,
              [&](auto &os) { os << "ISA_STRING: " << real_isa << "\n"; });
  SPIKE_DEBUG(DebugLog,
              [&](auto &os) { os << "PRIV_LEVEL: " << DEFAULT_PRIV << "\n"; });

  std::vector<mem_cfg_t> aligned_memory_layout = {};
  std::transform(dram_config.begin(), dram_config.end(),
                 std::back_inserter(aligned_memory_layout),
                 [this](auto &m) { return align_mem_cfg(m, DebugLog); });

  for (auto &mem_region : dram_config) {
    aligned_memory_layout.push_back(align_mem_cfg(mem_region, DebugLog));
    mem_regs.push_back({mem_region.Start, mem_region.Size});
  }

  mem_regs = merge_overlapping_memory_regions(mem_regs);

  sim_cfg = std::make_unique<cfg_t>();
  sim_cfg->initrd_bounds = std::make_pair((reg_t)0, (reg_t)0);
  sim_cfg->bootargs = nullptr;

  // TODO: this should not dangle since processor_t's constructor copies
  // this string before real_isa is destroyed
  sim_cfg->isa = real_isa.c_str();
  sim_cfg->priv = DEFAULT_PRIV;
  sim_cfg->endianness = endianness_little;
  sim_cfg->pmpregions = 16;
  sim_cfg->pmpgranularity = (1 << PMP_SHIFT);
  sim_cfg->mem_layout = aligned_memory_layout;
  (void)sim_cfg->start_pc;
  sim_cfg->hartids = hart_ids;
  (void)sim_cfg->explicit_hartids;
  sim_cfg->real_time_clint = false;
  sim_cfg->trigger_count = 8;
  sim_cfg->cache_blocksz = 64;

  debug_mmu = std::make_unique<mmu_t>(this, sim_cfg->endianness, nullptr,
                                      sim_cfg->cache_blocksz);

  proc.push_back(std::make_unique<processor_t>(
      sim_cfg.get()->isa, sim_cfg.get()->priv, sim_cfg.get(), this,
      0 /*hart index*/, false /*halted*/, Log.get_fd_unsafe(), sout_));

  proc_map[0] = proc.back().get();

  dram_storage = make_mems(aligned_memory_layout);
  for (const auto &mem : dram_storage)
    bus.add_device(mem.first, mem.second.get());

  for (auto &hart : proc) {
    // We always need logged write/read. Note that these don't actually log
    // anything to the stderr/stdout.
    hart->enable_log_commits();
    hart->set_debug(Log.is_enabled());
    hart->set_pmp_num(16);
#define SPIKE_MODEL_MMU_SV57_VA_MAX_BITS 57
#define SPIKE_MODEL_MMU_SV32_VA_MAX_BITS 32
    switch(hart->get_xlen()) {
      case 64:
        hart->set_max_vaddr_bits(SPIKE_MODEL_MMU_SV57_VA_MAX_BITS);
        break;
      case 32:
        hart->set_max_vaddr_bits(SPIKE_MODEL_MMU_SV32_VA_MAX_BITS);
        break;
    }
#undef SPIKE_MODEL_MMU_SV57_VA_MAX_BITS
#undef SPIKE_MODEL_MMU_SV32_VA_MAX_BITS
  }
}

static bool uvalue_fits(uint64_t value, unsigned width) {
  assert(width > 0 && width <= 64);
  uint64_t Max = [=] {
    if (width == 64)
      return ~uint64_t{0}; // Avoids overflow from shifting 1ULL << 64
    return (uint64_t{1} << width) - 1;
  }();
  return value <= Max;
}

RVMErrorCode Simulator::set_pc_reg(uint64_t pc) {
  if (!uvalue_fits(pc, proc.back()->get_const_xlen())) {
    std::stringstream ss;
    ss << "Value 0x" << std::hex << pc
       << " is too wide to write to the 32-bit PC register";
    RVMErrorContext = ss.str();
    return RVM_ERRC_INVALID_ADDRESS;
  }
  proc.back()->get_state()->pc = pc;
  return RVM_ERRC_SUCCESS;
}

uint64_t Simulator::get_pc_reg() const {
  return cut_extra_msbs(proc.back()->get_state()->pc);
}

RVMErrorCode Simulator::set_end_pc(uint64_t pc) {
  if (!uvalue_fits(pc, proc.back()->get_const_xlen())) {
    std::stringstream ss;
    ss << "Value 0x" << std::hex << pc
       << " is too wide to write to the 32-bit PC register";
    RVMErrorContext = ss.str();
    return RVM_ERRC_INVALID_ADDRESS;
  }
  end_addr = pc;
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::set_gpr(unsigned i, uint64_t value) {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect X register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  if (!uvalue_fits(value, proc.back()->get_const_xlen())) {
    std::stringstream ss;
    ss << "Attempt to assign 64-bit value 0x" << std::hex << value
       << " to X register";
    RVMErrorContext = ss.str();
    return RVM_ERRC_VALUE_OUT_OF_RANGE;
  }
  proc.back()->get_state()->XPR.write(i, value);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::get_gpr(unsigned i, RVMRegT &Val) const {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect X register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  Val = cut_extra_msbs(proc.back()->get_state()->XPR[i]);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::set_fpr(unsigned i, uint64_t value) {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect F register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  if (!(uvalue_fits(value, proc.back()->get_flen()))) {
    std::stringstream ss;
    ss << "attempt to assign 64-bit value 0x" << std::hex << value
       << " to F register when D extension is not enabled";
    RVMErrorContext = ss.str();
    return RVM_ERRC_VALUE_OUT_OF_RANGE;
  }

  freg_t fp128_v;
  if (proc.back()->get_flen() == 32)
    fp128_v = freg(f32(value));
  else
    fp128_v = freg(f64(value));
  proc.back()->get_state()->FPR.write(i, fp128_v);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::get_fpr(unsigned i, RVMRegT &Val) const {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect F register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  auto raw_fpr_value = proc.back()->get_state()->FPR[i];
  Val = std_bit_cast<uint64_t>(raw_fpr_value.v[0]);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::set_vreg(unsigned i, const char *Data,
                                 size_t &MaxSize) {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect V register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  auto &VU = proc.back()->VU;
  if (!Data) {
    MaxSize = VU.vlenb;
    return RVM_ERRC_SUCCESS;
  }
  char *reg_file = reinterpret_cast<char *>(VU.reg_file);
  char *reg_loc = reg_file + (VU.vlenb * i);
  size_t bytes_to_write = std::min(MaxSize, VU.vlenb);
  std::memcpy(reg_loc, Data, bytes_to_write);
  if (VU.vlenb < MaxSize) {
    std::stringstream ss;
    ss << "given incorrect MaxSize: got " << MaxSize
       << " but vector register width is " << VU.vlenb << '\n';
    RVMErrorContext = ss.str();
    MaxSize = VU.vlenb;
    return RVM_ERRC_VALUE_OUT_OF_RANGE;
  }
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::get_vreg(unsigned i, char *Data,
                                 size_t &MaxSize) const {
  if (i >= kNumRegs) {
    std::stringstream ss;
    ss << "incorrect V register index <" << i << ">: expected values from 0 to "
       << kNumRegs << "\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_IDX_OUT_OF_RANGE;
  }
  const auto &VU = proc.back()->VU;
  if (!Data) {
    MaxSize = VU.vlenb;
    return RVM_ERRC_SUCCESS;
  }
  const char *reg_file = reinterpret_cast<const char *>(VU.reg_file);
  const char *reg_loc = reg_file + (VU.vlenb * i);
  size_t bytes_to_read = std::min(MaxSize, VU.vlenb);
  std::memcpy(Data, reg_loc, bytes_to_read);
  if (VU.vlenb > MaxSize) {
    std::stringstream ss;
    ss << "given incorrect MaxSize: got " << MaxSize
       << " but vector register width is " << VU.vlenb << '\n';
    RVMErrorContext = ss.str();
    MaxSize = VU.vlenb;
    return RVM_ERRC_VALUE_OUT_OF_RANGE;
  }
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::set_csr(unsigned Address, RVMRegT value) {
  auto &csrmap = proc.back()->get_state()->csrmap;
  auto csr_it = csrmap.find(Address);
  if (csr_it == csrmap.end()) {
    std::stringstream ss;
    ss << "set_csr: unsupported CSR <0x" << std::hex << Address << ">\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_INVALID_ADDRESS;
  }
  csr_it->second->write(value);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::get_csr(unsigned Address, RVMRegT &Val) const {
  auto &csrmap = proc.back()->get_state()->csrmap;
  auto csr_it = csrmap.find(Address);
  if (csr_it == csrmap.end()) {
    std::stringstream ss;
    ss << "set_csr: unsupported CSR 0x" << std::hex << Address << ">\n";
    RVMErrorContext = ss.str();
    return RVM_ERRC_INVALID_ADDRESS;
  }
  Val = csr_it->second->read();
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::raise_interrupt(uint64_t Cause) {
  auto Err = set_csr(CSR_MCAUSE, Cause);
  if (Err != RVM_ERRC_SUCCESS)
    return Err;
  proc.back()->get_state()->mip->write_with_mask(MIP_MEIP, MIP_MEIP);
  proc.back()->get_state()->mie->write_with_mask(MIP_MEIP, MIP_MEIP);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::clear_interrupt(uint64_t Cause) {
  auto Err = set_csr(CSR_MCAUSE, Cause);
  if (Err != RVM_ERRC_SUCCESS)
    return Err;
  proc.back()->get_state()->mip->write_with_mask(MIP_MEIP, 0);
  return RVM_ERRC_SUCCESS;
}

void Simulator::reset() {
  for (auto &hart : proc)
    hart->reset();
  [[maybe_unused]] auto Err = set_csr(CSR_MCAUSE, RESET_MCAUSE);
  assert(Err == RVM_ERRC_SUCCESS);
}

unsigned Simulator::getVLENB() const { return proc.back()->VU.VLEN / CHAR_BIT; }

void Simulator::setCallbackHandler(RVMCallbackHandler *Handler) {
  CallbackHandler = Handler;
}

void Simulator::setMemReadCallback(MemReadCallbackTy Callback) {
  MemReadCallback = Callback;
}

void Simulator::setMemUpdateCallback(MemUpdateCallbackTy Callback) {
  MemUpdateCallback = Callback;
}

void Simulator::setXRegUpdateCallback(XRegUpdateCallbackTy Callback) {
  XRegUpdateCallback = Callback;
}

void Simulator::setFRegUpdateCallback(FRegUpdateCallbackTy Callback) {
  FRegUpdateCallback = Callback;
}

void Simulator::setVRegUpdateCallback(VRegUpdateCallbackTy Callback) {
  VRegUpdateCallback = Callback;
}

void Simulator::setCSRUpdateCallback(CSRUpdateCallbackTy Callback) {
  CSRUpdateCallback = Callback;
}

void Simulator::setPCUpdateCallback(PCUpdateCallbackTy Callback) {
  PCUpdateCallback = Callback;
}

static std::string report_invalid_memory_access(uint64_t addr, uint8_t size) {
  std::stringstream ss;
  ss << "Invalid memory access (maybe it's trying to access areas "
        "outside of the configured memory regions):\n";
  ss << "Requested start address: " << rvm_utils::tohs(addr) << "\n";
  ss << "Requested size: " << rvm_utils::tohs(static_cast<unsigned>(size))
     << "\n";
  return ss.str();
}

static uint64_t get_address_for_xlen(uint64_t address, unsigned xlen) {
  assert(xlen == 32 || xlen == 64);
  if (xlen == 64)
    return address;
  return address & std::numeric_limits<uint32_t>::max();
}

void Simulator::notifyOnStateUpdate() {
  const auto *state = proc.back()->get_state();

  assert(state);

  auto Err = check_memory_access(state->log_mem_read.begin(),
                                 state->log_mem_read.end());
  assert(Err == RVM_ERRC_SUCCESS);
  Err = check_memory_access(state->log_mem_write.begin(),
                            state->log_mem_write.end());
  assert(Err == RVM_ERRC_SUCCESS);

  if (!CallbackHandler)
    return;

  if (PCUpdateCallback)
    PCUpdateCallback(CallbackHandler, get_pc_reg());

  if (MemReadCallback) {
    for (const auto &[addr, value, size] : state->log_mem_read) {
      // In spike we always read zero data:
      // proc->state.log_mem_read.push_back(make_tuple(original_addr, 0, len));
      std::array<char, sizeof(value)> data;
      auto xlen_addr =
          get_address_for_xlen(addr, proc.back()->get_const_xlen());
      memif.read(xlen_addr, size, data.data());
      MemReadCallback(CallbackHandler, xlen_addr, data.data(), size);
    }
  }

  if (MemUpdateCallback) {
    for (const auto &[addr, value, size] : state->log_mem_write) {
      auto data = std_bit_cast<std::array<char, sizeof(value)>>(value);
      MemUpdateCallback(CallbackHandler, addr, data.data(), size);
    }
  }

  std::vector<char> vec_data(getVLENB());
  for (const auto &[reg, value] : state->log_reg_write) {
    // Skip X0.
    if (reg == 0)
      continue;
    auto regNo = reg >> 4;
    // IMPORTANT: These case numbers should be kept in sync with those listed
    // in riscv/decode_macros.h above the WRITE_REG macro.
    // FIXME: make a reg class enum in core spike code
    switch (reg & 0xf) {
    case 0:
      if (XRegUpdateCallback)
        XRegUpdateCallback(CallbackHandler, static_cast<RVMXReg>(regNo),
                           value.v[0]);
      break;
    case 1:
      if (FRegUpdateCallback)
        FRegUpdateCallback(CallbackHandler, static_cast<RVMFReg>(regNo),
                           (proc.back()->get_flen() == 64
                                ? value.v[0]
                                : static_cast<uint32_t>(value.v[0])));
      break;
    case 2:
      if (VRegUpdateCallback) {
        size_t Size = vec_data.size();
        auto Err = get_vreg(regNo, vec_data.data(), Size);
        assert(Err == RVM_ERRC_SUCCESS);
        VRegUpdateCallback(CallbackHandler, static_cast<RVMVReg>(regNo),
                           reinterpret_cast<const char *>(vec_data.data()),
                           vec_data.size());
      }
      break;
    case 3:
      break;
    case 4:
      if(CSRUpdateCallback)
        CSRUpdateCallback(CallbackHandler, static_cast<RVMCSR>(regNo), value.v[0]);
      break;
    default:
      assert("can't be here" && 0);
      break;
    }
  }
}

RVMSimExecStatus Simulator::step(size_t n) {
  if (stop_mode == RVM_STOP_BY_PC && get_sym_end() == get_pc_reg())
    return RVM_STEP_FINISH;

  for (auto &hart : proc)
    hart->step(n);

  notifyOnStateUpdate();

  // This means that most likely we've got an exception
  RVMRegT current_mcause = 0;
  [[maybe_unused]] RVMErrorCode Err = get_csr(CSR_MCAUSE, current_mcause);
  assert(Err == RVM_ERRC_SUCCESS);
  if (current_mcause != RESET_MCAUSE) {
    RVMRegT current_mtvec = 0;
    Err = get_csr(CSR_MTVEC, current_mtvec);
    if (current_mtvec == get_pc_reg()) {
      return RVM_STEP_EXCEPTION;
    }
  }

  return RVM_STEP_SUCCESS;
}

char *Simulator::addr_to_mem(reg_t addr) {
  auto desc = bus.find_device(addr >> PGSHIFT << PGSHIFT, PGSIZE);
  if (auto *mem = dynamic_cast<mem_t *>(desc.second)) {
    if (addr - desc.first < mem->size())
      return mem->contents(addr - desc.first);
  }
  SPIKE_DEBUG(DebugLog, ([&, name = __PRETTY_FUNCTION__](auto &os) {
                os << name << ": could not get physical memory for "
                   << rvm_utils::tohs(addr) << "\n";
              }));
  return NULL;
}

bool Simulator::mmio_load(reg_t addr, size_t len, uint8_t *data) {
  return bus.load(addr, len, data);
}

bool Simulator::mmio_store(reg_t addr, size_t len, const uint8_t *data) {
  return bus.store(addr, len, data);
}

static std::string report_mem_chunk_ref(const char *prefix, addr_t taddr,
                                        size_t len) {
  std::ostringstream out_s;
  out_s << prefix << ": <" << rvm_utils::tohs(taddr) << ", " << len << ">";
  return out_s.str();
}

static void abort_if_overflow(uint64_t taddr, size_t len) {
  uint64_t res = taddr + len;
  if (res >= taddr || res == 0) [[likely]]
    return;
  std::cerr << "Got an overflowed memory access\nMemory Start: " << taddr
            << "\nRequested memory size: " << len
            << "\nOverflowed to: " << taddr + len << "\n";
  rvm_utils::report_fatal_error("Got an overflowed memory access");
}

static bool check_accessing_address(
    const std::vector<maybe_illegal_mem_cfg_t> &sorted_regions, addr_t taddr,
    size_t len) {
  // There's no legitimate case for zero-sized accesses.
  if (len == 0)
    return false;

  abort_if_overflow(taddr, len);

  auto it = std::upper_bound(
      sorted_regions.begin(), sorted_regions.end(), taddr,
      [](uint64_t start, const maybe_illegal_mem_cfg_t &region) {
        return start < region.get_base();
      });

  // This precondition should have been validated in modelCreate already. Let's
  // double-check just in case.
  assert(sorted_regions.size() > 0);

  // The binary search above finds the first region, such that region.get_base()
  // > taddr. This is the region after the last region that could contain the
  // memory access.
  if (it == sorted_regions.begin())
    // The first range already starts at an address larger than the start of the
    // memory access.
    return false;

  --it; // This is the actual range that should contain the memory access.
  return (taddr + len - 1 <= it->get_inclusive_end());
}

void Simulator::read_chunk(addr_t taddr, size_t len, void *dst) {
  SPIKE_DEBUG(DebugLog, ([&, name = __PRETTY_FUNCTION__](auto &os) {
                os << report_mem_chunk_ref(name, taddr, len) << "\n";
              }));
  assert(len == 8 && "read_chunk implementation expects only 8-byte requests");

  try {
    auto data = debug_mmu->load<uint64_t>(taddr);
    std::memcpy(dst, &data, sizeof(data));
  } catch (const trap_t &trap) {
    std::cerr << report_mem_chunk_ref(__PRETTY_FUNCTION__, taddr, len)
              << " failed with unexpected " << serialize_trap(trap) << "\n";
    rvm_utils::report_fatal_error("fatal error during read_chunk");
  }
}

void Simulator::write_chunk(addr_t taddr, size_t len, const void *src) {
  SPIKE_DEBUG(DebugLog, ([&, name = __PRETTY_FUNCTION__](auto &os) {
                os << report_mem_chunk_ref(name, taddr, len) << "\n";
              }));
  assert(len == 8 && "write_chunk implementation expects only 8-byte requests");
  uint64_t data;
  std::memcpy(&data, src, sizeof(data));
  try {
    debug_mmu->store<uint64_t>(taddr, data);
  } catch (const trap_t &trap) {
    std::cerr << report_mem_chunk_ref(__PRETTY_FUNCTION__, taddr, len)
              << " failed with unexpected " << serialize_trap(trap) << "\n";
    rvm_utils::report_fatal_error("fatal error during write_chunk");
  }
}

void Simulator::clear_chunk(addr_t taddr, size_t len) {
  uint64_t zeros = 0;
  for (size_t pos = 0; pos < len; pos += chunk_max_size())
    write_chunk(taddr + pos, std::min(len - pos, chunk_max_size()), &zeros);
}

RVMErrorCode
Simulator::check_memory_access(commit_log_mem_t::const_iterator begin,
                               commit_log_mem_t::const_iterator end) const {
  uint32_t xlen = proc.back()->get_const_xlen();
  const auto found_invalid_access =
      std::find_if(begin, end, [this, xlen](const auto &x) {
        auto &[start, _, len] = x;
        return !check_accessing_address(mem_regs,
                                        get_address_for_xlen(start, xlen), len);
      });
  if (found_invalid_access != end) {
    const auto [addr, _, size] = *found_invalid_access;
    RVMErrorContext = report_invalid_memory_access(addr, size);
    return RVM_ERRC_INVALID_ADDRESS;
  }
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode Simulator::check_memory_access(addr_t start,
                                            std::size_t size) const {
  uint32_t xlen = proc.back()->get_const_xlen();
  if (!check_accessing_address(mem_regs, get_address_for_xlen(start, xlen),
                               size)) {
    RVMErrorContext = report_invalid_memory_access(start, size);
    return RVM_ERRC_INVALID_ADDRESS;
  }
  return RVM_ERRC_SUCCESS;
}

} // namespace rvm
