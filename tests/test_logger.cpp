#include "logger.hpp"

#include <cassert>

// The Logger every part of the engine reports through: which subsystems
// report, from which level up, chosen by a program or by --debug= on the
// command line -- and a release build that keeps no debug reports at all.

int main() {
    Logger::types(Logger::Type::Lexer);
    Logger::level(Logger::Level::Warning);
    assert((Logger::check(Logger::Type::Lexer, Logger::Level::Error)) && "a subsystem asked for reports its errors");
    assert((!Logger::check(Logger::Type::Parser, Logger::Level::Error)) && "one not asked for does not");
    assert((!Logger::check(Logger::Type::Lexer, Logger::Level::Informative)) && "nor anything below the level");

    char program[] = "latex";
    char option[] = "--debug=parser,layout";
    char* arguments[] = {program, option};
    Logger::compose(2, arguments);
    assert((Logger::check(Logger::Type::Parser, Logger::Level::Error) &&
           Logger::check(Logger::Type::Layout, Logger::Level::Error)) && "--debug= names the subsystems reported");
    assert((!Logger::check(Logger::Type::Lexer, Logger::Level::Error)) && "and only those");
    if constexpr (Logger::floor <= Logger::Level::Debug) {
        assert((Logger::check(Logger::Type::Parser, Logger::Level::Debug)) && "at the debug level, in a debug build");
    } else {
        assert((!Logger::check(Logger::Type::Parser, Logger::Level::Debug)) &&
               "a release build keeps no debug reports");
    }
    assert((Logger::name(Logger::Type::Lexer) == "Lexer" && !Logger::name(Logger::Level::Warning).empty()) &&
           "every subsystem and level has a name");

    char* none[] = {program};
    Logger::compose(1, none);
    assert((!Logger::check(Logger::Type::Parser, Logger::Level::Error)) && "with no options, nothing is reported");

    return 0;
}
