cmake_minimum_required(VERSION 3.24)
project(AquaFrame VERSION 1.0.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Aqua Frame requires Windows x64 and MSVC.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(USE_SANDBOX ON CACHE BOOL "Enable the Chromium sandbox")
if(NOT USE_SANDBOX)
  message(FATAL_ERROR "Aqua Frame requires the Chromium sandbox.")
endif()
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef" CACHE PATH "CEF binary distribution")
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)
ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()
add_library(AquaFrame SHARED
  src/browser/main.cpp
  src/chromium/client.cpp
  src/navigation/address.cpp
  src/ui/window.cpp
  src/chromium/pages.cpp)
SET_LIBRARY_TARGET_PROPERTIES(AquaFrame)
target_compile_features(AquaFrame PRIVATE cxx_std_20)
target_compile_definitions(AquaFrame PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(AquaFrame PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
target_link_libraries(AquaFrame PRIVATE libcef_lib libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
  comctl32 shell32 ole32 oleaut32 urlmon dwmapi)
COPY_SINGLE_FILE(AquaFrame "${CEF_BINARY_DIR}/bootstrap.exe" "${CEF_TARGET_OUT_DIR}/AquaFrame.exe")
COPY_FILES(AquaFrame "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(AquaFrame "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
SET_LPAC_ACLS(AquaFrame)
install(DIRECTORY "${CEF_ROOT}/Release/" DESTINATION .
  PATTERN "bootstrap*.exe" EXCLUDE PATTERN "*.lib" EXCLUDE PATTERN "*.pdb" EXCLUDE PATTERN "*.exp" EXCLUDE)
install(DIRECTORY "${CEF_ROOT}/Resources/" DESTINATION .)
install(TARGETS AquaFrame RUNTIME DESTINATION .)
install(PROGRAMS "${CEF_ROOT}/Release/bootstrap.exe" DESTINATION . RENAME AquaFrame.exe)
install(FILES "${CEF_ROOT}/LICENSE.txt" DESTINATION . RENAME CEF-LICENSE.txt)
