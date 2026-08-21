cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 17)

include(ExternalProject)

project(SpikeSim LANGUAGES C CXX)
include(GNUInstallDirs)
if(${SC_RVM_BUILD_TESTING})
  include(CTest)
endif()

get_filename_component(SPIKE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR} PATH)

set(SPIKE_INSTALL_PATH ${CMAKE_BINARY_DIR}/install_spike/spike)
set(SPIKE_BUILD_DIR SpikeSimBuild)
set(SPIKE_BUILD_PATH ${CMAKE_BINARY_DIR}/${SPIKE_BUILD_DIR}/src/spike_sim-build)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(SPIKE_OPT_LEVEL -Og)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(SPIKE_OPT_LEVEL -O3)
endif()

set(SPIKE_DEBUG_INFO_FLAG -g1)
set(SPIKE_OPT_FLAGS ${SPIKE_OPT_LEVEL}\ ${SPIKE_DEBUG_INFO_FLAG})

file(WRITE ${SPIKE_BUILD_PATH}/ldflags "-gz -Wl,-rpath=${CMAKE_INSTALL_RPATH}")
file(WRITE ${SPIKE_BUILD_PATH}/ccflags "${SPIKE_OPT_FLAGS} ${ADDITIONAL_ARGS}")
file(WRITE ${SPIKE_BUILD_PATH}/cxxflags "${SPIKE_OPT_FLAGS} ${ADDITIONAL_ARGS}")

ExternalProject_Add(spike_sim
  PREFIX ${SPIKE_BUILD_DIR}
  SOURCE_DIR ${SPIKE_SOURCES}
  CONFIGURE_COMMAND
    ${SPIKE_SOURCES}/configure
      --prefix=${CMAKE_INSTALL_PREFIX}
      LDFLAGS=@${SPIKE_BUILD_PATH}/ldflags
      CXXFLAGS=@${SPIKE_BUILD_PATH}/cxxflags
      CFLAGS=@${SPIKE_BUILD_PATH}/ccflags
  # This doesn't work with Ninja generator but we don't care.
  # TODO: Use JOB_SERVER_AWARE instead with cmake >= 3.28.
  BUILD_COMMAND $(MAKE) INSTALLDIR=${SPIKE_INSTALL_PATH} install
  BUILD_BYPRODUCTS ${SPIKE_INSTALL_PATH}/bin/spike ${SPIKE_INSTALL_PATH}/bin/spike-dasm
  INSTALL_COMMAND ""
  BUILD_ALWAYS TRUE
)

get_filename_component(SPIKE_INSTALL_PARENT ${SPIKE_INSTALL_PATH} PATH)

add_subdirectory(${SPIKE_SOURCES}/sc-rvm sc-rvm)

install(PROGRAMS ${SPIKE_INSTALL_PATH}/bin/spike
                 ${SPIKE_INSTALL_PATH}/bin/spike-dasm DESTINATION ${CMAKE_INSTALL_BINDIR})
