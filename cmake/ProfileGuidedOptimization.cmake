# Profile-guided optimisation (PGO): the compiler lays out and inlines code by
# what a training run measured, rather than by guesswork. It takes two builds of
# the SAME build tree - GCC finds each object's profile by the object's path:
#
#   1. Configure with -DMUDLET_PGO=GENERATE and build PipelineBenchmark,
#      MapRenderBenchmark and PathfindBenchmark, which need BUILD_TESTING=ON:
#      the instrumented programs the training runs.
#   2. Run CI/pgo-train.sh on that tree, which records the profile.
#   3. Reconfigure with -DMUDLET_PGO=USE and build again: the optimised Mudlet.
#
# MUDLET_PGO_DIR is where the profile lives; docs/platform-builds.md has the
# full walk-through and what the training run covers.

set(MUDLET_PGO "OFF" CACHE STRING "Profile-guided optimisation stage: OFF, GENERATE or USE")
set_property(CACHE MUDLET_PGO PROPERTY STRINGS OFF GENERATE USE)
set(MUDLET_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-profile" CACHE PATH "Where the PGO profile is written (GENERATE) and read (USE)")

string(TOUPPER "${MUDLET_PGO}" MUDLET_PGO)
if(MUDLET_PGO STREQUAL "OFF" OR MUDLET_PGO STREQUAL "")
  return()
endif()
if(NOT MUDLET_PGO STREQUAL "GENERATE" AND NOT MUDLET_PGO STREQUAL "USE")
  message(FATAL_ERROR "MUDLET_PGO is \"${MUDLET_PGO}\" - use OFF, GENERATE or USE")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(MUDLET_PGO_COMPILER "GCC")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MUDLET_PGO_COMPILER "ClangCL")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(MUDLET_PGO_COMPILER "Clang")
else()
  message(FATAL_ERROR "MUDLET_PGO supports GCC and Clang, not ${CMAKE_CXX_COMPILER_ID}")
endif()

# TriggerMatchPool's helper threads run instrumented code too, and non-atomic
# counters would lose their increments to races.
if(MUDLET_PGO_COMPILER STREQUAL "ClangCL")
  # clang-cl hands linking to lld-link/link.exe, which know no -fprofile-* flag, so name the runtime
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir OUTPUT_VARIABLE _mudlet_clang_res OUTPUT_STRIP_TRAILING_WHITESPACE)
  file(GLOB_RECURSE _mudlet_profile_rt "${_mudlet_clang_res}/lib/*clang_rt.profile*.lib")
  if(MUDLET_PGO STREQUAL "GENERATE")
    if(NOT _mudlet_profile_rt)
      message(FATAL_ERROR "no clang_rt.profile library under ${_mudlet_clang_res}/lib")
    endif()
    list(GET _mudlet_profile_rt 0 _mudlet_profile_rt)
    file(MAKE_DIRECTORY "${MUDLET_PGO_DIR}")
    add_compile_options("/clang:-fprofile-generate=${MUDLET_PGO_DIR}" "/clang:-fprofile-update=atomic")
    add_link_options("${_mudlet_profile_rt}")
  else()
    set(_mudlet_pgo_profdata "${MUDLET_PGO_DIR}/mudlet.profdata")
    if(NOT EXISTS "${_mudlet_pgo_profdata}")
      message(FATAL_ERROR "MUDLET_PGO=USE but ${_mudlet_pgo_profdata} does not exist")
    endif()
    add_compile_options("/clang:-fprofile-use=${_mudlet_pgo_profdata}" "-Wno-profile-instr-unprofiled" "-Wno-profile-instr-out-of-date")
  endif()
elseif(MUDLET_PGO STREQUAL "GENERATE")
  file(MAKE_DIRECTORY "${MUDLET_PGO_DIR}")
  add_compile_options("-fprofile-generate=${MUDLET_PGO_DIR}" "-fprofile-update=atomic")
  add_link_options("-fprofile-generate=${MUDLET_PGO_DIR}")
elseif(MUDLET_PGO_COMPILER STREQUAL "GCC")
  # MinGW GCC nests the .gcda files under the profile dir by source path
  file(GLOB_RECURSE _mudlet_pgo_profiles "${MUDLET_PGO_DIR}/*.gcda")
  if(NOT _mudlet_pgo_profiles)
    message(FATAL_ERROR "MUDLET_PGO=USE but ${MUDLET_PGO_DIR} holds no .gcda profiles - build with MUDLET_PGO=GENERATE and run CI/pgo-train.sh first")
  endif()
  # The training cannot visit every feature, and without
  # -fprofile-partial-training GCC optimises everything it did not reach for
  # size. With it, functions the training never entered are compiled as if
  # there were no profile; unreached blocks inside trained ones stay cold.
  add_compile_options("-fprofile-use=${MUDLET_PGO_DIR}" "-fprofile-partial-training" "-fprofile-correction" "-Wno-missing-profile")
else()
  # Clang's counterpart is CI/pgo-train.sh merging the profile with -sparse, which
  # leaves out functions the training never entered so they are not treated as cold.
  set(_mudlet_pgo_profdata "${MUDLET_PGO_DIR}/mudlet.profdata")
  if(NOT EXISTS "${_mudlet_pgo_profdata}")
    message(FATAL_ERROR "MUDLET_PGO=USE but ${_mudlet_pgo_profdata} does not exist - build with MUDLET_PGO=GENERATE and run CI/pgo-train.sh first")
  endif()
  add_compile_options("-fprofile-use=${_mudlet_pgo_profdata}" "-Wno-profile-instr-unprofiled" "-Wno-profile-instr-out-of-date")
endif()

message(STATUS "Profile-guided optimisation: ${MUDLET_PGO} with ${MUDLET_PGO_COMPILER}, profile in ${MUDLET_PGO_DIR}")
