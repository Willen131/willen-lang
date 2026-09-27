#include "lexer.h"
#include <cctype>
#include <iostream>

// 标识符首字符：字母、下划线，或任何非 ASCII 字节。
// 放宽到 u >= 0x80 是中文标识符与中文关键字能够工作的关键——
// UTF-8 多字节序列的每个字节都 >= 0x80，会被逐字节读入后拼成完整字符。
static bool isIdentStart(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_' || u >= 0x80;
}

static bool isIdentPart(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || u >= 0x80;
}

// Willen 语言的关键字表。新增关键字只需在此登记。
const std::unordered_map<std::string, Tok> KEYWORDS = {
    {"设", Tok::KW_LET},        {"常量", Tok::KW_CONST},
    {"如果", Tok::KW_IF},       {"否则如果", Tok::KW_ELSEIF},
    {"否则", Tok::KW_ELSE},     {"当", Tok::KW_WHILE},
    {"遍历", Tok::KW_FOR},      {"在", Tok::KW_IN},
    {"重复", Tok::KW_REPEAT},   {"次", Tok::KW_TIMES},
    {"函数", Tok::KW_FUNC},     {"返回", Tok::KW_RETURN},
    {"结构", Tok::KW_STRUCT},   {"跳出", Tok::KW_BREAK},
    {"继续", Tok::KW_CONTINUE}, {"真", Tok::KW_TRUE},
    {"假", Tok::KW_FALSE},      {"空", Tok::KW_NULL},
    {"且", Tok::KW_AND},        {"或", Tok::KW_OR},
    {"非", Tok::KW_NOT},
};

std::string tokName(Tok t) {
    switch (t) {
#define CASE(name) case Tok::name: return #name
        CASE(INT_LIT); CASE(FLOAT_LIT); CASE(STR_LIT); CASE(IDENT);
        CASE(KW_LET); CASE(KW_CONST);
        CASE(KW_IF); CASE(KW_ELSEIF); CASE(KW_ELSE);
        CASE(KW_WHILE); CASE(KW_FOR); CASE(KW_IN);
        CASE(KW_REPEAT); CASE(KW_TIMES);
        CASE(KW_FUNC); CASE(KW_RETURN); CASE(KW_STRUCT);
        CASE(KW_BREAK); CASE(KW_CONTINUE);
        CASE(KW_TRUE); CASE(KW_FALSE); CASE(KW_NULL);
        CASE(KW_AND); CASE(KW_OR); CASE(KW_NOT);
        CASE(PLUS); CASE(MINUS); CASE(STAR); CASE(SLASH); CASE(PERCENT);
        CASE(ASSIGN); CASE(PLUS_ASSIGN); CASE(MINUS_ASSIGN);
        CASE(STAR_ASSIGN); CASE(SLASH_ASSIGN);
        CASE(EQ); CASE(NEQ); CASE(LT); CASE(LE); CASE(GT); CASE(GE);
        CASE(LPAREN); CASE(RPAREN); CASE(LBRACKET); CASE(RBRACKET);
        CASE(LBRACE); CASE(RBRACE);
        CASE(COMMA); CASE(SEMICOLON); CASE(DOT); CASE(DOTDOT); CASE(COLON);
        CASE(END);
#undef CASE
    }
    return "UNKNOWN";
}

Lexer::Lexer(std::string src, std::string file)
    : src_(std::move(src)), file_(std::move(file)) {}

char Lexer::peek(size_t offset) const {
    size_t i = pos_ + offset;
    return i < src_.size() ? src_[i] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; } else { col_++; }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || src_[pos_] != expected) return false;
    advance();
    return true;
}

Token Lexer::makeToken(Tok type, const std::string& text, int line, int col) const {
    Token t;
    t.type = type;
    t.text = text;
    t.line = line;
    t.col = col;
    return t;
}

void Lexer::error(const std::string& message, int line) {
    std::cerr << "第 " << line << " 行：" << message << "\n";
    hadError_ = true;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '#') {
            while (!atEnd() && peek() != '\n') advance();
        } else {
            break;
        }
    }
}

Token Lexer::identOrKeyword() {
    int line = line_, col = col_;
    std::string text;
    while (!atEnd() && isIdentPart(peek())) text += advance();

    auto it = KEYWORDS.find(text);
    if (it != KEYWORDS.end()) return makeToken(it->second, text, line, col);
    return makeToken(Tok::IDENT, text, line, col);
}

Token Lexer::number() {
    int line = line_, col = col_;
    std::string text;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }

    // 只有在小数点后紧跟数字时才当作小数，
    // 这样范围语法 "0..9" 不会被误切成 "0." + ".9"。
    bool isFloat = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        isFloat = true;
        text += advance();  // 小数点
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }
    return makeToken(isFloat ? Tok::FLOAT_LIT : Tok::INT_LIT, text, line, col);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    // 跳过 UTF-8 BOM。带 BOM 的文件若不处理，首个关键字会被识别成
    // 一个以 BOM 开头的标识符。
    if (src_.size() >= 3 &&
        static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF) {
        pos_ = 3;
    }

    while (!atEnd()) {
        skipWhitespaceAndComments();
        if (atEnd()) break;

        int line = line_, col = col_;
        char c = peek();

        if (isIdentStart(c)) {
            tokens.push_back(identOrKeyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(number());
            continue;
        }

        advance();
        switch (c) {
            case '=': tokens.push_back(makeToken(Tok::ASSIGN, "=", line, col)); break;
            case '(': tokens.push_back(makeToken(Tok::LPAREN, "(", line, col)); break;
            case ')': tokens.push_back(makeToken(Tok::RPAREN, ")", line, col)); break;
            case '{': tokens.push_back(makeToken(Tok::LBRACE, "{", line, col)); break;
            case '}': tokens.push_back(makeToken(Tok::RBRACE, "}", line, col)); break;
            default:
                error(std::string("无法识别的字符 '") + c + "'", line);
                break;
        }
    }

    tokens.push_back(makeToken(Tok::END, "", line_, col_));
    return tokens;
}
