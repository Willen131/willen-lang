#include "parser.h"
#include "lexer.h"
#include <iostream>

StmtPtr Parser::declaration() {
    if (check(Tok::KW_LET) || check(Tok::KW_CONST)) {
        bool isConst = (current().type == Tok::KW_CONST);
        int line = current().line;
        advance();

        Token nameTok = expect(Tok::IDENT, "变量名必须是标识符");
        expect(Tok::ASSIGN, "变量声明需要初始值，写法：设 甲 = 1");
        ExprPtr init = expression();
        match(Tok::SEMICOLON);

        auto d = std::make_unique<VarDecl>();
        d->line = line;
        d->name = nameTok.text;
        d->isConst = isConst;
        d->init = std::move(init);
        return d;
    }

    if (check(Tok::KW_STRUCT))  return structDecl();
    if (check(Tok::KW_FUNC))    return funcDecl();
    if (check(Tok::KW_IF))      return ifStatement();
    if (check(Tok::KW_WHILE))   return whileStatement();
    if (check(Tok::KW_FOR))     return forEachStatement();
    if (check(Tok::KW_REPEAT))  return repeatStatement();
    if (check(Tok::KW_RETURN))  return returnStatement();
    if (check(Tok::LBRACE))     return block();

    if (check(Tok::KW_BREAK) || check(Tok::KW_CONTINUE)) {
        bool isBreak = (current().type == Tok::KW_BREAK);
        int line = current().line;
        advance();
        match(Tok::SEMICOLON);

        if (isBreak) {
            auto s = std::make_unique<BreakStmt>();
            s->line = line;
            return s;
        }
        auto s = std::make_unique<ContinueStmt>();
        s->line = line;
        return s;
    }

    int line = current().line;
    ExprPtr e = expression();
    match(Tok::SEMICOLON);

    auto s = std::make_unique<ExprStmt>();
    s->line = line;
    s->expr = std::move(e);
    return s;
}

// 解析条件表达式，括号是强制的。
//
// 为什么不强制不行：「如果 甲 { ... }」里的「甲 {」与结构字面量
// 「点{横: 1}」的写法完全一致，解析器无从区分——强制括号把这个歧义
// 在语法层面直接消除，递归下降不需要任何回溯。
//
// 报错后还要把被误写的条件部分跳掉，一路跳到块开始处。否则那一段会被
// 当成结构字面量继续解析，报出一连串与真正原因无关的级联错误。
ExprPtr Parser::condition(const std::string& keyword, int line) {
    if (check(Tok::LPAREN)) {
        advance();
        ExprPtr e = expression();
        expect(Tok::RPAREN, "条件缺少右括号");
        return e;
    }

    errorAt(current(), keyword + " 的条件必须用括号包围，正确写法：" +
                       keyword + " (条件) { ... }");
    while (!check(Tok::LBRACE) && !check(Tok::RBRACE) && !atEnd()) advance();

    // 放一个占位条件，让语句的其余部分（块）照常解析
    auto placeholder = std::make_unique<BoolLit>();
    placeholder->line = line;
    placeholder->value = false;
    return placeholder;
}

// 语句块 { ... }。块内的语句共享同一层作用域（由求值器建立）。
StmtPtr Parser::block() {
    if (!check(Tok::LBRACE)) {
        errorAt(current(), "这里需要一个语句块，用 { } 包围");
        return nullptr;
    }
    int line = current().line;
    advance();

    auto b = std::make_unique<BlockStmt>();
    b->line = line;
    while (!check(Tok::RBRACE) && !atEnd()) {
        size_t before = pos_;
        StmtPtr s = declaration();
        if (s) b->stmts.push_back(std::move(s));
        if (pos_ == before) advance();   // 防死循环
    }
    expect(Tok::RBRACE, "语句块缺少右花括号");
    return b;
}

StmtPtr Parser::structDecl() {
    int line = current().line;
    advance();   // 结构

    Token name = expect(Tok::IDENT, "结构名必须是标识符");
    expect(Tok::LBRACE, "结构的写法是：结构 点 { 横, 纵 }");

    std::vector<std::string> fields;
    if (!check(Tok::RBRACE)) {
        do {
            Token f = expect(Tok::IDENT, "字段名必须是标识符");
            fields.push_back(f.text);
        } while (match(Tok::COMMA));
    }
    expect(Tok::RBRACE, "结构定义缺少右花括号");

    auto s = std::make_unique<StructDecl>();
    s->line = line;
    s->name = name.text;
    s->fields = std::move(fields);
    return s;
}

StmtPtr Parser::funcDecl() {
    int line = current().line;
    advance();   // 函数

    Token name = expect(Tok::IDENT, "函数名必须是标识符");
    expect(Tok::LPAREN, "函数名后需要参数表，写法：函数 名(甲, 乙) { ... }");

    std::vector<std::string> params;
    if (!check(Tok::RPAREN)) {
        do {
            Token p = expect(Tok::IDENT, "参数名必须是标识符");
            params.push_back(p.text);
        } while (match(Tok::COMMA));
    }
    expect(Tok::RPAREN, "函数参数表缺少右括号");
    StmtPtr body = block();

    auto s = std::make_unique<FuncDecl>();
    s->line = line;
    s->name = name.text;
    s->params = std::move(params);
    s->body = std::move(body);
    return s;
}

StmtPtr Parser::ifStatement() {
    int line = current().line;
    advance();   // 如果
    ExprPtr cond = condition("如果", line);
    return ifTail(line, std::move(cond));
}

// 「否则如果」不引入新节点，而是递归成一个嵌套的 IfStmt 放进 elseB，
// 这样求值器只需要认识一种条件语句。
StmtPtr Parser::ifTail(int line, ExprPtr cond) {
    StmtPtr thenB = block();

    StmtPtr elseB;
    if (match(Tok::KW_ELSEIF)) {
        int elseIfLine = previous().line;
        ExprPtr nextCond = condition("否则如果", elseIfLine);
        elseB = ifTail(elseIfLine, std::move(nextCond));
    } else if (match(Tok::KW_ELSE)) {
        elseB = block();
    }

    auto s = std::make_unique<IfStmt>();
    s->line = line;
    s->cond = std::move(cond);
    s->thenB = std::move(thenB);
    s->elseB = std::move(elseB);
    return s;
}

StmtPtr Parser::whileStatement() {
    int line = current().line;
    advance();   // 当
    ExprPtr cond = condition("当", line);
    StmtPtr body = block();

    auto s = std::make_unique<WhileStmt>();
    s->line = line;
    s->cond = std::move(cond);
    s->body = std::move(body);
    return s;
}

StmtPtr Parser::forEachStatement() {
    int line = current().line;
    advance();   // 遍历

    bool paren = match(Tok::LPAREN);   // 括号可写可不写
    Token var = expect(Tok::IDENT,
                       "遍历需要一个循环变量，写法：遍历 (元 在 集合) { ... }");
    expect(Tok::KW_IN, "遍历的写法是：遍历 (变量 在 集合) { ... }");
    ExprPtr iterable = expression();
    if (paren) expect(Tok::RPAREN, "遍历缺少右括号");
    StmtPtr body = block();

    auto s = std::make_unique<ForEachStmt>();
    s->line = line;
    s->var = var.text;
    s->iterable = std::move(iterable);
    s->body = std::move(body);
    return s;
}

StmtPtr Parser::repeatStatement() {
    int line = current().line;
    advance();   // 重复
    ExprPtr count = expression();
    expect(Tok::KW_TIMES, "重复的写法是：重复 N 次 { ... }");
    StmtPtr body = block();

    auto s = std::make_unique<RepeatStmt>();
    s->line = line;
    s->count = std::move(count);
    s->body = std::move(body);
    return s;
}

StmtPtr Parser::returnStatement() {
    int line = current().line;
    advance();   // 返回

    ExprPtr value;
    // 「返回」后面没有值也是合法的：返回 与 返回 甲 都允许
    if (!check(Tok::RBRACE) && !check(Tok::SEMICOLON) && !atEnd()) {
        value = expression();
    }
    match(Tok::SEMICOLON);

    auto s = std::make_unique<ReturnStmt>();
    s->line = line;
    s->value = std::move(value);
    return s;
}

// ---------------- 表达式：按优先级从低到高 ----------------

