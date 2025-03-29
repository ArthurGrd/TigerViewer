cmake_minimum_required(VERSION 3.10)
project(ASTViewerSVG)

set(CMAKE_CXX_STANDARD 17)

# --- Utiliser pkg-config pour trouver GLFW3 ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
include_directories(${GLFW3_INCLUDE_DIRS})
link_directories(${GLFW3_LIBRARY_DIRS})

# --- Intégrer ImGui sans modifier son contenu ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

# --- Intégrer tinyfiledialogs ---
add_library(tinyfiledialogs STATIC ${CMAKE_SOURCE_DIR}/tinyfiledialogs/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${CMAKE_SOURCE_DIR}/tinyfiledialogs)

# --- Utiliser pkg-config pour librsvg, cairo et glib ---
pkg_check_modules(LIBRSVG REQUIRED librsvg-2.0)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(GLIB REQUIRED glib-2.0)
include_directories(${LIBRSVG_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS})

# --- Créer l'exécutable principal ---
add_executable(ASTViewerSVG
    main.cc
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_link_libraries(ASTViewerSVG imgui tinyfiledialogs)
target_link_libraries(ASTViewerSVG ${GLFW3_LIBRARIES})

if(APPLE)
    find_library(OPENGL_LIBRARY OpenGL)
    target_link_libraries(ASTViewerSVG ${OPENGL_LIBRARY} glfw)
elseif(WIN32)
    target_link_libraries(ASTViewerSVG opengl32 glfw glew32)
else()
    target_link_libraries(ASTViewerSVG GL glfw GLEW dl pthread)
endif()

target_link_libraries(ASTViewerSVG ${LIBRSVG_LIBRARIES} ${CAIRO_LIBRARIES} ${GLIB_LIBRARIES})
