#!/bin/bash
set -x
set -e

ASM_MARCH="rv64gcv"
ASM_MABI="lp64d"
if [[ "${ASM_TARGET_RV32}" == "TRUE" ]]; then
  ASM_MARCH="rv32gcv"
  ASM_MABI="ilp32d"
fi

test_clang_ext_string() {
    echo "int main() {}" | $COMPILER -x c++ -march=$1 - -c -o /dev/null
}

SIM_ISA_STRING="${ASM_MARCH}"

if [[ $COMPILER == *clang ]]; then
  if [[ "${ASM_TARGET_RV32}" == "TRUE" ]]; then
    EXTRA_COMPILER_ARGS="--gcc-toolchain=${GCC_TOOLCHAIN} -target riscv32-unknown-elf"
  else
    EXTRA_COMPILER_ARGS="--gcc-toolchain=${GCC_TOOLCHAIN} -target riscv64-unknown-elf"
  fi
fi

rm -rf "${TEST_NAME}.elf"
${COMPILER} \
  ${EXTRA_COMPILER_ARGS} \
  ${EXTRA_COMPILER_INCLUDES} \
  -march=${ASM_MARCH} \
  -mabi=${ASM_MABI} \
  -nostdlib \
  -T${LD_SCRIPT} \
  ${ASM_FILE} \
  -o "${TEST_NAME}.elf"

JSON_LOG_NAME="exec_log.json"
JSON_LOG_ARG=""
if [[ -n "${EXPECTED_JSON_EXEC_LOG}" ]]; then
  JSON_LOG_ARG="--driver-json-exec-log=${JSON_LOG_NAME}"
fi

if [[ -n "${ISA_STRING}" ]]; then
  DRIVER_ISA_STRING_OPTION="--rvm-isa-string ${ISA_STRING}"
  SIM_ISA_STRING="${ISA_STRING}"
fi

function model_launcher_run() {
  ${EXEC_DRIVER} \
    --driver-model-lib ${DRIVER_MODEL_LIB:-''} \
    --driver-output-state-file output_state.result \
    --driver-allow-exceptions "${DRIVER_EXCEPTION_MODE}" \
    --rvm-model-log-path spike.log \
    --rvm-stop-mode AtLabel \
    --rvm-exec-file-path ${TEST_NAME}.elf \
    --rvm-debug-log-path ${TEST_NAME}.debug.log \
    ${JSON_LOG_ARG} \
    ${DRIVER_XEXT_OPTION} \
    ${DRIVER_ISA_STRING_OPTION}
}

if [[ "${RAW_SPIKE}" != "TRUE" ]]; then

  if [[ "${DRIVER_FAILURE}" != "TRUE" ]]; then
    model_launcher_run
  else
    if model_launcher_run; then
      echo "model launcher indicates successful execution, test fail"
      exit 1
    fi
    echo "model launcher indicates failure, as expected"
  fi

  diff ${EXPECTED_OUTPUT} output_state.result

  if [[ -n "${EXPECTED_JSON_EXEC_LOG}" ]]; then
    diff ${JSON_LOG_NAME} ${EXPECTED_JSON_EXEC_LOG}
  fi
else
  export LD_LIBRARY_PATH=${CUSTOMEXT_LIBS_PATH}
  ${SPIKE_PATH} \
    -l --log-commits \
    --isa ${SIM_ISA_STRING}_x${XEXTS} \
    -m0x80000000:0x10000,0x10000:0x100000 \
    ${TEST_NAME}.elf
fi
