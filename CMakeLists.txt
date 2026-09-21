cmake_minimum_required(VERSION 3.24)
project(latex C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(SK_SUPPORT_PDF=1 SK_ENABLE_SKPDF=1)

find_package(Doxygen)

if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "latex")
    set(DOXYGEN_OUTPUT_DIRECTORY "${binary}/docs")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_FULL_SIDEBAR NO)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_REFERENCED_BY_RELATION YES)
    set(DOXYGEN_DOT_IMAGE_FORMAT svg)
    set(DOXYGEN_INTERACTIVE_SVG YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_ALIASES [["complexity=@par Complexity:^^"]])

    doxygen_add_docs(docs "${source}/include" "${source}/src")
endif()

find_program(GPERF_EXECUTABLE gperf REQUIRED)

find_package(unofficial-skia CONFIG REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)
find_package(EXPAT REQUIRED)

find_package(harfbuzz CONFIG QUIET)
if(NOT harfbuzz_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
endif()

set(source "${CMAKE_CURRENT_SOURCE_DIR}")
set(binary "${CMAKE_CURRENT_BINARY_DIR}")

file(MAKE_DIRECTORY "${binary}/assets")
file(MAKE_DIRECTORY "${binary}/include")

set(input "${source}/assets/unicodes.gperf")
set(output "${binary}/include/lookup.hpp")

add_custom_command(
        OUTPUT ${output}
        COMMAND ${GPERF_EXECUTABLE} -t -C -N query -Z Lookup -L C++ ${input} --output-file=${output}
        DEPENDS ${input}
        VERBATIM
)

add_custom_target(lookup DEPENDS ${output})

set(modules "${source}/src/modules")
set(script "${source}/cmake/ModulesStaticLinker.cmake")
set(draft "${binary}/assets/modules.gperf")
set(header "${binary}/include/modules.hpp")

file(GLOB files CONFIGURE_DEPENDS "${modules}/*.mtex")

add_custom_command(
        OUTPUT ${draft}
        COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${modules} -DTARGET=${draft} -P ${script}
        DEPENDS ${files} ${script}
        VERBATIM
)

add_custom_command(
        OUTPUT ${header}
        COMMAND ${GPERF_EXECUTABLE} -t -C -N query -Z Modules -L C++ ${draft} --output-file=${header}
        DEPENDS ${draft}
        VERBATIM
)

add_custom_target(modules DEPENDS ${header})

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${source}/src/*.cpp"
        "${source}/src/*.c"
)

set(engine "${sources}")
list(REMOVE_ITEM engine "${source}/src/main.cpp")

file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${source}/include/*.hpp"
        "${source}/include/*.h"
        "${source}/src/*.hpp"
        "${source}/src/*.h"
)

file(GLOB_RECURSE tree LIST_DIRECTORIES true CONFIGURE_DEPENDS
        "${source}/include/*"
        "${source}/src/*"
)

set(includes
        "${source}/include"
        "${source}/src"
        "${binary}"
        "${binary}/include"
)

foreach(item ${tree})
    if(IS_DIRECTORY ${item})
        list(APPEND includes ${item})
    endif()
endforeach()

add_executable(latex ${sources} ${headers} ${output} ${header})
add_dependencies(latex lookup modules)

target_include_directories(latex PUBLIC ${includes})

target_link_libraries(latex PRIVATE
        unofficial::skia::skia
        Fontconfig::Fontconfig
        Freetype::Freetype
        EXPAT::EXPAT
)

if(TARGET harfbuzz::harfbuzz)
    target_link_libraries(latex PRIVATE harfbuzz::harfbuzz)
else()
    target_link_libraries(latex PRIVATE ${HARFBUZZ_LIBRARIES})
    target_include_directories(latex PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
endif()

if(WIN32)
    target_link_libraries(latex PRIVATE
            dwrite
            gdi32
            user32
            opengl32
            usp10
    )
endif()

enable_testing()
add_subdirectory(tests)