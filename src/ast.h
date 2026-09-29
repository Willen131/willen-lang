#pragma once
#include "token.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Willen 语言的抽象语法树。
//
// 分成表达式（Expr）与语句（Stmt）两族，全部节点用 unique_ptr 持有子节点，
// 生命周期随根节点自动释放，无需手工管理。
//
// 每个节点带 line 字段，用于运行时错误定位——错误信息里的「第 N 行」就来自这里。

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    int line = 0;
    virtual ~Expr() = default;
};

// ---------- 表达式 ----------

struct IntLit   : Expr { int64_t value = 0; };
struct FloatLit : Expr { double value = 0; };
struct StrLit   : Expr { std::string value; };            // 非模板字符串

// 模板字符串：文本片段与插值片段交替。
// 文本片段是 StrLit，插值片段是各式子表达式。
// 例如 "你好，{名字}！" → [StrLit("你好，"), VarExpr(名字), StrLit("！")]
struct TmplLit  : Expr { std::vector<ExprPtr> pieces; };

struct BoolLit  : Expr { bool value = false; };
struct NullLit  : Expr {};
struct ArrayLit : Expr { std::vector<ExprPtr> items; };

// 结构字面量：点{横: 3, 纵: 4}
struct StructLit : Expr {
    std::string name;
    std::vector<std::pair<std::string, ExprPtr>> fields;
};

struct VarExpr   : Expr { std::string name; };
struct RangeExpr : Expr { ExprPtr from, to; };

struct BinaryExpr : Expr { Tok op; ExprPtr left, right; };
struct UnaryExpr  : Expr { Tok op; ExprPtr operand; };

struct IndexExpr  : Expr { ExprPtr object, index; };
struct FieldExpr  : Expr { ExprPtr object; std::string field; };
struct CallExpr   : Expr { ExprPtr callee; std::vector<ExprPtr> args; };
struct MethodExpr : Expr { ExprPtr object; std::string method;
                           std::vector<ExprPtr> args; };

// 赋值。target 必须是 VarExpr / FieldExpr / IndexExpr 之一，
// 该约束在求值阶段校验（解析阶段无法判定）。
struct AssignExpr : Expr { ExprPtr target; Tok op; ExprPtr value; };

// ---------- 语句 ----------

struct Stmt {
    int line = 0;
    virtual ~Stmt() = default;
};

struct ExprStmt     : Stmt { ExprPtr expr; };
struct VarDecl      : Stmt { std::string name; bool isConst = false; ExprPtr init; };
struct BlockStmt    : Stmt { std::vector<StmtPtr> stmts; };
struct IfStmt       : Stmt { ExprPtr cond; StmtPtr thenB, elseB; };
struct WhileStmt    : Stmt { ExprPtr cond; StmtPtr body; };
struct ForEachStmt  : Stmt { std::string var; ExprPtr iterable; StmtPtr body; };
struct RepeatStmt   : Stmt { ExprPtr count; StmtPtr body; };
struct BreakStmt    : Stmt {};
struct ContinueStmt : Stmt {};
struct ReturnStmt   : Stmt { ExprPtr value; };
struct FuncDecl     : Stmt { std::string name;
                             std::vector<std::string> params; StmtPtr body; };
struct StructDecl   : Stmt { std::string name; std::vector<std::string> fields; };

// ---------- 调试输出 ----------

// 把语法树打印成 S 表达式形式，供 willen ast 子命令与测试比对使用。
// 例：(VarDecl 甲 (+ 2 (* 3 4)))
std::string dumpExpr(const Expr* e);
std::string dumpStmt(const Stmt* s);
