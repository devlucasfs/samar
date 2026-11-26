#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define KIND_TABLE(X) \
X(Keyword) \
X(String) \
X(Runner) \
X(Arrow) \
X(Quad) \
X(Identifier) \
X(OBrace) \
X(CBrace) \
X(OParen) \
X(CParen) \
X(Unkown) \
X(Eof)

#define QUOTE '"'
#define COLON ':'
#define CRASIS '`'
#define OBRACE '{'
#define CBRACE '}'
#define OPAREN '('
#define CPAREN ')'
#define SIGN '='
#define MORETHAN '>'
#define SLASH '/'

#pragma GCC visibility push(hidden)
inline namespace samar_v1 {

#define E(x) x,
enum class Kind { KIND_TABLE(E) };
#undef E

inline std::string_view kind_to_str(Kind k) {
    #define D(x) case Kind::x: return #x;
    switch(k) {
        KIND_TABLE(D)
    }
    #undef D
    return "";
}

struct Tk {
    Kind kind = Kind::Unkown;
    std::string lex;
    int line = 0;
    int col  = 0;

    Tk() = default;
    Tk(Kind kind, std::string lex, int line, int col)
        : kind(kind), lex(std::move(lex)), line(line), col(col) {}
};

inline void lex_dbg(const std::vector<Tk>& vec) {
    int i = 0;
    for( auto& tk : vec ) {
        std::cout << "Token " << i << " {\n"
                  << "  kind: " << kind_to_str(tk.kind) << "\n"
                  << "  lex: " << tk.lex << "\n"
                  << "  line: " << tk.line << "\n"
                  << "  col: " << tk.col << "\n"
                  << "}\n\n";
        i++;
    }
}

struct Src {
    std::string data;
    std::size_t pos = 0;
    int line = 1, col = 1;

    bool eof() const { return pos >= data.size(); }

    int peek() const {
        return eof() ? EOF : (unsigned char)data[pos];
    }

    int get() {
        if( eof() ) return EOF;
        unsigned char c = (unsigned char)data[pos++];
        if( c == '\n' ) { line++; col = 1; }
        else col++;
        return c;
    }
};

inline bool is_identifier(const std::string& s) {
    if( s.empty() ) return false;
    unsigned char c0 = (unsigned char)s[0];
    if( !(std::isalpha(c0) || c0 == '_') ) return false;
    for( unsigned char c : s )
        if( !(std::isalnum(c) || c == '_') ) return false;
    return true;
}

inline void flush_word(std::vector<Tk>& out, std::string& cur, int line, int col) {
    if( cur.empty() ) return;

    static const std::string_view keywords[] = {
        "architecture", "rule", "runner",
        "override", "dependencies", "die",
        "log", "when", "$os", "-eq", "-ne",
        "amends", "and", "or"
    };

    Kind kind = Kind::Unkown;
    for( auto sv : keywords ) {
        if( sv == cur ) { kind = Kind::Keyword; break; }
    }
    if( kind == Kind::Unkown && is_identifier(cur) )
        kind = Kind::Identifier;

    if( kind != Kind::Unkown )
        out.emplace_back(kind, std::move(cur), line, col);

    cur.clear();
}

inline std::vector<Tk> lexer_string(std::string text) {
    std::vector<Tk> out;
    std::string cur;
    Src src;
    src.data = std::move(text);

    int start_line = 0, start_col = 0;
    int cl = 0, cc = 0;

    try {
        while( !src.eof() ) {
            cl = src.line;
            cc = src.col;
            int ch = src.get();

            if( ch == OBRACE ) {
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::OBrace, "{", cl, cc);
                continue;
            }

            if( ch == CBRACE ) {
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::CBrace, "}", cl, cc);
                continue;
            }

            if( ch == OPAREN ) {
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::OParen, "(", cl, cc);
                continue;
            }

            if( ch == CPAREN ) {
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::CParen, ")", cl, cc);
                continue;
            }

            if( ch == SLASH && src.peek() == SLASH ) {
                flush_word(out, cur, start_line, start_col);
                int n;
                while( (n = src.get()) != EOF && n != '\n' );
                continue;
            }

            if( std::isspace((unsigned char)ch) ) {
                flush_word(out, cur, start_line, start_col);
                continue;
            }

            if( ch == COLON && src.peek() == COLON ) {
                src.get();
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::Quad, "::", cl, cc);
                continue;
            }

            if( ch == SIGN && src.peek() == MORETHAN ) {
                src.get();
                flush_word(out, cur, start_line, start_col);
                out.emplace_back(Kind::Arrow, "=>", cl, cc);
                continue;
            }

            if( ch == QUOTE ) {
                flush_word(out, cur, start_line, start_col);

                std::string s;
                while( true ) {
                    int c;
                    while( (c = src.get()) != QUOTE && c != EOF ) s += (char)c;
                    if( c == EOF ) break;

                    while( !src.eof() && std::isspace((unsigned char)src.peek()) ) src.get();

                    if( src.peek() == QUOTE ) {
                        src.get();
                        s += ' ';
                        continue;
                    }
                    break;
                }

                out.emplace_back(Kind::String, std::move(s), cl, cc);
                continue;
            }

            if( ch == CRASIS ) {
                flush_word(out, cur, start_line, start_col);

                std::string s;
                int c;
                while( (c = src.get()) != CRASIS && c != EOF ) s += (char)c;

                out.emplace_back(Kind::Runner, std::move(s), cl, cc);
                continue;
            }

            if( cur.empty() ) {
                start_line = cl;
                start_col  = cc;
            }
            cur += (char)ch;
        }

        flush_word(out, cur, start_line, start_col);
        out.emplace_back(Kind::Eof, "<eof>", src.line, src.col);
        return out;
    }
    catch (const std::bad_alloc&) {
        std::cerr << "[bad_alloc] no lexer | pos=" << src.line << ":" << src.col
                  << " | tokens=" << out.size()
                  << " | token_atual.size=" << cur.size() << "\n";
        throw;
    }
}

inline std::vector<Tk> lexer(FILE *file) {
    if( !file )
        throw std::runtime_error("lexer: FILE* is null");

    std::string text;
    char buf[4096];
    std::size_t n;
    while( (n = std::fread(buf, 1, sizeof buf, file)) > 0 )
        text.append(buf, n);

    return lexer_string(std::move(text));
}

}
#pragma GCC visibility pop

#undef QUOTE
#undef COLON
#undef CRASIS
#undef OBRACE
#undef CBRACE
#undef SIGN
#undef MORETHAN
#undef SLASH
#undef KIND_TABLE
