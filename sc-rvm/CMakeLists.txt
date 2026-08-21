set(CMAKE_CXX_STANDARD 20)
set(RVM_ENABLE_ASSERTIONS
    ON
    CACHE BOOL "Enable assertions")

# Taken from llvm-project
if(RVM_ENABLE_ASSERTIONS)
  # MSVC doesn't like _DEBUG on release builds. See PR 4379.
  if(NOT MSVC)
    add_compile_definitions(_DEBUG)
  endif()
  # On non-Debug builds cmake automatically defines NDEBUG, so we explicitly
  # undefine it:
  if(NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    add_compile_options(
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-UNDEBUG>)
    if(MSVC)
      # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
      foreach(
        flags_var_to_scrub
        CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
                             "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
      endforeach()
    endif()
  endif()
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
if (uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
  # Enable assertions in libstdc++.
  add_compile_definitions(_GLIBCXX_ASSERTIONS)
  # Enable assertions in libc++.
  add_compile_definitions(_LIBCPP_ENABLE_ASSERTIONS)
endif()

function(add_spike_based_library target_name)
  set(options_args SHARED)
  set(multi_value_args LLVM_COMPONENTS SOURCES)
  cmake_parse_arguments(ARGUMENTS "${options_args}" "" "${multi_value_args}"
                        ${ARGN})
  if(${ARGUMENTS_SHARED})
    add_library(${target_name} SHARED ${ARGUMENTS_SOURCES})
  else()
    add_library(${target_name} STATIC ${ARGUMENTS_SOURCES})
  endif()

  target_include_directories(
    ${target_name}
    PRIVATE ${SPIKE_INSTALL_PATH}/include ${SPIKE_INSTALL_PATH}/include/softfloat
            ${CMAKE_CURRENT_LIST_DIR}/include)
  # FIXME: -fPIC should be explicitly set for static libraries only
  target_compile_options(
    ${target_name} PRIVATE ${SPIKE_OPT_LEVEL} ${SPIKE_DEBUG_INFO_FLAG} -Wall
                           -Wextra -Werror -fPIC)
  target_link_options(
    ${target_name} PRIVATE -gz)
  add_dependencies(${target_name} spike_sim)
  install(TARGETS ${target_name})
endfunction()

add_spike_based_library(
  RISCVModel SHARED SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/RVM.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/VTable.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/Simulator.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(rvmi REQUIRED IMPORTED_TARGET rvmi)
target_link_libraries(RISCVModel PUBLIC PkgConfig::rvmi)

add_library(spike_internal_headers INTERFACE)
target_include_directories(spike_internal_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../riscv)
target_include_directories(spike_internal_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../softfloat)
target_include_directories(spike_internal_headers INTERFACE ${CMAKE_BINARY_DIR}/SpikeSimBuild/src/spike_sim-build)
target_link_libraries(
  RISCVModel
  PRIVATE ${SPIKE_BUILD_PATH}/libriscv.a ${SPIKE_BUILD_PATH}/libsoftfloat.a
          ${SPIKE_BUILD_PATH}/libfesvr.a ${SPIKE_BUILD_PATH}/libfdt.a
          ${SPIKE_BUILD_PATH}/libdisasm.a pthread)

target_link_options(RISCVModel PRIVATE -gz)
target_link_libraries(
  RISCVModel
  PRIVATE
  spike_internal_headers
)
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions("-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")

if (BUILD_TESTING)
  set(MODEL_BINARIES_DIR ${CMAKE_CURRENT_BINARY_DIR})
  enable_testing()
  add_subdirectory(tests)
endif ()
