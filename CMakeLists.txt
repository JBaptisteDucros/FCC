cmake_minimum_required(VERSION 3.16)

# macOS : binaire universel (Intel + Apple Silicon) -- doit etre defini avant project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

project(FCC CXX)

# Chemin vers le SDK X-Plane (dossier qui contient CHeaders et Libraries)
# ex: cmake -B build -DXPLM_SDK=C:/SDK
set(XPLM_SDK "" CACHE PATH "Dossier du SDK X-Plane (contient CHeaders et Libraries)")
file(TO_CMAKE_PATH "${XPLM_SDK}" XPLM_SDK)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(FCC MODULE src/main.cpp)
target_include_directories(FCC PRIVATE
    ${XPLM_SDK}/CHeaders/XPLM
    ${XPLM_SDK}/CHeaders/Widgets)
target_compile_definitions(FCC PRIVATE XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1)

set_target_properties(FCC PROPERTIES PREFIX "" SUFFIX ".xpl")

if(WIN32)
    target_compile_definitions(FCC PRIVATE IBM=1)
    target_link_libraries(FCC
        ${XPLM_SDK}/Libraries/Win/XPLM_64.lib
        opengl32)
    # runtime statique : pas besoin des redistribuables Visual C++
    set_property(TARGET FCC PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_target_properties(FCC PROPERTIES OUTPUT_NAME "win")
elseif(APPLE)
    target_compile_definitions(FCC PRIVATE APL=1)
    find_library(XPLM_FW XPLM PATHS ${XPLM_SDK}/Libraries/Mac NO_DEFAULT_PATH)
    target_link_libraries(FCC ${XPLM_FW} "-framework OpenGL")
    target_compile_options(FCC PRIVATE -fvisibility=hidden)
    target_link_options(FCC PRIVATE -undefined dynamic_lookup)
    set_target_properties(FCC PROPERTIES OUTPUT_NAME "mac")
else()
    target_compile_definitions(FCC PRIVATE LIN=1)
    target_compile_options(FCC PRIVATE -fvisibility=hidden)
    target_link_libraries(FCC GL)
    target_link_options(FCC PRIVATE -static-libgcc -static-libstdc++)
    set_target_properties(FCC PROPERTIES OUTPUT_NAME "lin")
endif()

# Le .xpl est genere dans dist/FCC/64/ pret a copier dans Falcon8X/plugins/FCC/
set_target_properties(FCC PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/FCC/64
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/FCC/64)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(FCC PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/dist/FCC/64
        RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_SOURCE_DIR}/dist/FCC/64)
endforeach()
