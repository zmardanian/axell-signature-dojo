cmake_minimum_required(VERSION 3.22)
project(AxellSignatureDojo VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.15
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Axell core (shared DSP + look): next to this project, or vendored into external/axell-core for releases
set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/external/axell-core" CACHE PATH "Axell core")
if(NOT EXISTS "${AXELL_CORE_DIR}/CMakeLists.txt")
  set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../axell-core" CACHE PATH "Axell core" FORCE)
endif()
set(AXELL_CORE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${AXELL_CORE_DIR} axell-core)

option(DOJO_SANITIZE "AddressSanitizer + UBSan for the test programs" OFF)
function(dojo_sanitize target)
  if(DOJO_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# Everything JUCE in one static library: the app, the tests and the snapshot tool share it
set(DOJO_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_PLUGINHOST_VST3=1
  JUCE_USE_MP3AUDIOFORMAT=1 JUCE_STRICT_REFCOUNTEDPOINTER=1 JUCE_APPLICATION_NAME_STRING="Axell Signature Dojo"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

file(GLOB DOJO_PLATFORM CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/Source/platform/*.cpp)
file(GLOB DOJO_GUI CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/Source/gui/*.cpp)

juce_add_binary_data(DojoAssets HEADER_NAME DojoAssets.h NAMESPACE DojoAssets SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/duo.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/penta.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/master.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/velvet.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/gate.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/dereverb.png
  ${CMAKE_CURRENT_LIST_DIR}/Resources/images/icon.png)
set_target_properties(DojoAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_gui_app(AxellSignatureDojo
  PRODUCT_NAME "Axell Signature Dojo"
  COMPANY_NAME "Axell Yobag"
  COMPANY_EMAIL "axellpetre@protonmail.com"
  BUNDLE_ID com.axellyobag.signaturedojo
  ICON_BIG ${CMAKE_CURRENT_LIST_DIR}/Resources/images/icon.png)
target_sources(AxellSignatureDojo PRIVATE Source/Main.cpp ${DOJO_PLATFORM} ${DOJO_GUI})
target_include_directories(AxellSignatureDojo PRIVATE Source)
target_compile_definitions(AxellSignatureDojo PRIVATE ${DOJO_DEFS} JUCE_MODAL_LOOPS_PERMITTED=0)
axell_core_add_gui(AxellSignatureDojo)
target_link_libraries(AxellSignatureDojo PRIVATE DojoAssets
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_cryptography juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- tests ---
enable_testing()

# JUCE-free model tests (plain g++: the model headers exactly as they ship)
add_executable(DojoCoreTests tests/CoreTests.cpp)
target_include_directories(DojoCoreTests PRIVATE Source)
target_link_libraries(DojoCoreTests PRIVATE axell_core_dsp)
target_compile_options(DojoCoreTests PRIVATE -Wall -Wextra -Wshadow $<$<CONFIG:Release>:-O2>)
dojo_sanitize(DojoCoreTests)
add_test(NAME core_suite COMMAND DojoCoreTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# platform + GUI suite and snapshot tool (JUCE)
foreach(tool DojoPlatformTests DojoSnapshot)
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_include_directories(${tool} PRIVATE Source)
  target_compile_definitions(${tool} PRIVATE ${DOJO_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(${tool} PRIVATE DojoAssets juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_cryptography juce::juce_dsp juce::juce_recommended_config_flags)
  axell_core_add_gui(${tool})
  target_sources(${tool} PRIVATE ${DOJO_PLATFORM} ${DOJO_GUI})
  dojo_sanitize(${tool})
endforeach()
target_sources(DojoPlatformTests PRIVATE tests/PlatformTests.cpp)
target_sources(DojoSnapshot PRIVATE tests/Snapshot.cpp)
add_test(NAME platform_suite COMMAND DojoPlatformTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# research tool (JUCE-free)
add_executable(VoiceLab tools/VoiceLab.cpp)
target_link_libraries(VoiceLab PRIVATE axell_core_dsp)
target_compile_options(VoiceLab PRIVATE -O2)
