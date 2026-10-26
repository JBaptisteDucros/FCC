cmake_minimum_required(VERSION 3.16)

# macOS : binaire universel (Intel + Apple Silicon) -- doit etre defini avant project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

project(SFC CXX)

# Chemin vers le SDK X-Plane (dossier qui contient CHeaders et Libraries)
# ex: cmake -B build -DXPLM_SDK=C:/SDK
set(XPLM_SDK "" CACHE PATH "Dossier du SDK X-Plane (contient CHeaders et Libraries)")
file(TO_CMAKE_PATH "${XPLM_SDK}" XPLM_SDK)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(SFC MODULE src/main.cpp)
target_include_directories(SFC PRIVATE
    ${XPLM_SDK}/CHeaders/XPLM
    ${XPLM_SDK}/CHeaders/Widgets)
target_compile_definitions(SFC PRIVATE XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1)

set_target_properties(SFC PROPERTIES PREFIX "" SUFFIX ".xpl")

if(WIN32)
    target_compile_definitions(SFC PRIVATE IBM=1)
    target_link_libraries(SFC
        ${XPLM_SDK}/Libraries/Win/XPLM_64.lib
        opengl32)
    # runtime statique : pas besoin des redistribuables Visual C++
    set_property(TARGET SFC PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_target_properties(SFC PROPERTIES OUTPUT_NAME "win")
elseif(APPLE)
    target_compile_definitions(SFC PRIVATE APL=1)
    # Les symboles XPLM sont fournis par X-Plane au chargement : inutile de lier le framework
    # du SDK (qui perd ses liens symboliques quand il passe par Windows/Git).
    target_link_libraries(SFC "-framework OpenGL")
    target_compile_options(SFC PRIVATE -fvisibility=hidden)
    target_link_options(SFC PRIVATE -undefined dynamic_lookup)
    set_target_properties(SFC PROPERTIES OUTPUT_NAME "mac")
else()
    target_compile_definitions(SFC PRIVATE LIN=1)
    target_compile_options(SFC PRIVATE -fvisibility=hidden)
    target_link_libraries(SFC GL)
    target_link_options(SFC PRIVATE -static-libgcc -static-libstdc++)
    set_target_properties(SFC PROPERTIES OUTPUT_NAME "lin")
endif()

# Le .xpl est genere dans dist/SFC/64/ pret a copier dans Falcon8X/plugins/SFC/
set_target_properties(SFC PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/SFC/64
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/SFC/64)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(SFC PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/dist/SFC/64
        RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/dist/SFC/64)
endforeach()
