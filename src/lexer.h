#pragma once
#include "token.h"
#include <string>
#include <vector>

// 词法分析器：把 Willen 源码文本转换为词法单元序列。
class Lexer {
public:
    Lexer(std::string src, std::string file);

    // 扫描全部源码。遇到词法错误时记录并继续，最终 hadError() 为 true。
    std::vector<Token> tokenize();

    bool hadError() const { return hadError_; }

private:
    std::string src_, file_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    bool hadError_ = false;

    bool atEnd() const { return pos_ >= src_.size(); }
    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void skipWhitespaceAndComments();
    Token makeToken(Tok type, const std::string& text, int line, int col) const;
    Token identOrKeyword();
    Token number();
    void error(const std::string& message, int line);
};
