#include "RISCVModel/RVM.h"
#include "RISCVModel/RVM.hpp"

#include <riscv/cfg.h>
#include <riscv/sim.h>

#include <regex>
#include <stdbool.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include <dlfcn.h>

#include <iomanip>
#include <iostream>

#include <PrintUtils.hpp>

#include <Utils.hpp>

#include "Simulator.hpp"

#define TOSTRING(x) #x
const char *VERSION_SYMBOL_NAME = TOSTRING(GIT_COMMIT_HASH);
#undef TOSTRING

static const char *copyString(const char *Ptr) {
  if (!Ptr)
    return nullptr;
  auto InputLen = std::strlen(Ptr);
  char *Result = new char[InputLen + 1];
  std::strcpy(Result, Ptr);
  return Result;
}

static RVMConfig copyRVMConfig(const RVMConfig &ConfigIn) {
  auto Result = ConfigIn;
  Result.LogFilePath = copyString(ConfigIn.LogFilePath);
  return Result;
}

struct RVMState {
  RVMConfig Config;
  std::unique_ptr<rvm::Simulator> Sim;

  RVMState(const RVMConfig &ConfigIn, std::unique_ptr<rvm::Simulator> SimIn)
      : Config(copyRVMConfig(ConfigIn)), Sim(std::move(SimIn)) {}

  ~RVMState() { delete[] Config.LogFilePath; }
};
static std::string deriveIsaString(bool IsRV64,
                                   const RVMExtDescriptor &Extensions,
                                   unsigned VLEN, rvm_utils::Logger &DebugLog) {
  auto IsaString = rvm::create_isa_string(Extensions, IsRV64, /* Lowercase */ true);
  SPIKE_DEBUG(DebugLog, [&](auto &os) {
    os << "Derived ISA String: " << IsaString.c_str() << "\n";
  });
  // These days we can have vector register file without V extension.
  // Having zvlXXb in our isa string guarantees that spike allows accesses
  // to VXSAT registers
  if (VLEN) {
    // TODO: zveXXx is hard-coded. Probably, we should extend rvm interface
    unsigned SimulatedVLEN = VLEN;
    if (SimulatedVLEN == 0)
      SimulatedVLEN = 128;
    IsaString.append("_zvl")
        .append(std::to_string(SimulatedVLEN))
        .append("b_zve64x");
  }

  SPIKE_DEBUG(DebugLog, [&](auto &os) {
    os << "Derived ISA String: " << IsaString.c_str() << "\n";
  });

  std::transform(IsaString.begin(), IsaString.end(), IsaString.begin(),
                 [](auto c) { return std::tolower(c); });
  return IsaString;
}

extern "C" {

RVMState *rvm_modelCreate(const RVMConfig *ConfigPtr, RVMErrorCode *Err,
                          char *ErrBuf, size_t ErrBufSize) try {
  assert(ConfigPtr);
  const auto &Config = *ConfigPtr;

  std::optional<std::string> MaybeDebugLogPath;
  if (Config.DebugLogFilePath)
    MaybeDebugLogPath = Config.DebugLogFilePath;
  rvm_utils::Logger debug_logger(MaybeDebugLogPath);
  SPIKE_DEBUG(debug_logger, [](auto &os) { os << "creating simulator\n"; });

  assert(Config.MemoryRegions || Config.MemoryRegionCount == 0);

  auto IsaString = deriveIsaString(Config.RV64, Config.Extensions, Config.VLEN,
                                   debug_logger);

  // The user can disable logs by setting LogFilePath to nullptr. Useful
  // for improving the performance by avoiding unnecessary formatting and
  // disassembly.
  std::optional<std::string> MaybeLogFilePath;
  if (Config.LogFilePath)
    MaybeLogFilePath = Config.LogFilePath;

  auto Sim = std::make_unique<rvm::Simulator>(
      IsaString.c_str(), Config.EnableMisalignedAccess,
      std::vector<::RVMMemoryRegion>(Config.MemoryRegions,
                                     Config.MemoryRegions +
                                         Config.MemoryRegionCount),
      rvm_utils::Logger(MaybeLogFilePath), std::move(debug_logger));

  Sim->reset();
  Sim->setCallbackHandler(Config.CallbackHandler);
  Sim->setMemReadCallback(Config.MemReadCallback);
  Sim->setMemUpdateCallback(Config.MemUpdateCallback);
  Sim->setXRegUpdateCallback(Config.XRegUpdateCallback);
  Sim->setFRegUpdateCallback(Config.FRegUpdateCallback);
  Sim->setVRegUpdateCallback(Config.VRegUpdateCallback);
  Sim->setCSRUpdateCallback(Config.CSRUpdateCallback);
  Sim->setPCUpdateCallback(Config.PCUpdateCallback);

  SPIKE_DEBUG(Sim->get_debug_logger(),
              [](auto &os) { os << "simulator instance created\n"; });

  return new RVMState{Config, std::move(Sim)};
} catch (rvm::invalid_mem_cfg_t &e) {
  if (Err) {
    auto *Msg = e.what();
    std::copy_n(Msg, std::min(std::strlen(Msg), ErrBufSize), ErrBuf);
  }
  return nullptr;
} catch (std::exception &e) {
  rvm_utils::report_fatal_error(e.what());
} catch (...) {
  rvm_utils::report_fatal_error("unexpected error condition in rvm_modelCreate");
}

int rvm_queryCallbackSupportPresent([[maybe_unused]] const RVMState *State) {
  return 1;
}

void rvm_modelDestroy(RVMState *State) { delete State; }

void rvm_modelReset(RVMState *State) {
  assert(State);
  State->Sim->reset();
}

const RVMConfig *rvm_getModelConfig(const RVMState *State) {
  assert(State);
  return &State->Config;
}

RVMSimExecStatus rvm_executeInstr(RVMState *State) {
  auto &Sim = *State->Sim;
  return Sim.step(1);
}

RVMErrorCode rvm_readMem(const RVMState *State, uint64_t Addr, size_t Count,
                         char *Data) {
  auto Err = State->Sim->check_memory_access(Addr, Count);
  if (Err != RVM_ERRC_SUCCESS)
    return Err;
  State->Sim->imem().read(Addr, Count, Data);
  return RVM_ERRC_SUCCESS;
}

RVMErrorCode rvm_writeMem(RVMState *State, uint64_t Addr, uint64_t Count,
                          const char *Data) {
  auto Err = State->Sim->check_memory_access(Addr, Count);
  if (Err != RVM_ERRC_SUCCESS)
    return Err;
  State->Sim->imem().write(Addr, Count, Data);
  return RVM_ERRC_SUCCESS;
}

void rvm_setStopMode(RVMState *State, RVMStopMode Mode) {
  assert(State);
  State->Config.Mode = Mode;
  State->Sim->set_stop_mode(Mode);
}

RVMErrorCode rvm_setStopPC(RVMState *State, uint64_t Addr) {
  assert(State);
  State->Config.StopAddr = Addr;
  return State->Sim->set_end_pc(Addr);
}

uint64_t rvm_readPC(const RVMState *State) { return State->Sim->get_pc_reg(); }

RVMErrorCode rvm_setPC(RVMState *State, uint64_t NewPC) {
  return State->Sim->set_pc_reg(NewPC);
}

RVMErrorCode rvm_readXReg(const RVMState *State, RVMXReg Reg, RVMRegT *Val) {
  return State->Sim->get_gpr(Reg, *Val);
}

RVMErrorCode rvm_setXReg(RVMState *State, RVMXReg Reg, RVMRegT Value) {
  return State->Sim->set_gpr(Reg, Value);
}

RVMErrorCode rvm_readFReg(const RVMState *State, RVMFReg Reg, RVMRegT *Val) {
  return State->Sim->get_fpr(Reg, *Val);
}

RVMErrorCode rvm_setFReg(RVMState *State, RVMFReg Reg, RVMRegT Value) {
  return State->Sim->set_fpr(Reg, Value);
}

RVMErrorCode rvm_readCSR(const RVMState *State, unsigned Reg, RVMRegT *Val) {
  return State->Sim->get_csr(Reg, *Val);
}

RVMErrorCode rvm_setCSR(RVMState *State, unsigned Reg, RVMRegT Value) {
  return State->Sim->set_csr(Reg, Value);
}

RVMErrorCode rvm_raiseInterrupt(RVMState *State, RVMRegT Cause) {
  return State->Sim->raise_interrupt(Cause);
}

RVMErrorCode rvm_clearInterrupt(RVMState *State, RVMRegT Cause) {
  return State->Sim->clear_interrupt(Cause);
}

RVMErrorCode rvm_readVReg(const RVMState *State, RVMVReg Reg, char *Data,
                          size_t *MaxSize) {
  return State->Sim->get_vreg(Reg, Data, *MaxSize);
}

RVMErrorCode rvm_setVReg(RVMState *State, RVMVReg Reg, const char *Data,
                         size_t *DataSize) {
  assert(DataSize);
  return State->Sim->set_vreg(Reg, Data, *DataSize);
}

void rvm_logMessage([[maybe_unused]] const RVMState *State,
                    const char *Message) {
  SPIKE_DEBUG(State->Sim->get_logger(), [Message](auto &os) { os << Message; });
}
void rvm_getErrorContext(const RVMState *State, char *Buf, size_t *BufSize) {
  State->Sim->get_error_context(Buf, BufSize);
}
}
