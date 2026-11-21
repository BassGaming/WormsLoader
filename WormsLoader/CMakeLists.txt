set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(minhook STATIC
  ${TP}/minhook/src/buffer.c ${TP}/minhook/src/hook.c ${TP}/minhook/src/trampoline.c ${TP}/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${TP}/minhook/include)

add_library(imgui STATIC
  ${TP}/imgui/imgui.cpp ${TP}/imgui/imgui_draw.cpp ${TP}/imgui/imgui_tables.cpp ${TP}/imgui/imgui_widgets.cpp
  ${TP}/imgui/backends/imgui_impl_opengl2.cpp ${TP}/imgui/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${TP}/imgui)

# Output name dinput8.dll: the game imports DINPUT8.dll, so Windows loads this from the game folder.
add_library(WormsLoader SHARED src/dllmain.cpp src/api.cpp src/config.cpp src/net.cpp src/ui.cpp src/assets.cpp src/matchfix.cpp src/dinput8.def)
target_include_directories(WormsLoader PRIVATE include)
target_link_libraries(WormsLoader PRIVATE minhook imgui opengl32 ws2_32)
set_target_properties(WormsLoader PROPERTIES OUTPUT_NAME dinput8
                                             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out
                                             RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/out)

# Self-check for the parts that need no game: hashing and ini round-trip.
add_executable(wl_selftest test/selftest.cpp src/config.cpp)
target_include_directories(wl_selftest PRIVATE include src)
