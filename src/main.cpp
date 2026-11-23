#include "mangled.hpp"
#include "common.hpp"
#include "lexer.hpp"
#include "interpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <optional>
#include <utility>

extern "C"
void mangled(push_arg) (SamarState *state, cstring variable, cstring value) {
    state->args.push_back({ std::string(variable), std::string(value) });
}

static std::optional<std::pair<std::string, std::string>>
rsplit_once(const std::string& s, const std::string& sep) {
    std::size_t pos = s.rfind(sep);
    if( pos == std::string::npos ) return std::nullopt;
    return std::pair{ s.substr(0, pos), s.substr(pos + sep.size()) };
}

extern "C"
void mangled(call_runner) (SamarState *state, cstring abi) {
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

extern "C"
SamarState* mangled(load) (cstring file) {
    auto f = std::fopen(file, "r+");
    std::vector<Tk> tks = lexer(f);
    std::fclose(f);
    // lex_dbg(tks);
    try {
        return parser(tks);
    } catch(std::runtime_error err) {
        std::cerr << "Samar failure: " << err.what() << "\n";
        std::exit(14);
    }
}
// rafael esteve aqui
