#pragma once

#include "Utils.hpp"

#include "RISCVModel/RVM.h"

#include <inttypes.h>

#include <fesvr/config.h>

#include <riscv/decode.h>
#include <riscv/devices.h>
#include <riscv/mmu.h>
#include <riscv/sim.h>

#include <fesvr/device.h>
#include <fesvr/memif.h>
#include <limits>

#include <memory>

namespace rvm {

using mem_region_t = std::pair<reg_t, std::unique_ptr<mem_t>>;

struct maybe_illegal_mem_cfg_t {
  maybe_illegal_mem_cfg_t(reg_t base, reg_t size) : base(base), size(size) {}

  reg_t base;
  reg_t size;

  reg_t get_base() const { return base; }
  reg_t get_size() const { return size; }
  reg_t get_inclusive_end() const { return base + size - 1; }
};

struct invalid_mem_cfg_t : std::exception {
  std::string error_msg;

  invalid_mem_cfg_t(::RVMMemoryRegion cfg, std::string_view error);

  const char *what() const noexcept { return error_msg.c_str(); }
};

class Simulator final : public simif_t, public chunked_memif_t {
public:
  // RESET_MCAUSE would be the default for MCAUSE CSR
  static constexpr uint64_t RESET_MCAUSE = 0x7fffffff;

  Simulator(const char *isa, bool enable_misaligned_access,
            const std::vector<::RVMMemoryRegion> &dram_config,
            rvm_utils::Logger &&logger, rvm_utils::Logger &&debug_logger);
  ~Simulator() = default;

  RVMErrorCode set_pc_reg(uint64_t pc);
  uint64_t get_pc_reg() const;

  RVMErrorCode set_gpr(unsigned i, uint64_t value);
  RVMErrorCode get_gpr(unsigned i, RVMRegT &Val) const;

  RVMErrorCode set_fpr(unsigned i, uint64_t value);
  RVMErrorCode get_fpr(unsigned i, RVMRegT &Val) const;

  RVMErrorCode set_vreg(unsigned i, const char *Data, size_t &MaxSize);
  RVMErrorCode get_vreg(unsigned i, char *Data, size_t &MaxSize) const;

  RVMErrorCode set_csr(unsigned Address, RVMRegT Val);
  RVMErrorCode get_csr(unsigned Address, RVMRegT &Val) const;
  RVMErrorCode raise_interrupt(uint64_t Cause);
  RVMErrorCode clear_interrupt(uint64_t Cause);

  void reset();

  reg_t get_entry_point() const { return entry; }
  reg_t get_sym_end() const { return end_addr; }

  RVMErrorCode set_end_pc(uint64_t pc);
  void set_stop_mode(RVMStopMode mode) { stop_mode = mode; }
  RVMStopMode set_stop_mode() const { return stop_mode; }

  RVMSimExecStatus step(size_t n);

  /* simif_t */
  char *addr_to_mem(reg_t addr) override;

  bool mmio_load(reg_t, size_t, uint8_t *) override;
  bool mmio_store(reg_t, size_t, const uint8_t *) override;
  void proc_reset(unsigned) override {
    // we don't need the implementation of this callback
  }
  virtual const cfg_t &get_cfg() const override { return *sim_cfg; }
  const char *get_symbol(uint64_t) override { return nullptr; }

  /* chunked_memif_t */
  static constexpr std::size_t max_chunk_size = 8;

  size_t chunk_align() override { return max_chunk_size; }
  size_t chunk_max_size() override { return max_chunk_size; }
  void read_chunk(addr_t taddr, size_t len, void *dst) override;
  void write_chunk(addr_t taddr, size_t len, const void *src) override;
  void clear_chunk(addr_t taddr, size_t len) override;

  memif_t &imem() { return memif; }

  RVMErrorCode check_memory_access(addr_t taddr, size_t len) const;
  RVMErrorCode check_memory_access(commit_log_mem_t::const_iterator begin,
                                   commit_log_mem_t::const_iterator end) const;

  unsigned getVLENB() const;

  void notifyOnStateUpdate();

  void setCallbackHandler(RVMCallbackHandler *Handler);
  void setMemReadCallback(MemReadCallbackTy Callback);
  void setMemUpdateCallback(MemUpdateCallbackTy Callback);
  void setXRegUpdateCallback(XRegUpdateCallbackTy Callback);
  void setFRegUpdateCallback(FRegUpdateCallbackTy Callback);
  void setVRegUpdateCallback(VRegUpdateCallbackTy Callback);
  void setCSRUpdateCallback(CSRUpdateCallbackTy Callback);
  void setPCUpdateCallback(PCUpdateCallbackTy Callback);

  rvm_utils::Logger &get_logger() { return Log; }
  rvm_utils::Logger &get_debug_logger() { return DebugLog; }
  void get_error_context(char *Buf, size_t *BufSize) const {
    assert(BufSize);
    auto CtxSize = RVMErrorContext.size();
    if (!Buf) {
      *BufSize = CtxSize;
      return;
    }
    auto Count = std::min(CtxSize, *BufSize);
    std::copy_n(RVMErrorContext.begin(), Count, Buf);
    if (Count < CtxSize)
      *BufSize = CtxSize;
  }

private:
  // In RV32 mode, simulator may return junk in the 32 MSBs in PC and in XRegs.
  // Note: RV128 is not supported.
  uint64_t cut_extra_msbs(const uint64_t &regval) const {
    if (proc.back()->get_xlen() == 32)
      return regval & UINT32_MAX;
    return regval;
  }

  constexpr static const char *kSymEnd = "_sim_end";
  constexpr static uint64_t kDefaultStartPC = 0x80000000;
  constexpr static uint64_t kDefaultEndPC = 0x1;
  constexpr static unsigned kNumRegs = 32;

  std::unique_ptr<cfg_t> sim_cfg;
  std::unique_ptr<isa_parser_t> isa;
  std::unique_ptr<mmu_t> debug_mmu;
  std::vector<std::unique_ptr<processor_t>> proc;
  std::map<size_t, processor_t *> proc_map;
  virtual const std::map<size_t, processor_t *> &get_harts() const override {
    return proc_map;
  }

  std::ostream sout_ = std::ostream(nullptr);

  rvm_utils::Logger Log;
  rvm_utils::Logger DebugLog;
  // mutable to be able to return error from const methods
  mutable std::string RVMErrorContext;

  std::vector<mem_region_t> dram_storage;
  std::vector<maybe_illegal_mem_cfg_t> mem_regs;

  // memory access and MMIO
  memif_t memif;
  bus_t bus;

  // execution
  reg_t entry = kDefaultStartPC;
  reg_t end_addr = kDefaultEndPC;
  uint64_t end_pc = std::numeric_limits<uint64_t>::max();
  RVMStopMode stop_mode = RVM_STOP_NEVER;

  RVMCallbackHandler *CallbackHandler;
  MemReadCallbackTy MemReadCallback;
  MemUpdateCallbackTy MemUpdateCallback;
  XRegUpdateCallbackTy XRegUpdateCallback;
  FRegUpdateCallbackTy FRegUpdateCallback;
  VRegUpdateCallbackTy VRegUpdateCallback;
  CSRUpdateCallbackTy CSRUpdateCallback;
  PCUpdateCallbackTy PCUpdateCallback;

  // miscellaneous
  debug_module_config_t dm_config = {.progbufsize = 2,
                                     .max_sba_data_width = 0,
                                     .require_authentication = false,
                                     .abstract_rti = 0,
                                     .support_hasel = true,
                                     .support_abstract_csr_access = true,
                                     .support_abstract_fpr_access = true,
                                     .support_haltgroups = true,
                                     .support_impebreak = true};
};

} // namespace rvm
