// Includes da biblioteca padrao ANTES dos headers do Samar.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mangled.hpp"
#include "common.hpp"
#include "lexer.hpp"
#include "interpreter.hpp"

#define SAMAR_API extern "C" __attribute__((visibility("default")))
SAMAR_API SamarState* __samar_load(cstring file);

#ifdef SAMAR_DEBUG
  #define SAMAR_TRACE(msg) (std::cerr << "[samar] " << msg << "\n")
#else
  #define SAMAR_TRACE(msg) ((void)0)
#endif

[[noreturn]] static void samar_fatal(const char* where, const char* what) {
    std::cerr << "Samar failure (" << where << "): " << what << "\n";
    std::exit(14);
}

SAMAR_API
void mangled(push_arg) (SamarState *state, cstring variable, cstring value) {
    try {
        state->args.push_back({ std::string(variable), std::string(value) });
    }
    catch(const std::exception& e) { samar_fatal("push_arg", e.what()); }
    catch(...)                     { samar_fatal("push_arg", "unknown exception"); }
}

static std::optional<std::pair<std::string, std::string>>
rsplit_once(const std::string& s, const std::string& sep) {
    std::size_t pos = s.rfind(sep);
    if( pos == std::string::npos ) return std::nullopt;
    return std::pair{ s.substr(0, pos), s.substr(pos + sep.size()) };
}

SAMAR_API
void mangled(call_runner) (SamarState *state, cstring abi) {
    try {
        std::string cxx(abi);

        auto parts = rsplit_once(cxx, "::");
        if( !parts )
            emissor("Invalid runner abi `" + cxx + "`: expected `namespace::runner`.");

        auto& [ns, name] = *parts;

        if(! state->runners.contains(cxx) )
            emissor("Runner `" + name + "` was not found in `" + ns + "`.");

        auto runner_block = state->runners.at(cxx);
        if( state->dependenciess.contains(ns) ) {
            auto block = state->dependenciess.at(ns);
            parse_dependencies_entries(state, block);
        }

        parser_over_vec(state, runner_block);
        state->args.clear();
    }
    catch(const std::exception& e) { samar_fatal("call_runner", e.what()); }
    catch(...)                     { samar_fatal("call_runner", "unknown exception"); }
}

SAMAR_API
SamarState* mangled(load) (cstring file) {
    try {
        SAMAR_TRACE("load");

        FILE* f = std::fopen(file, "r");
        if( !f )
            emissor(std::string("Cannot open the build file `") + file + "`.");
        SAMAR_TRACE("open");

        std::vector<Tk> tks;
        try {
            tks = lexer(f);
        } catch(...) {
            std::fclose(f);
            throw;
        }
        std::fclose(f);
        SAMAR_TRACE("lexer: " << tks.size() << " tokens");

        SamarState* state = parser(std::move(tks));
        SAMAR_TRACE("parser");
        return state;
    }
    catch(const std::exception& e) { samar_fatal("load", e.what()); }
    catch(...)                     { samar_fatal("load", "unknown exception"); }
}
