cmake_minimum_required(VERSION 3.25)

# The libraries -- HarfBuzz, FreeType, the image libraries -- come from
# vcpkg. Its toolchain is taken from whichever vcpkg has libraries installed,
# VCPKG_ROOT's or the one at C:/vcpkg where it usually lives on Windows,
# whenever the command line names none: so a preset, an IDE's own profile
# and a bare `cmake -B build` all find them. Installed, because a Visual
# Studio shell points VCPKG_ROOT at the copy Visual Studio ships, which holds
# no libraries of its own.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    foreach(root "$ENV{VCPKG_ROOT}" "C:/vcpkg")
        if(NOT root STREQUAL "" AND EXISTS "${root}/scripts/buildsystems/vcpkg.cmake" AND IS_DIRECTORY "${root}/installed")
            set(CMAKE_TOOLCHAIN_FILE "${root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg's toolchain")
            break()
        endif()
    endforeach()
endif()

# On Windows the engine is built with the clang-cl Visual Studio installs
# beside cl, under VC/Tools/Llvm, whatever compiler an IDE's profile names.
# cl has no C++26 and no #embed; a MinGW GCC has neither before GCC 15, and
# none can link vcpkg's x64-windows libraries, which are built for the MSVC
# ABI. So a profile naming cl or a GCC -- CLion's "Visual Studio" and
# "MinGW" toolchains -- or naming no compiler at all is given clang-cl
# instead, found through vswhere in the newest Visual Studio that has it,
# previews included; a Clang it names is kept. Decided here, before
# project(), the one place a compiler can still be chosen.
if(CMAKE_HOST_WIN32)
    get_filename_component(named "${CMAKE_CXX_COMPILER}" NAME_WE)
    string(TOLOWER "${named}" named)
    if(named STREQUAL "" OR named STREQUAL "cl" OR named MATCHES "^(g\\+\\+|gcc|c\\+\\+|cc)(-[0-9.]+)?$")
        set(programs "ProgramFiles(x86)")
        find_program(vswhere vswhere HINTS "$ENV{${programs}}/Microsoft Visual Studio/Installer" NO_CACHE)
        set(studio "")
        if(vswhere)
            execute_process(COMMAND "${vswhere}" -latest -prerelease -products *
                                    -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
                            OUTPUT_VARIABLE studio OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
        find_program(clang clang-cl HINTS "${studio}/VC/Tools/Llvm/x64/bin" NO_CACHE)
        if(clang)
            if(NOT named STREQUAL "")
                message(STATUS "${named} cannot build this project (C++26, #embed, vcpkg's x64-windows); "
                               "compiling with ${clang} instead")
            endif()
            set(CMAKE_C_COMPILER "${clang}" CACHE FILEPATH "C compiler" FORCE)
            set(CMAKE_CXX_COMPILER "${clang}" CACHE FILEPATH "C++ compiler" FORCE)
            # The MinGW toolchain's windres, if one was named, reads no
            # manifest clang-cl's linker hands it.
            unset(CMAKE_RC_COMPILER CACHE)
            if(NOT DEFINED VCPKG_TARGET_TRIPLET)
                set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg's triplet")
            endif()
        endif()
    endif()

    # clang-cl's own resource compiler, which links every program's manifest
    # in: the SDK's `rc` is on the path only inside a Visual Studio shell, and
    # neither a MinGW profile nor a preset run outside one is.
    if(CMAKE_CXX_COMPILER MATCHES "clang-cl" AND NOT DEFINED CMAKE_RC_COMPILER)
        get_filename_component(tools "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(resources llvm-rc HINTS "${tools}" NO_CACHE)
        if(resources)
            set(CMAKE_RC_COMPILER "${resources}" CACHE FILEPATH "Resource compiler")
        endif()
    endif()
endif()

project(latex VERSION 0.1.0 LANGUAGES C CXX)

# C++26, for #embed above all: the prelude, its packages and the hyphenation
# patterns are compiled into the engine as the files they are, by the
# compiler, with no tool of the project's own to write them out first. That
# takes Clang 19 or later -- on Windows, the clang-cl that Visual Studio
# installs beside cl, under VC/Tools/Llvm -- or GCC 15 or later. cl itself
# has no #embed yet.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The C and C++ runtimes linked into the engine rather than loaded beside it.
# Of every library a run loads, the C++ runtime is the one only this code
# asks for -- HarfBuzz, FreeType and the image libraries are C at their
# boundary and bring none -- and loading it cost a millisecond before main
# began. Those libraries keep their own; nothing one side allocates is freed
# by the other, each handing back its own memory through its own call.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Defined before anything reads them. The documentation target below names
# both, and naming them before they are set silently documents nothing --
# which looks exactly like a build that worked.
set(source "${CMAKE_CURRENT_SOURCE_DIR}")
set(binary "${CMAKE_CURRENT_BINARY_DIR}")

# Asked of the compiler once, here, rather than discovered as a wall of
# errors from the first file that embeds something.
include(CheckCXXSourceCompiles)
file(WRITE "${binary}/embedding.txt" "x")
check_cxx_source_compiles("
static constexpr char bytes[] = {
#embed \"${binary}/embedding.txt\"
};
int main() { return bytes[0] == 'x' ? 0 : 1; }" embedding)
if(NOT embedding)
    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no #embed. Build with "
            "Clang 19 or later (on Windows, the clang-cl under Visual Studio's VC/Tools/Llvm/x64/bin) "
            "or GCC 15 or later.")
endif()

# Warnings are errors waiting to be read. Third-party headers and the
# generated lookup table are marked external, so their own noise -- none of
# which this project can act on -- does not bury the engine's.
#
# Clang still calls #embed an extension of its own under C++26, which it is
# not: that one warning is turned off here rather than around each file that
# embeds, so no source carries a block of compiler-specific pragmas.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl: MSVC's spellings, and Clang's own shadowing check beside them.
    add_compile_options(/W4 /permissive- /external:W0 "/external:I${binary}/include" -Wshadow
            -Wno-c23-extensions)
elseif(MSVC)
    add_compile_options(/W4 /permissive- /external:W0 /external:anglebrackets
            "/external:I${binary}/include")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wshadow -Wno-c23-extensions)
else()
    add_compile_options(-Wall -Wextra -Wshadow)
endif()

# A release build carries nothing it does not run. No debug information is
# written, every function and constant nothing reaches is dropped at link
# time, identical ones are folded into one, symbols are stripped, and the
# whole engine is optimised as a single program rather than a file at a time.
# logger.hpp does the same for the engine's own tracing: below Informative it
# is compiled out of a release build entirely, where NDEBUG is defined.
#
# Emscripten strips a release module itself -- names, debug information and
# the runtime's own assertions all go at -O3 -- and reads `-s` as the start
# of one of its settings, so it is left to do that its own way.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
    add_link_options($<$<CONFIG:Release>:/DEBUG:NONE> $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>)
elseif(EMSCRIPTEN)
    add_link_options($<$<CONFIG:Release>:-sASSERTIONS=0>)
else()
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    if(APPLE)
        add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip> $<$<CONFIG:Release>:-Wl,-S>)
    else()
        add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-Wl,--strip-all>)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT whole LANGUAGES CXX)
if(whole)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

find_package(Doxygen)

if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "latex")
    set(DOXYGEN_PROJECT_BRIEF "A typesetting engine")
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

    # A parameter block that does not match the signature is a comment that
    # has drifted from its code, which is worse than no comment at all.
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_QUIET YES)

    # The package listing is generated into the build tree, and the file that
    # embeds it is read with it.
    set(DOXYGEN_INCLUDE_PATH "${binary}/include")

    # `@complexity` reads as its own paragraph, so the cost of an operation
    # sits beside its description rather than buried in it.
    set(DOXYGEN_ALIASES [["complexity=@par Complexity:^^"]])

    # The guides beside the reference: how to write a package, as a page of
    # its own.
    doxygen_add_docs(docs "${source}/include" "${source}/src" "${source}/docs"
            COMMENT "Generating documentation")
endif()

find_program(GPERF_EXECUTABLE gperf REQUIRED)

# HarfBuzz shapes the text, reads the font tables everything is measured
# against, and cuts each face down to the glyphs a document used. There is
# deliberately no system font database and no vector graphics library: the
# engine indexes its own font tree, keeps the bytes resident, draws its own
# lines directly as PDF operators, and writes the PDF itself -- which is
# faster than asking for any of that, and the same on every machine.
#
# An image is different: a JPEG's entropy coding and a PNG's filtering are not
# something a document engine should reimplement, so \includegraphics reaches
# for the one library each format already has -- decoded once, to plain
# pixels, and re-compressed with zlib on the way into the PDF, which is the
# one part of this that is cheap to do once rather than fast to skip.
find_package(harfbuzz CONFIG QUIET)
if(NOT harfbuzz_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz harfbuzz-subset)
endif()

# HarfBuzz is most of a run: every word goes through its shaper and every
# embedded face through its subsetter. Its debug build is several times slower
# than its release one and nobody steps into it, so a debug build of the
# engine links the release library instead. The two C runtimes never meet --
# HarfBuzz allocates and frees everything on its own side of a C interface --
# and the only thing to get right is which DLL sits beside the executable.
# vcpkg's own post-build step copies the debug ones there; each target below
# copies the release ones back over them afterwards, together with the
# FreeType, zlib, bzip2, libpng and Brotli builds they load in turn.
set(dependencies "")
if(TARGET harfbuzz::harfbuzz AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    get_target_property(shaping harfbuzz::harfbuzz ALIASED_TARGET)
    if(NOT shaping)
        set(shaping harfbuzz::harfbuzz)
    endif()
    set_target_properties(${shaping} harfbuzz::harfbuzz-subset PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)

    foreach(library harfbuzz harfbuzz-subset freetype z bz2 libpng16 brotlidec brotlicommon)
        set(file "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${library}.dll")
        if(EXISTS "${file}")
            list(APPEND dependencies "${file}")
        endif()
    endforeach()
endif()

find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

file(MAKE_DIRECTORY "${binary}/assets")
file(MAKE_DIRECTORY "${binary}/include")

set(input "${source}/assets/unicodes.gperf")
set(output "${binary}/include/lookup.hpp")

add_custom_command(
        OUTPUT ${output}
        COMMAND ${GPERF_EXECUTABLE} -t -C -N get -Z Lookup -L C++ ${input} --output-file=${output}
        DEPENDS ${input}
        VERBATIM
)

add_custom_target(lookup DEPENDS ${output})

# The prelude and every package, compiled into the engine as the files they
# are. Each .mtex file under src/modules becomes one #embed line in the
# listing written here, and src/syntax/modules.cpp includes the listing and
# indexes it in a table the compiler builds (memory/catalog.hpp). Nothing but
# the compiler reads the files, and a package added is a file added: the glob
# sees it the next time the build runs.
set(modules "${source}/src/modules")
file(GLOB_RECURSE files CONFIGURE_DEPENDS "${modules}/*.mtex")
list(SORT files)
list(LENGTH files count)

set(listing "// Written by CMakeLists.txt: every file under src/modules, embedded whole.\n")
set(entries "")
set(index 0)
foreach(file IN LISTS files)
    file(RELATIVE_PATH name "${modules}" "${file}")
    string(APPEND listing "static constexpr char file${index}[] = {\n#embed \"${file}\" suffix(,)\n0};\n")
    string(APPEND entries "    {\"${name}\", {file${index}, sizeof file${index} - 1}},\n")
    math(EXPR index "${index} + 1")
endforeach()
string(APPEND listing "\nstatic constexpr memory::Catalog<${count}> catalog{{{\n${entries}}}};\n")

# Rewritten only when the list of files changes, so a configure run does not
# recompile the prelude for nothing; a file's own edits reach the engine
# through the dependency below, since #embed is not a header the build scans.
file(CONFIGURE OUTPUT "${binary}/include/modules.inc" CONTENT "${listing}" @ONLY)
set_source_files_properties("${source}/src/syntax/modules.cpp" PROPERTIES OBJECT_DEPENDS "${files}")

# The font tree's listing, taken the same way: a run registers every face from
# it without walking the directory, and a face added to the tree is in the
# next build's listing. The files themselves are still read at run time, and
# only the ones a document uses.
set(typefaces "${source}/assets/fonts")
file(GLOB_RECURSE faces CONFIGURE_DEPENDS RELATIVE "${typefaces}"
        "${typefaces}/*.otf" "${typefaces}/*.ttf" "${typefaces}/*.ttc" "${typefaces}/*.pfb")
list(SORT faces)
list(LENGTH faces total)
set(roster "// Written by CMakeLists.txt: every font file under assets/fonts.\n")
string(APPEND roster "static constexpr std::array<std::string_view, ${total}> faces{{\n")
foreach(face IN LISTS faces)
    string(APPEND roster "    \"${face}\",\n")
endforeach()
string(APPEND roster "}};\n")
file(CONFIGURE OUTPUT "${binary}/include/fonts.inc" CONTENT "${roster}" @ONLY)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${source}/src/*.cpp"
        "${source}/src/*.c"
)

set(engine "${sources}")
list(REMOVE_ITEM engine "${source}/src/main.cpp" "${source}/src/network/ffi.cpp" "${source}/src/render/wasm.cpp")

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

# English hyphenation is compiled in the same way: src/render/typography/
# patterns.cpp embeds the pattern file as it stands in assets/hyphens, and the
# compiler builds its trie from those bytes, so a run builds nothing. That is
# a few million steps of constant evaluation, past what Clang and GCC allow
# one expression by default. Any other language is read from assets/hyphens
# at run time.
set(evaluation "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(evaluation "/clang:-fconstexpr-steps=100000000")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(evaluation "-fconstexpr-steps=100000000")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(evaluation "-fconstexpr-ops-limit=4294967296" "-fconstexpr-loop-limit=1048576")
endif()
set_source_files_properties("${source}/src/render/typography/patterns.cpp" PROPERTIES
        OBJECT_DEPENDS "${source}/assets/hyphens/hyph-en-us.pat.txt"
        COMPILE_OPTIONS "${evaluation}")

# The glossary is compiled in the same way: src/syntax/glossary.cpp embeds
# assets/glossary.mtex and the compiler splits it into a table by name.
set_source_files_properties("${source}/src/syntax/glossary.cpp" PROPERTIES
        OBJECT_DEPENDS "${source}/assets/glossary.mtex"
        COMPILE_OPTIONS "${evaluation}")

# The engine, compiled once, as the target core: the command line -- the
# target latex, the program an IDE runs -- the foreign-function library, the
# WebAssembly module and every test link this rather than each compiling
# all of it again, which is what used to make a full build take twenty
# minutes for ten small tests. Position independent, because the
# foreign-function library is a shared one and has to be able to carry it --
# except under Emscripten, which links everything into one module.
add_library(core STATIC ${engine} ${headers} ${output})
add_dependencies(core lookup)
if(NOT EMSCRIPTEN)
    set_target_properties(core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(core PUBLIC ${includes})

if(TARGET harfbuzz::harfbuzz)
    target_link_libraries(core PUBLIC harfbuzz::harfbuzz harfbuzz::harfbuzz-subset)
else()
    target_link_libraries(core PUBLIC ${HARFBUZZ_LIBRARIES})
    target_include_directories(core PUBLIC ${HARFBUZZ_INCLUDE_DIRS})
endif()

# PNG, JPEG and WebP decode an \includegraphics source into plain pixels;
# zlib recompresses them for the PDF. An https:// source is fetched by the
# HTTP client each platform already has: WinHTTP on Windows, and libcurl on
# Linux and macOS when it is installed. Without one, network/http.cpp reads
# files alone -- which is also the case in a browser, where the page fetches
# a picture and hands it in.
target_link_libraries(core PUBLIC PNG::PNG JPEG::JPEG WebP::webp ZLIB::ZLIB)

# Whether this is a release build, told to everything that links the engine:
# logger.hpp keys its floor on it, and the tests, which undefine NDEBUG so
# their asserts run, must see the same floor the engine was compiled with.
target_compile_definitions(core PUBLIC $<$<NOT:$<CONFIG:Debug>>:LATEX_RELEASE>)
if(WIN32)
    target_link_libraries(core PUBLIC winhttp)
elseif(NOT EMSCRIPTEN)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_link_libraries(core PUBLIC CURL::libcurl)
        target_compile_definitions(core PRIVATE LATEX_CURL)
    endif()
endif()

if(EMSCRIPTEN)
    # The engine as a WebAssembly module, for a web page, a worker or Node:
    # see include/render/wasm.hpp. Configure with the `wasm` preset, which
    # builds every library for the wasm32-emscripten triplet, and the build
    # leaves latex.js, latex.wasm and latex.data -- the fonts, packed into
    # the module's own file system where the engine looks for them.
    set(fonts "${source}/assets/fonts" CACHE PATH "The fonts packed into the WebAssembly module")

    add_executable(wasm "${source}/src/render/wasm.cpp" "${source}/include/render/wasm.hpp")
    target_link_libraries(wasm PRIVATE core)
    target_link_options(wasm PRIVATE
            -lembind
            -sMODULARIZE=1
            -sEXPORT_NAME=Latex
            -sALLOW_MEMORY_GROWTH=1
            -sENVIRONMENT=web,worker,node
            "SHELL:--preload-file ${fonts}@/assets/fonts")
    set_target_properties(wasm PROPERTIES OUTPUT_NAME latex)
else()
    add_executable(latex "${source}/src/main.cpp")
    target_link_libraries(latex PRIVATE core)
    # What `latex --version` prints.
    target_compile_definitions(latex PRIVATE LATEX_VERSION="${PROJECT_VERSION}")

    # The engine as a C library, for a program that wants documents made
    # without running a second process: see include/network/ffi.hpp. Named
    # latex_ffi so that its import library does not collide with the one the
    # executable leaves behind. It finds its assets from where it was loaded,
    # which on Linux and macOS takes dladdr and so the dynamic-loading library.
    add_library(ffi SHARED "${source}/src/network/ffi.cpp" "${source}/include/network/ffi.hpp")
    target_link_libraries(ffi PRIVATE core ${CMAKE_DL_LIBS})
    target_compile_definitions(ffi PRIVATE LATEX_EXPORTS)
    set_target_properties(ffi PROPERTIES OUTPUT_NAME latex_ffi)

    # Loaded the first time a document needs them rather than when the program
    # starts: most documents draw no JPEG or WebP picture and fetch nothing
    # over HTTP, and every library loaded at start-up is paid for by all.
    if(WIN32 AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        foreach(target latex ffi)
            target_link_options(${target} PRIVATE /DELAYLOAD:jpeg62.dll /DELAYLOAD:libwebp.dll
                    /DELAYLOAD:winhttp.dll)
            target_link_libraries(${target} PRIVATE delayimp)
        endforeach()
    endif()

    # After vcpkg's copy, not instead of it: see `dependencies` above.
    if(dependencies)
        foreach(target latex ffi)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dependencies} "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
        endforeach()
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()