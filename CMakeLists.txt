cmake_minimum_required(VERSION 3.20)
project(WormsCA CXX C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Worms Clan Wars is 32-bit: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT, no redist needed
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_subdirectory(WormsLoader)

# One folder per mod: <Name>/*.cpp -> Mods/<Name>/<Name>.dll
function(wl_mod name)
  file(GLOB src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp)
  add_library(${name} SHARED ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/WormsLoader/include)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/Mods/${name}
                                           RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/out/Mods/${name})
endfunction()

wl_mod(Reconnect)
wl_mod(Example)
# The Example ships its commented source, so mods can be written without the repository.
add_custom_command(TARGET Example POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/out/Mods/Example/source
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Example/example.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/WormsLoader/include/wormsloader.h ${CMAKE_BINARY_DIR}/out/Mods/Example/source/)
wl_mod(Borderless)
wl_mod(FpsCap)
wl_mod(SkipIntro)
# Developer tool (call counter for RE); links its own MinHook copy.
wl_mod(Trace)
target_link_libraries(Trace PRIVATE minhook)
add_custom_command(TARGET Trace POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Trace/targets.txt ${CMAKE_CURRENT_SOURCE_DIR}/Trace/calllog.txt ${CMAKE_BINARY_DIR}/out/Mods/Trace/)
