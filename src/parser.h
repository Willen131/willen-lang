#pragma once
#include "ast.h"
#include "token.h"
#include <string>
#include <utility>
#include <vector>

// 递归下降语法分析器。
//
// 表达式部分按优先级分层，每层一个函数，从低到高依次调用：
//   assignment → logicOr → logicAnd → equality → comparison
//   → range → term → factor → unary → postfix → primary
// 优先级直接体现在调用关系里，读代码即知语法。
class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file);

    // 解析整个文件为语句列表
    std::vector<StmtPtr> parse();

    // 解析单个表达式。供模板字符串的插值片段递归调用。
    ExprPtr parseSingleExpression();

    bool hadError() const { return hadError_; }

private:
    std::vector<Token> tokens_;
    std::string file_;
    size_t pos_ = 0;
    bool hadError_ = false;

    // ---- 词法单元游标 ----
    const Token& current() const;
    const Token& previous() const;
    bool atEnd() const;
    const Token& advance();
    bool check(Tok type) const;
    bool match(Tok type);
    Token expect(Tok type, const std::string& message);
    void errorAt(const Token& tok, const std::string& message);

    // ---- 语句 ----
    StmtPtr declaration();
    StmtPtr structDecl();
    StmtPtr funcDecl();
    StmtPtr ifStatement();
    StmtPtr ifTail(int line, ExprPtr cond);   // 「否则如果」链共用的尾部
    StmtPtr whileStatement();
    StmtPtr forEachStatement();
    StmtPtr repeatStatement();
    StmtPtr returnStatement();
    StmtPtr block();

    // 解析条件，括号强制。缺失括号时报错并跳过被误写的条件部分——
    // 否则「甲 {」会被当成结构字面量，引发一串级联错误。
    ExprPtr condition(const std::string& keyword, int line);

    // ---- 表达式（按优先级从低到高）----
    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr logicOr();
    ExprPtr logicAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr range();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();

    // ---- 主表达式构造 ----
    ExprPtr makeStringLiteral(const Token& t);   // 普通字符串 / 模板字符串
    ExprPtr makeArrayLiteral(const Token& t);
    ExprPtr makeIdentOrStruct(const Token& t);   // 变量引用 / 结构字面量
    ExprPtr parseFragment(const std::string& src, int line);  // 插值片段
    ExprPtr finishCall(ExprPtr callee);          // 函数调用实参表
};
