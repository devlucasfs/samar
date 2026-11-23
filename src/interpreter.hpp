#include "lexer.hpp"
#include "platform.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#ifdef _WIN32
  #include <algorithm>
#else
  #include <unistd.h>
#endif

namespace fs = std::filesystem;
struct SamarState {
    std::vector<std::string> architectures;
    std::unordered_map<std::string, std::vector<Tk>> rules;
    std::unordered_map<std::string, std::vector<Tk>> runners;
    std::unordered_map<std::string, std::vector<Tk>> dependenciess;
    std::vector<std::pair<std::string, std::string>> args;
    std::string at, ns;

    SamarState() = default;
};

#define GTK(j) vec.at(j)

#define SAMAR_PARSER_ARGS_EXPORT state, tk, vec, i
#define SAMAR_PARSER_ARGS_SUB(V) state, tk, V,   i
#define SAMAR_PARSER_ARGS_IMPORT SamarState *state, Tk& tk, std::vector<Tk>& vec, std::size_t& i

#define architecture "architecture"
#define dependencies "dependencies"
#define override_key "override"
#define runner "runner"
#define amends "amends"
#define when "when"
#define rule "rule"
#define log "log"
#define die "die"

void parser_over_vec(SamarState *state, std::vector<Tk>& vec);

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep))
        if (!item.empty()) out.push_back(item);
    return out;
}

static bool is_executable(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) return false;
#ifdef _WIN32
    return true;
#else
    return access(p.c_str(), X_OK) == 0;
#endif
}

bool check_for_cli(const std::string& nome) {
    if( nome.empty() ) return false;
#ifdef _WIN32
    constexpr char SEP = ';';
    std::vector<std::string> exts{""};
    const char* pathext = std::getenv("PATHEXT");
    for( auto& e : split(pathext ? pathext : ".COM;.EXE;.BAT;.CMD", SEP) )
        exts.push_back(e);
#else
    constexpr char SEP = ':';
    const std::vector<std::string> exts{""};
#endif

    if( nome.find('/') != std::string::npos || nome.find('\\') != std::string::npos ) {
        for( auto& e : exts )
            if( is_executable(fs::path(nome + e)) ) return true;
        return false;
    }

    const char* path = std::getenv("PATH");
    if(! path ) return false;

    for( auto& dir : split(path, SEP) )
        for( auto& e : exts )
            if (is_executable(fs::path(dir) / (nome + e))) return true;

    return false;
}

inline void emissor(std::string str) {
    throw std::runtime_error(str);
}

static std::vector<Tk> collect_block(std::vector<Tk>& vec, std::size_t& i) {
    std::vector<Tk> block;
    int depth = 1;
    while( depth > 0 ) {
        if( i + 1 >= vec.size() )
            emissor("Unterminated block: missing `}`.");
        Tk current = GTK(++i);
        if( current.kind == Kind::OBrace )      depth++;
        else if( current.kind == Kind::CBrace ) depth--;
        if( depth > 0 ) block.push_back(current);
    }
    return block;
}

static bool parse_when_condition(std::vector<Tk>& vec, std::size_t& i) {
    auto k = GTK(++i);
    if( k.kind != Kind::Keyword )
        emissor("The first argument need to be a keyword");

    std::string lhs;
    if( k.lex == "$os" ) lhs = platform();
    else emissor("Invalid keyword used.");

    auto eq = GTK(++i);
    if( eq.kind != Kind::Keyword || eq.lex != "-eq" )
        emissor("Expected the -eq keyword. Found " + eq.lex);

    auto string = GTK(++i);
    if( string.kind != Kind::String )
        emissor("Expected a String to compare");

    return lhs == string.lex;
}

static std::vector<Tk> parse_when_block(std::vector<Tk>& vec, std::size_t& i, bool& matched) {
    matched = parse_when_condition(vec, i);

    auto brace = GTK(++i);
    if( brace.kind != Kind::OBrace )
        emissor("A when need a block. Make one opening a brance. `{`");

    return collect_block(vec, i);
}

bool contains_str(std::vector<std::string>& v, std::string s) {
    for( auto f : v ) {
        if( f == s ) return true;
    }
    return false;
}


static std::string expand(const std::string& in, SamarState *state) {
    std::string out;
    out.reserve(in.size());

    for( std::size_t k = 0; k < in.size(); k++ ) {
        char c = in[k];

        if( c == '{' ) {
            if( !state->at.empty() && in.compare(k, 3, "{@}") == 0 ) {
                out += state->at;
                k += 2;
                continue;
            }

            if( in.compare(k, 10, "{env:HOME}") == 0 ) {
                if( const char* home = getenv("HOME") )
                    out += home;

                k += 9;
                continue;
            }

            std::size_t end = in.find('}', k + 1);

            if( end != std::string::npos ) {
                std::string label = in.substr(k + 1, end - k - 1);

                for( const auto& [key, value] : state->args ) {
                    if( key == label ) {
                        out += value;
                        k = end;
                        goto expanded;
                    }
                }
            }
        }

        if( c == '\\' && k + 1 < in.size() ) {
            switch( in[k + 1] ) {
                case 'n':  out += '\n';   k++; continue;
                case 't':  out += '\t';   k++; continue;
                case 'r':  out += '\r';   k++; continue;
                case 'a':  out += '\a';   k++; continue;
                case 'b':  out += '\b';   k++; continue;
                case 'f':  out += '\f';   k++; continue;
                case 'v':  out += '\v';   k++; continue;
                case '0':  out += '\0';   k++; continue;
                case 'e':  out += '\x1b'; k++; continue;
                case '\\': out += '\\';   k++; continue;
                case '"':  out += '"';    k++; continue;
                case '\'': out += '\'';   k++; continue;
                default: break;
            }
        }

        out += c;

        expanded:;
    }

    return out;
}


static std::size_t utf8_len(const std::string& s) {
    std::size_t n = 0;
    for( unsigned char c : s )
        if( (c & 0xC0) != 0x80 ) n++;
    return n;
}

static void emit(const std::string& line) {
    std::cerr << line << "\n";
}

void print_wrapped(const std::string& text, std::size_t width) {
    std::string word, line;
    std::size_t len = 0;

    auto flush_word = [&]() {
        if( word.empty() ) return;
        std::size_t wlen = utf8_len(word);

        if( len > 0 && len + 1 + wlen > width ) {
            emit(line);
            line.clear();
            len = 0;
        }

        if( len > 0 ) {
            line += ' ';
            len++;
        }

        line += word;
        len += wlen;
        word.clear();
    };

    for( char c : text ) {
        if( c == '\n' ) {
            flush_word();
            emit(line);
            line.clear();
            len = 0;
        }
        else if( c == ' ' || c == '\t' || c == '\r' ) {
            flush_word();
        }
        else {
            word += c;
        }
    }

    flush_word();
    if( !line.empty() ) emit(line);
}

constexpr int WIDTH_PRINT = 72;

void parse_log(SAMAR_PARSER_ARGS_IMPORT) {
    auto string = GTK(++i);
    if( string.kind != Kind::String )
        emissor("After a log keyword you need to inform a string to be logged.");

    print_wrapped("LOG: " + expand(string.lex, state) + "\n", WIDTH_PRINT);
}

void parse_die(SAMAR_PARSER_ARGS_IMPORT) {
    auto string = GTK(++i);
    if( string.kind != Kind::String )
        emissor("After a die keyword you need to inform a string with the reason the program died.");

    print_wrapped("Died because " + expand(string.lex, state) + "\n", WIDTH_PRINT);
    std::exit(14);
}

void parse_architecture(SAMAR_PARSER_ARGS_IMPORT) {
    auto arch = GTK(++i);
    if( arch.kind != Kind::Identifier )
        emissor("After an architecture keyword you need to inform an identifier for the architecture.");

    if( contains_str(state->architectures, arch.lex) )
        emissor(arch.lex + " is already an architecture.");

    state->architectures.push_back(arch.lex);
}

void parse_rule(SAMAR_PARSER_ARGS_IMPORT) {
    auto identifier = GTK(++i);
    if( identifier.kind != Kind::Identifier )
        emissor("After an rule keyword you need to inform an identifier for the rule identification.");

    auto brace = GTK(++i);
    if( brace.kind != Kind::OBrace )
        emissor("A rule need a block. Make one opening a brance. `{`");

    Tk current;
    std::vector<Tk> block;
    while( (current = GTK(i++)).kind != Kind::CBrace )
        block.push_back(current);

    state->rules.insert({ identifier.lex, block });
    i--;
}

static void parse_dependencies_entries(SamarState *state, std::vector<Tk>& block) {
    for( std::size_t i = 0; i < block.size(); i++ ) {
        if( block.at(i).kind == Kind::Keyword && block.at(i).lex == when ) {
            bool matched = false;
            auto inner = parse_when_block(block, i, matched);
            if( matched ) parse_dependencies_entries(state, inner);
            continue;
        }

        auto cli = block.at(i++);
        if( cli.kind != Kind::Identifier )
            emissor("Expected an Identifier: The CLI name; Right syntax: cli => rule : " + cli.lex);

        auto arr = block.at(i++);
        if( arr.kind != Kind::Arrow )
            emissor("Expected an Arrow; Right syntax: cli => rule");

        auto ruleid = block.at(i);
        if( ruleid.kind != Kind::Identifier || !state->rules.contains(ruleid.lex) )
            emissor("Expected an Identifier: Rule identifier; Right syntax: cli => rule");

        if( check_for_cli(cli.lex) ) continue;

        state->at = cli.lex;
        parser_over_vec(state, state->rules.at(ruleid.lex));
    }
}

void parse_dependencies(SAMAR_PARSER_ARGS_IMPORT, bool override) {
    auto brace = GTK(++i);
    if( brace.kind != Kind::OBrace )
        emissor("A dependencies keyword need a block. Make one opening a brance. `{`");

    auto block = collect_block(vec, i);
    if( state->dependenciess.contains(state->ns) && !override )
        emissor("That namespace already have an dependencies. You can to override it if you want but outside a namespace.");

    state->dependenciess[state->ns] = block;
}

void parse_runner(SAMAR_PARSER_ARGS_IMPORT, bool override, bool ns = false) {
    auto identifier = GTK(++i);
    if( identifier.kind != Kind::Identifier )
        emissor("After a runner keyword u need to enter with an Identifier.");

    auto brace = GTK(++i);
    if( brace.kind != Kind::OBrace )
        emissor("A runner keyword need a block. Make one opening a brance. `{`");

    Tk current;
    std::vector<Tk> block;
    while( (current = GTK(++i)).kind != Kind::CBrace )
        block.push_back(current);

    auto runner_name = state->ns + "::" + identifier.lex;
    if( state->runners.contains(runner_name) && !override )
        emissor("That runner is already known. You can to override it if you want but outside a namespace.");

    state->runners[runner_name] = block;
}

static void parse_namespace_body(SamarState *state, Tk& tk, std::vector<Tk>& block, bool override) {
    for( std::size_t k = 0; k < block.size(); k++ ) {
        const Tk& t = block.at(k);
        if( t.kind != Kind::Keyword ) continue;

        if( t.lex == dependencies ) parse_dependencies(state, tk, block, k, override);
        if( t.lex == runner ) parse_runner(state, tk, block, k, override, true);
        if( t.lex == when ) {
            bool matched = false;
            auto inner = parse_when_block(block, k, matched);
            if( matched ) parse_namespace_body(state, tk, inner, override);
        }
    }
}

void parse_namespace(SAMAR_PARSER_ARGS_IMPORT, bool override = false) {
    auto q = GTK(++i);
    if( q.kind != Kind::Quad )
        emissor("To make an namespace is needed inform the OS/MCU you are using.");

    auto identifier = GTK(++i);
    if( identifier.kind != Kind::Identifier )
        emissor("To the OS/MCU was expected an Identifier.");

    state->ns = tk.lex + "::" + identifier.lex;

    auto brace = GTK(++i);
    if( brace.kind != Kind::OBrace )
        emissor("A namespace need a block. Make one opening a brance. `{`");

    std::vector<Tk> block = collect_block(vec, i);
    parse_namespace_body(state, tk, block, override);
}

void parse_bash(SAMAR_PARSER_ARGS_IMPORT) {
    std::system(expand(tk.lex, state).c_str());
}

void parse_when(SAMAR_PARSER_ARGS_IMPORT) {
    bool matched = false;
    auto block = parse_when_block(vec, i, matched);
    if( matched ) parser_over_vec(state, block);
}

void parse_override(SAMAR_PARSER_ARGS_IMPORT) {
    parse_namespace(state, GTK(++i), vec, i, true);
}

void parse_amends(SAMAR_PARSER_ARGS_IMPORT) {
    auto string = GTK(++i);
    if( string.kind != Kind::String )
        emissor("After an amends keyword you need enter with a String which is the file you want to amend.");

    auto f = std::fopen(expand(string.lex, state).c_str(), "r+");
    std::vector<Tk> tks = lexer(f);
    std::fclose(f);

    parser_over_vec(state, tks);
}

#define c(id) { id(SAMAR_PARSER_ARGS_EXPORT); continue; }
void parser_over_vec(SamarState *state, std::vector<Tk>& vec) {
    for( std::size_t i = 0; i < vec.size(); i++ ) {
        auto tk = GTK(i);

        if( tk.kind == Kind::Keyword && tk.lex == architecture ) c(parse_architecture);
        if( tk.kind == Kind::Keyword && tk.lex == override_key ) c(parse_override);
        if( tk.kind == Kind::Keyword && tk.lex == amends ) c(parse_amends);
        if( tk.kind == Kind::Keyword && tk.lex == when ) c(parse_when);
        if( tk.kind == Kind::Keyword && tk.lex == rule ) c(parse_rule);
        if( tk.kind == Kind::Keyword && tk.lex == log ) c(parse_log);
        if( tk.kind == Kind::Keyword && tk.lex == die ) c(parse_die);

        if( tk.kind == Kind::Identifier && contains_str(state->architectures, tk.lex) ) c(parse_namespace)

        if( tk.kind == Kind::Runner ) c(parse_bash)
    }
}

SamarState *parser(std::vector<Tk> vec) {
    SamarState *state = new SamarState();
    parser_over_vec(state, vec);
    return state;
}
