#include "parser.h"
#include "lexer.h"
#include <iostream>

// 构造一个二元表达式节点，行号取左操作数所在行
static ExprPtr makeBinary(Tok op, ExprPtr left, ExprPtr right) {
    auto e = std::make_unique<BinaryExpr>();
    e->line = left ? left->line : 0;
    e->op = op;
    e->left = std::move(left);
    e->right = std::move(right);
    return e;
}

Parser::Parser(std::vector<Token> tokens, std::string file)
    : tokens_(std::move(tokens)), file_(std::move(file)) {}

// ---------------- 词法单元游标 ----------------

const Token& Parser::current() const {
    if (pos_ >= tokens_.size()) return tokens_.back();
    return tokens_[pos_];
}

const Token& Parser::previous() const {
    return tokens_[pos_ > 0 ? pos_ - 1 : 0];
}

bool Parser::atEnd() const {
    return current().type == Tok::END;
}

const Token& Parser::advance() {
    if (!atEnd()) pos_++;
    return previous();
}

bool Parser::check(Tok type) const {
    return current().type == type;
}

bool Parser::match(Tok type) {
    if (!check(type)) return false;
    advance();
    return true;
}

void Parser::errorAt(const Token& tok, const std::string& message) {
    // 词法分析器把 END 的行号记成「最后一个有内容行的下一行」。
    // 直接采用会让「文件末尾处出错」报出一个源码里并不存在的位置，
    // 因此这里回退到最后一个有内容的词法单元所在行。
    int line = tok.line;
    if (tok.type == Tok::END) {
        for (size_t i = tokens_.size(); i > 0; i--) {
            if (tokens_[i - 1].type != Tok::END) {
                line = tokens_[i - 1].line;
                break;
            }
        }
    }
    std::cerr << "第 " << line << " 行：" << message << "\n";
    hadError_ = true;
}

Token Parser::expect(Tok type, const std::string& message) {
    if (check(type)) return advance();
    errorAt(current(), message);
    // 返回当前词法单元而不前进，让解析得以继续，一次报出更多错误
    return current();
}

// ---------------- 语句 ----------------

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> stmts;
    while (!atEnd()) {
        size_t before = pos_;
        StmtPtr s = declaration();
        if (s) stmts.push_back(std::move(s));
        // 本轮没消耗任何词法单元时强制前进一格，否则死循环
        if (pos_ == before) advance();
    }
    return stmts;
}

ExprPtr Parser::parseSingleExpression() {
    return expression();
}

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

    int line = current().line;
    ExprPtr e = expression();
    match(Tok::SEMICOLON);

    auto s = std::make_unique<ExprStmt>();
    s->line = line;
    s->expr = std::move(e);
    return s;
}

// ---------------- 表达式：按优先级从低到高 ----------------

ExprPtr Parser::expression() {
    return assignment();
}

ExprPtr Parser::assignment() {
    ExprPtr left = logicOr();

    if (check(Tok::ASSIGN) || check(Tok::PLUS_ASSIGN) ||
        check(Tok::MINUS_ASSIGN) || check(Tok::STAR_ASSIGN) ||
        check(Tok::SLASH_ASSIGN)) {
        Tok op = advance().type;
        ExprPtr value = assignment();   // 赋值右结合：甲 = 乙 = 1

        auto e = std::make_unique<AssignExpr>();
        e->line = left ? left->line : 0;
        e->target = std::move(left);
        e->op = op;
        e->value = std::move(value);
        return e;
    }
    return left;
}

ExprPtr Parser::logicOr() {
    ExprPtr left = logicAnd();
    while (match(Tok::KW_OR)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), logicAnd());
    }
    return left;
}

ExprPtr Parser::logicAnd() {
    ExprPtr left = equality();
    while (match(Tok::KW_AND)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), equality());
    }
    return left;
}

ExprPtr Parser::equality() {
    ExprPtr left = comparison();
    while (match(Tok::EQ) || match(Tok::NEQ)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), comparison());
    }
    return left;
}

ExprPtr Parser::comparison() {
    ExprPtr left = range();
    while (match(Tok::LT) || match(Tok::LE) || match(Tok::GT) || match(Tok::GE)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), range());
    }
    return left;
}

// 范围 0..9 只允许出现一次，不做链式结合
ExprPtr Parser::range() {
    ExprPtr left = term();
    if (match(Tok::DOTDOT)) {
        ExprPtr right = term();
        auto e = std::make_unique<RangeExpr>();
        e->line = left ? left->line : 0;
        e->from = std::move(left);
        e->to = std::move(right);
        return e;
    }
    return left;
}

ExprPtr Parser::term() {
    ExprPtr left = factor();
    while (match(Tok::PLUS) || match(Tok::MINUS)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), factor());
    }
    return left;
}

ExprPtr Parser::factor() {
    ExprPtr left = unary();
    while (match(Tok::STAR) || match(Tok::SLASH) || match(Tok::PERCENT)) {
        Tok op = previous().type;
        left = makeBinary(op, std::move(left), unary());
    }
    return left;
}

ExprPtr Parser::unary() {
    if (check(Tok::MINUS) || check(Tok::KW_NOT)) {
        Tok op = current().type;
        int line = current().line;
        advance();

        auto e = std::make_unique<UnaryExpr>();
        e->line = line;
        e->op = op;
        e->operand = unary();   // 允许 非 非 甲、- - 甲
        return e;
    }
    return postfix();
}

// 后缀：函数调用 ()、下标 []、字段与方法 .
ExprPtr Parser::postfix() {
    ExprPtr expr = primary();

    for (;;) {
        if (match(Tok::LPAREN)) {
            expr = finishCall(std::move(expr));
        } else if (match(Tok::LBRACKET)) {
            ExprPtr index = expression();
            expect(Tok::RBRACKET, "下标访问缺少右方括号");

            auto e = std::make_unique<IndexExpr>();
            e->line = expr ? expr->line : 0;
            e->object = std::move(expr);
            e->index = std::move(index);
            expr = std::move(e);
        } else if (match(Tok::DOT)) {
            Token name = expect(Tok::IDENT, "点号后需要字段名或方法名");
            if (match(Tok::LPAREN)) {
                auto e = std::make_unique<MethodExpr>();
                e->line = expr ? expr->line : 0;
                e->method = name.text;
                e->object = std::move(expr);
                if (!check(Tok::RPAREN)) {
                    do { e->args.push_back(expression()); } while (match(Tok::COMMA));
                }
                expect(Tok::RPAREN, "方法调用缺少右括号");
                expr = std::move(e);
            } else {
                auto e = std::make_unique<FieldExpr>();
                e->line = expr ? expr->line : 0;
                e->field = name.text;
                e->object = std::move(expr);
                expr = std::move(e);
            }
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::primary() {
    const Token t = current();   // 拷贝一份：advance 之后仍要用它

    if (match(Tok::INT_LIT)) {
        auto e = std::make_unique<IntLit>();
        e->line = t.line;
        try {
            e->value = std::stoll(t.text);
        } catch (...) {
            errorAt(t, "整数 " + t.text + " 超出了可表示范围");
            e->value = 0;
        }
        return e;
    }

    if (match(Tok::FLOAT_LIT)) {
        auto e = std::make_unique<FloatLit>();
        e->line = t.line;
        e->value = std::stod(t.text);
        return e;
    }

    if (match(Tok::STR_LIT))          return makeStringLiteral(t);
    if (match(Tok::KW_TRUE) || match(Tok::KW_FALSE)) {
        auto e = std::make_unique<BoolLit>();
        e->line = t.line;
        e->value = (t.type == Tok::KW_TRUE);
        return e;
    }
    if (match(Tok::KW_NULL)) {
        auto e = std::make_unique<NullLit>();
        e->line = t.line;
        return e;
    }

    if (match(Tok::LPAREN)) {
        ExprPtr e = expression();
        expect(Tok::RPAREN, "缺少右括号");
        return e;
    }

    if (match(Tok::LBRACKET))         return makeArrayLiteral(t);
    if (match(Tok::IDENT))            return makeIdentOrStruct(t);

    if (t.type == Tok::END) {
        errorAt(t, "表达式不完整，源码在此处结束");
    } else {
        errorAt(t, "这里需要一个表达式，却是「" + t.text + "」");
    }
    advance();   // 跳过这个无法理解的词法单元，避免死循环
    return nullptr;
}

// ---------------- 主表达式构造 ----------------

ExprPtr Parser::makeStringLiteral(const Token& t) {
    if (!t.isTemplate) {
        auto e = std::make_unique<StrLit>();
        e->line = t.line;
        e->value = t.text;
        return e;
    }

    auto e = std::make_unique<TmplLit>();
    e->line = t.line;
    for (const TemplatePart& part : t.parts) {
        if (part.isExpr) {
            // 插值片段是 Willen 表达式源码，递归走一遍词法 + 语法分析。
            // 这一步在解析期完成，运行时的模板求值就只剩拼接。
            e->pieces.push_back(parseFragment(part.text, t.line));
        } else {
            auto s = std::make_unique<StrLit>();
            s->line = t.line;
            s->value = part.text;
            e->pieces.push_back(std::move(s));
        }
    }
    return e;
}

ExprPtr Parser::parseFragment(const std::string& src, int line) {
    Lexer lex(src, file_);
    std::vector<Token> toks = lex.tokenize();
    if (lex.hadError()) { hadError_ = true; return nullptr; }

    Parser sub(std::move(toks), file_);
    ExprPtr e = sub.parseSingleExpression();
    if (sub.hadError()) hadError_ = true;
    if (e) e->line = line;   // 行号统一归到模板字符串所在行
    return e;
}

ExprPtr Parser::makeArrayLiteral(const Token& t) {
    auto e = std::make_unique<ArrayLit>();
    e->line = t.line;
    if (!check(Tok::RBRACKET)) {
        do { e->items.push_back(expression()); } while (match(Tok::COMMA));
    }
    expect(Tok::RBRACKET, "数组字面量缺少右方括号");
    return e;
}

ExprPtr Parser::makeIdentOrStruct(const Token& t) {
    // 标识符后紧跟 { 就是结构字面量：点{横: 1, 纵: 2}
    if (check(Tok::LBRACE)) {
        advance();

        auto e = std::make_unique<StructLit>();
        e->line = t.line;
        e->name = t.text;

        if (!check(Tok::RBRACE)) {
            do {
                Token field = expect(Tok::IDENT, "结构字面量的字段名必须是标识符");
                expect(Tok::COLON, "字段名后需要冒号，写法：点{横: 1, 纵: 2}");
                e->fields.push_back({field.text, expression()});
            } while (match(Tok::COMMA));
        }
        expect(Tok::RBRACE, "结构字面量缺少右花括号");
        return e;
    }

    auto v = std::make_unique<VarExpr>();
    v->line = t.line;
    v->name = t.text;
    return v;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
    auto e = std::make_unique<CallExpr>();
    e->line = callee ? callee->line : 0;
    e->callee = std::move(callee);

    if (!check(Tok::RPAREN)) {
        do { e->args.push_back(expression()); } while (match(Tok::COMMA));
    }
    expect(Tok::RPAREN, "函数调用缺少右括号");
    return e;
}
