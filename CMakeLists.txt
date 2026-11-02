cmake_minimum_required(VERSION 3.25)
project(david VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
if(NOT WIN32)
  find_package(Fontconfig REQUIRED)
endif()

add_compile_definitions(DAVID_VERSION="${PROJECT_VERSION}")
if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0603 WINVER=0x0603 NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
  add_compile_options(/W3 /utf-8 /permissive-)
endif()

#region [ core ]
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS core/*.cpp)
add_library(david_core STATIC ${CORE_SRC})
target_include_directories(david_core PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(david_core PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(david_core PUBLIC PNG::PNG ZLIB::ZLIB WebP::webp WebP::webpdecoder WebP::webpdemux WebP::libwebpmux glm::glm nlohmann_json::nlohmann_json Freetype::Freetype harfbuzz::harfbuzz)
#endregion

#region [ render ]
file(GLOB_RECURSE RENDER_SRC CONFIGURE_DEPENDS render/*.cpp)
add_library(david_render STATIC ${RENDER_SRC})
target_link_libraries(david_render PUBLIC david_core glad::glad SDL3::SDL3)
#endregion

#region [ mods ]
file(GLOB SCRIPTING_SRC CONFIGURE_DEPENDS scripting/*.cpp)
add_library(david_mods STATIC ${SCRIPTING_SRC})
target_include_directories(david_mods PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(david_mods PUBLIC david_render ${LUA_LIBRARIES} sol2::sol2 ${CMAKE_DL_LIBS})
# sol2 bindings outgrow the default object section limit in debug builds
if(MSVC)
  target_compile_options(david_mods PRIVATE /bigobj)
endif()

# The native example mod (the tutorial's finished result), built next to the app's mods
add_library(qoi-like SHARED mods/examples/qoi-like/qoi_like.c)
target_include_directories(qoi-like PRIVATE ${CMAKE_SOURCE_DIR}/sdk)
set_target_properties(qoi-like PROPERTIES OUTPUT_NAME qlike PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mods/examples/qoi-like
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mods/examples/qoi-like)
#endregion

#region [ app ]
file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS app/*.cpp ui/*.cpp platform/*.cpp)
if(WIN32)
  # The exe's icon and version info
  configure_file(app/david.rc.in ${CMAKE_BINARY_DIR}/david.rc @ONLY)
  list(APPEND APP_SRC ${CMAKE_BINARY_DIR}/david.rc)
  # The resource compiler doesn't report the icon as an input: without this a new icon never reaches the exe
  set_source_files_properties(${CMAKE_BINARY_DIR}/david.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/assets/icons/david.ico)
endif()
add_executable(david WIN32 ${APP_SRC})
target_link_libraries(david PRIVATE david_mods imgui::imgui SDL3::SDL3)
add_dependencies(david qoi-like)
if(WIN32)
  target_link_libraries(david PRIVATE dwrite)
else()
  target_link_libraries(david PRIVATE Fontconfig::Fontconfig)
endif()
add_custom_command(TARGET david POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/themes $<TARGET_FILE_DIR:david>/themes
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:david>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:david>/lang
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/patterns $<TARGET_FILE_DIR:david>/patterns
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/mods $<TARGET_FILE_DIR:david>/mods
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/docs/modding $<TARGET_FILE_DIR:david>/docs/modding)
#endregion

#region [ cli ]
# A console program: the GUI is a windowed app, so pipes and exit codes work only here
add_executable(david-cli cli/main.cpp platform/Paths.cpp)
target_link_libraries(david-cli PRIVATE david_mods CLI11::CLI11 SDL3::SDL3)
add_dependencies(david-cli david)
#endregion

#region [ tests ]
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
add_executable(david_tests ${TEST_SRC})
target_link_libraries(david_tests PRIVATE david_core)
#endregion
