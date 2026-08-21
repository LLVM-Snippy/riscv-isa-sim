#include "RISCVModel/VTable.h"

unsigned char RVMAPI_VERSION_SYMBOL = RVMAPI_CURRENT_INTERFACE_VERSION;

extern const struct rvm::RVM_FunctionPointers RVMAPI_ENTRY_POINT_SYMBOL = {
    .modelCreate = &rvm_modelCreate,
    .modelDestroy = &rvm_modelDestroy,
    .modelReset = &rvm_modelReset,

    .getModelConfig = &rvm_getModelConfig,

    .executeInstr = &rvm_executeInstr,

    .readMem = &rvm_readMem,
    .writeMem = &rvm_writeMem,

    .setStopMode = &rvm_setStopMode,
    .setStopPC = &rvm_setStopPC,

    .readPC = &rvm_readPC,
    .setPC = &rvm_setPC,

    .readXReg = &rvm_readXReg,
    .setXReg = &rvm_setXReg,

    .readFReg = &rvm_readFReg,
    .setFReg = &rvm_setFReg,

    .readCSR = &rvm_readCSR,
    .setCSR = &rvm_setCSR,

    .readVReg = &rvm_readVReg,
    .setVReg = &rvm_setVReg,

    .raiseInterrupt = &rvm_raiseInterrupt,
    .clearInterrupt = &rvm_clearInterrupt,

    .logMessage = &rvm_logMessage,
    .queryCallbackSupportPresent = &rvm_queryCallbackSupportPresent,
    .getErrorContext = &rvm_getErrorContext,
};
