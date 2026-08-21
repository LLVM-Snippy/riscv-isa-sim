set(MODEL_PLUGIN_LIBRARY "${MODEL_BINARIES_DIR}/libRISCVModel.so")
set(TEST_VECTORS_ROOT ${CMAKE_CURRENT_BINARY_DIR}/test_vectors)
file(MAKE_DIRECTORY ${TEST_VECTORS_ROOT})

find_program(MODEL_LAUNCHER_EXECUTABLE NAMES ModelLauncher)
if (MODEL_LAUNCHER_EXECUTABLE)
  message(STATUS "Found ModelLauncher at: ${MODEL_LAUNCHER_EXECUTABLE}")
else()
  message(FATAL_ERROR "ModelLauncher not found")
endif()

function(add_model_test)
  set(options
     CLANG_COMPILER
     CHECK_JSON_EXEC_LOG
     ASM_TARGET_RV32
     EXPECT_DRIVER_FAILURE
     RAW_SPIKE)
  set(oneValueArgs
     TEST_NAME
     ASM_FILE
     ISA_STRING
     EXCEPTION_MODE)
  set(multiValueArgs
     XEXTS)
  cmake_parse_arguments(ModelTest "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN} )

  set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/${ModelTest_TEST_NAME})
  file(MAKE_DIRECTORY ${TEST_DIR})

  if(ModelTest_CLANG_COMPILER)
    set(COMPILER "${CLANG_TOOLCHAIN_PATH}/bin/clang")
  else()
    set(COMPILER "${GCC_TOOLCHAIN_PATH}/bin/riscv64-unknown-elf-gcc")
  endif()

  if(NOT DEFINED ModelTest_EXCEPTION_MODE)
    set(driver_exc_mode "all")
  else()
    set(driver_exc_mode "${ModelTest_EXCEPTION_MODE}")
  endif()

  if (ModelTest_CHECK_JSON_EXEC_LOG)
    set(EXPECTED_JSON_LOG
      "${CMAKE_CURRENT_LIST_DIR}/expected/${ModelTest_ASM_FILE}_json_log.expected")
  else()
    set(EXPECTED_JSON_LOG "")
  endif()

  add_test(
    NAME ${ModelTest_TEST_NAME}
    WORKING_DIRECTORY ${TEST_DIR}
    COMMAND env
      "COMPILER=${COMPILER}"
      "GCC_TOOLCHAIN=${GCC_TOOLCHAIN_PATH}"
      "LD_SCRIPT=${CMAKE_CURRENT_LIST_DIR}/ld/spike.ld"
      "ASM_FILE=${CMAKE_CURRENT_LIST_DIR}/asm/${ModelTest_ASM_FILE}"
      "EXPECTED_JSON_EXEC_LOG=${EXPECTED_JSON_LOG}"
      "EXPECTED_OUTPUT=${CMAKE_CURRENT_LIST_DIR}/expected/${ModelTest_ASM_FILE}.expected"
      "ASM_TARGET_RV32=${ModelTest_ASM_TARGET_RV32}"
      "RAW_SPIKE=${ModelTest_RAW_SPIKE}"
      "TEST_NAME=${ModelTest_TEST_NAME}"
      "EXEC_DRIVER=${MODEL_LAUNCHER_EXECUTABLE}"
      "SPIKE_PATH=${CMAKE_BINARY_DIR}/install_spike/spike/bin/spike"
      "CUSTOMEXT_LIBS_PATH=${MODEL_BINARIES_DIR}"
      "XEXTS=${ModelTest_XEXTS}"
      "ISA_STRING=${ModelTest_ISA_STRING}"
      "DRIVER_EXCEPTION_MODE=${driver_exc_mode}"
      "DRIVER_FAILURE=${ModelTest_EXPECT_DRIVER_FAILURE}"
      "EXTRA_COMPILER_INCLUDES=-I${TEST_VECTORS_ROOT}"
      "DRIVER_MODEL_LIB=${MODEL_PLUGIN_LIBRARY}"
      ${CMAKE_CURRENT_LIST_DIR}/runner/run.sh
  )
endfunction()

add_model_test(TEST_NAME exc_ctrl_ebreak ASM_FILE exc_ctrl_ebreak.S EXCEPTION_MODE ebreak)
add_model_test(TEST_NAME exc_ctrl_exc ASM_FILE exc_ctrl_exc.S EXCEPTION_MODE none EXPECT_DRIVER_FAILURE)
add_model_test(TEST_NAME trace_mem ASM_FILE trace_mem.S CHECK_JSON_EXEC_LOG)
add_model_test(TEST_NAME rvv_basic ASM_FILE rvv.S CHECK_JSON_EXEC_LOG)
add_model_test(TEST_NAME vxsat_noaccess ASM_FILE vxsat_noaccess.S ISA_STRING "rv64i")

add_model_test(TEST_NAME rv32_xreg_width
  ASM_FILE rv32_xreg_width.S
  ASM_TARGET_RV32
  ISA_STRING "rv32gcv")

include(testgen-model-interface-launcher-tests)
if(NOT TESTGEN_MODEL_INTERFACE_LAUNCHER_TESTS_MODULE_LOADED)
  message(FATAL_ERROR "testgen-model-interface-launcher-tests module was not properly included")
endif()
set(MODEL_LAUNCHER_TESTS_DIR ${TESTGEN_MODEL_INTERFACE_LAUNCHER_TESTS_DIR})
add_model_tests()
