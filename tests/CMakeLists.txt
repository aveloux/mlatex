file(GLOB_RECURSE tests CONFIGURE_DEPENDS "*.cpp")

# One test per source file, at the same place in the tree -- src/syntax/lexer.cpp
# is tested by tests/syntax/test_lexer.cpp -- each standing on its own, and
# nothing else lives here. Two sources may share a name -- syntax/parser.cpp and
# syntax/expression/parser.cpp -- so a test is named for its file alone unless
# another shares that name, and then for its folders too: test_syntax_parser,
# test_syntax_expression_parser.
set(names "")
foreach(test ${tests})
    get_filename_component(name ${test} NAME_WE)
    list(APPEND names ${name})
endforeach()

foreach(test ${tests})
    get_filename_component(name ${test} NAME_WE)
    set(same "")
    foreach(other ${names})
        if(other STREQUAL name)
            list(APPEND same ${other})
        endif()
    endforeach()
    list(LENGTH same shared)
    if(shared GREATER 1)
        file(RELATIVE_PATH place ${CMAKE_CURRENT_SOURCE_DIR} ${test})
        get_filename_component(place ${place} DIRECTORY)
        string(REPLACE "/" "_" place ${place})
        string(REGEX REPLACE "^test_" "test_${place}_" name ${name})
    endif()

    # The engine is linked, not recompiled per test: see the engine library in
    # the top-level CMakeLists.txt.
    add_executable(${name} ${test})
    target_link_libraries(${name} PRIVATE latex)

    # A test checks with assert, which a release build compiles out: kept on
    # here, so a test built for speed still checks everything it says.
    target_compile_options(${name} PRIVATE $<IF:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>,/UNDEBUG,-UNDEBUG>)

    # The release HarfBuzz a debug build links, as the engine's own target does.
    if(dependencies)
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dependencies} "$<TARGET_FILE_DIR:${name}>"
                VERBATIM)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endforeach()

# The command line's test runs the executable itself, so it is told where the
# build put it, and waits for it to be built.
if(TARGET test_main)
    target_compile_definitions(test_main PRIVATE BINARY="$<TARGET_FILE:driver>")
    add_dependencies(test_main driver)
endif()

# The foreign-function test calls the engine the way a host program would,
# through the shared library and nothing else, so it links that and needs the
# library beside it when it runs.
if(TARGET test_ffi)
    target_link_libraries(test_ffi PRIVATE ffi)
    add_custom_command(TARGET test_ffi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ffi>" "$<TARGET_FILE_DIR:test_ffi>"
            VERBATIM)
endif()
