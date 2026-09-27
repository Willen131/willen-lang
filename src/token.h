#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Willen 语言的全部词法单元类型。
// 枚举名用英文（规避编译器对非 ASCII 标识符的差异），
// 对应的中文关键字在 KEYWORDS 表中映射。
enum class Tok {
    INT_LIT, FLOAT_LIT, STR_LIT, IDENT,             // 字面量与标识符

    KW_LET, KW_CONST,                               // 设 / 常量
    KW_IF, KW_ELSEIF, KW_ELSE,                      // 如果 / 否则如果 / 否则
    KW_WHILE, KW_FOR, KW_IN, KW_REPEAT, KW_TIMES,   // 当 / 遍历 / 在 / 重复 / 次
    KW_FUNC, KW_RETURN, KW_STRUCT,                  // 函数 / 返回 / 结构
    KW_BREAK, KW_CONTINUE,                          // 跳出 / 继续
    KW_TRUE, KW_FALSE, KW_NULL,                     // 真 / 假 / 空
    KW_AND, KW_OR, KW_NOT,                          // 且 / 或 / 非

    PLUS, MINUS, STAR, SLASH, PERCENT,              // + - * / %
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN,              // = += -=
    STAR_ASSIGN, SLASH_ASSIGN,                      // *= /=
    EQ, NEQ, LT, LE, GT, GE,                        // == != < <= > >=
    LPAREN, RPAREN, LBRACKET, RBRACKET,             // ( ) [ ]
    LBRACE, RBRACE,                                 // { }
    COMMA, SEMICOLON, DOT, DOTDOT, COLON,           // , ; . .. :

    END
};

// 模板字符串被切分后的片段：要么是字面文本，要么是一段插值表达式源码。
struct TemplatePart {
    bool isExpr;
    std::string text;
};

struct Token {
    Tok type = Tok::END;
    std::string text;        // 标识符名、数字字面量原文、字符串内容
    int line = 1;
    int col = 1;
    bool isTemplate = false;                 // 仅 STR_LIT 可能为 true
    std::vector<TemplatePart> parts;         // 仅当 isTemplate 为 true 时有效
};

// 中文关键字表 —— Willen 语言的全部关键字在此集中定义。
extern const std::unordered_map<std::string, Tok> KEYWORDS;

// 返回枚举的英文名，供调试输出与错误信息使用。
// 唯一实现位置：lexer.cpp
std::string tokName(Tok t);
