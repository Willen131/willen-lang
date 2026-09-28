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

    // 解析字符串字面量。含插值的返回 isTemplate = true 且填好 parts，
    // 否则返回普通 STR_LIT。
    Token stringLiteral();

    // 校验源码是否为合法 UTF-8。返回首个非法字节所在行号，全合法返回 0。
    // 用于识别被存成 GBK 的源文件——否则中文关键字会静默失效。
    int validateUtf8() const;

    void error(const std::string& message, int line);
};
