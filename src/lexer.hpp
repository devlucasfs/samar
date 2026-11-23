#pragma once

#include <cctype>
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
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
X(Unkown) \
X(Eof)

#define E(x) x,
enum class Kind { KIND_TABLE(E) };

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

    Tk(Kind kind, std::string lex, int line, int col)
        : kind(kind), lex(std::move(lex)), line(line), col(col) {}
    Tk() = default;
    ~Tk() = default;
};

#define QUOTE '"'
#define COLON ':'
#define CRASIS '`'
#define OBRACE '{'
#define CBRACE '}'
#define SIGN '='
#define MORETHAN '>'
#define SLASH '/'

struct Src {
    FILE *f;
    int line = 1, col = 1;
    int last_line = 1, last_col = 1;

    explicit Src(FILE *f) : f(f) {}

    int get() {
        int c = getc(f);
        last_line = line;
        last_col = col;
        if( c == '\n' ) { line++; col = 1; }
        else if( c != EOF ) col++;
        return c;
    }

    void unget(int c) {
        if( c == EOF ) return;
        ungetc(c, f);
        line = last_line;
        col = last_col;
    }
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

inline void check(std::vector<Tk>& vec, std::stringstream& ss, int line, int col) {
    std::string val = ss.str();
    ss.str(""); ss.clear();

    if( val.empty() ) return;

    static const std::string_view keywords[] = {
        "architecture", "rule", "runner",
        "override", "dependencies", "die",
        "log", "when", "$os", "-eq", "amends"
    };

    for( auto sv : keywords ) {
        if( sv != val ) continue;
        vec.push_back(Tk(Kind::Keyword, val, line, col));
        return;
    }

    static const std::regex identifier("[a-zA-Z_][_a-zA-Z0-9]*");
    if( std::regex_match(val, identifier) ) {
        vec.push_back(Tk(Kind::Identifier, val, line, col));
        return;
    }

    vec.push_back(Tk(Kind::Unkown, val, line, col));
}

inline std::vector<Tk> lexer(FILE *file) {
    std::vector<Tk> vec;
    std::stringstream ss;
    Src src(file);

    int start_line = 0, start_col = 0;

    auto append = [&](int ch) {
        if( ss.str().empty() ) {
            start_line = src.last_line;
            start_col  = src.last_col;
        }
        ss << (char)ch;
    };

    auto flush = [&]() { check(vec, ss, start_line, start_col); };

    while( true ) {
        int ch = src.get();
        if( ch == EOF ) break;

        int cl = src.last_line, cc = src.last_col;

        if( ch == OBRACE ) {
            flush();
            vec.push_back(Tk(Kind::OBrace, "{", cl, cc));
            continue;
        }

        if( ch == CBRACE ) {
            flush();
            vec.push_back(Tk(Kind::CBrace, "}", cl, cc));
            continue;
        }

        if( ch == SLASH ) {
            int n = src.get();
            if( n == SLASH ) {
                flush();
                while( (n = src.get()) != EOF && n != '\n' );
                continue;
            }
            src.unget(n);
            append(ch);
            continue;
        }

        if( std::isspace((unsigned char)ch) ) {
            flush();
            continue;
        }

        if( ch == COLON ) {
            int n = src.get();
            if( n == COLON ) {
                flush();
                vec.push_back(Tk(Kind::Quad, "::", cl, cc));
                continue;
            }
            src.unget(n);
            append(ch);
            continue;
        }

        if( ch == SIGN ) {
            int n = src.get();
            if( n == MORETHAN ) {
                flush();
                vec.push_back(Tk(Kind::Arrow, "=>", cl, cc));
                continue;
            }
            src.unget(n);
            append(ch);
            continue;
        }

        if( ch == QUOTE ) {
            flush();

            std::string s;
            int ch2;

            while( true ) {
                while( (ch2 = src.get()) != QUOTE && ch2 != EOF ) s += (char)ch2;
                if( ch2 == EOF ) break;

                while( (ch2 = src.get()) != EOF && std::isspace((unsigned char)ch2) );
                if( ch2 == QUOTE ) { s += ' '; continue; }

                src.unget(ch2);
                break;
            }

            vec.push_back(Tk(Kind::String, s, cl, cc));
            continue;
        }

        if( ch == CRASIS ) {
            flush();

            std::string s;
            int ch2;
            while( (ch2 = src.get()) != CRASIS && ch2 != EOF ) s += (char)ch2;

            vec.push_back(Tk(Kind::Runner, s, cl, cc));
            continue;
        }

        append(ch);
    }

    flush();
    vec.push_back(Tk(Kind::Eof, "<eof>", src.line, src.col));

    std::vector<Tk> v2;
    for( auto& v : vec ) {
        if( v.kind == Kind::Unkown ) continue;
        v2.push_back(v);
    }

    return v2;
}
