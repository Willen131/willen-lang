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

// 表达式节点的种类标记。
//
// 为什么需要它：求值器原先靠一长串 dynamic_cast 判断节点类型，
// 每求值一个节点都要逐个尝试十几个类型。dynamic_cast 涉及运行时
// 类型信息查询，比一次 switch 跳转慢得多。给每个节点带上种类标记后，
// 求值器改用 switch 分发，只用一次 static_cast 就能取到具体类型。
enum class ExprKind {
    Int, Float, Str, Tmpl, Bool, Null, Array, Struct,
    Var, Range, Binary, Unary, Index, Field, Call, Method, Assign
};

struct Expr {
    int line = 0;
    ExprKind kind;
    virtual ~Expr() = default;
    explicit Expr(ExprKind k) : kind(k) {}
};

// ---------- 表达式 ----------

struct IntLit   : Expr { int64_t value = 0; IntLit() : Expr(ExprKind::Int) {} };
struct FloatLit : Expr { double value = 0;  FloatLit() : Expr(ExprKind::Float) {} };
struct StrLit   : Expr { std::string value; StrLit() : Expr(ExprKind::Str) {} };  // 非模板字符串

// 模板字符串：文本片段与插值片段交替。
// 文本片段是 StrLit，插值片段是各式子表达式。
// 例如 "你好，{名字}！" → [StrLit("你好，"), VarExpr(名字), StrLit("！")]
struct TmplLit  : Expr {
    std::vector<ExprPtr> pieces;
    TmplLit() : Expr(ExprKind::Tmpl) {}
};

struct BoolLit  : Expr { bool value = false; BoolLit() : Expr(ExprKind::Bool) {} };
struct NullLit  : Expr { NullLit() : Expr(ExprKind::Null) {} };
struct ArrayLit : Expr {
    std::vector<ExprPtr> items;
    ArrayLit() : Expr(ExprKind::Array) {}
};

// 结构字面量：点{横: 3, 纵: 4}
struct StructLit : Expr {
    std::string name;
    std::vector<std::pair<std::string, ExprPtr>> fields;
    StructLit() : Expr(ExprKind::Struct) {}
};

struct VarExpr   : Expr {
    std::string name;
    // 槽位下标，由 Resolver 在解析期填充。
    // >= 0 表示这是函数内的局部变量，运行期直接按槽位下标寻址；
    // -1 表示全局变量或未解析（按名字查全局表）。
    int slot = -1;
    VarExpr() : Expr(ExprKind::Var) {}
};
struct RangeExpr : Expr {
    ExprPtr from, to;
    RangeExpr() : Expr(ExprKind::Range) {}
};

struct BinaryExpr : Expr {
    Tok op; ExprPtr left, right;
    BinaryExpr() : Expr(ExprKind::Binary) {}
};
struct UnaryExpr  : Expr {
    Tok op; ExprPtr operand;
    UnaryExpr() : Expr(ExprKind::Unary) {}
};

struct IndexExpr  : Expr {
    ExprPtr object, index;
    IndexExpr() : Expr(ExprKind::Index) {}
};
struct FieldExpr  : Expr {
    ExprPtr object; std::string field;
    FieldExpr() : Expr(ExprKind::Field) {}
};
struct CallExpr   : Expr {
    ExprPtr callee; std::vector<ExprPtr> args;
    CallExpr() : Expr(ExprKind::Call) {}
};
struct MethodExpr : Expr {
    ExprPtr object; std::string method; std::vector<ExprPtr> args;
    MethodExpr() : Expr(ExprKind::Method) {}
};

// 赋值。target 必须是 VarExpr / FieldExpr / IndexExpr 之一，
// 该约束在求值阶段校验（解析阶段无法判定）。
struct AssignExpr : Expr {
    ExprPtr target; Tok op; ExprPtr value;
    AssignExpr() : Expr(ExprKind::Assign) {}
};

// ---------- 语句 ----------

struct Stmt {
    int line = 0;
    virtual ~Stmt() = default;
};

struct ExprStmt     : Stmt { ExprPtr expr; };
struct VarDecl      : Stmt {
    std::string name;
    bool isConst = false;
    ExprPtr init;
    int slot = -1;      // 由 Resolver 填充；-1 表示全局变量
};
struct BlockStmt    : Stmt { std::vector<StmtPtr> stmts; };
struct IfStmt       : Stmt { ExprPtr cond; StmtPtr thenB, elseB; };
struct WhileStmt    : Stmt { ExprPtr cond; StmtPtr body; };
struct ForEachStmt  : Stmt {
    std::string var;
    ExprPtr iterable;
    StmtPtr body;
    int varSlot = -1;   // 循环变量所在槽位，由 Resolver 填充
};
struct RepeatStmt   : Stmt { ExprPtr count; StmtPtr body; };
struct BreakStmt    : Stmt {};
struct ContinueStmt : Stmt {};
struct ReturnStmt   : Stmt { ExprPtr value; };
struct FuncDecl     : Stmt { std::string name;
                             std::vector<std::string> params; StmtPtr body;
                             // 该函数需要的槽位总数（参数 + 全部局部变量），
                             // 由 Resolver 填充。调用时按这个大小开帧。
                             int slotCount = 0; };
struct StructDecl   : Stmt { std::string name; std::vector<std::string> fields; };

// ---------- 调试输出 ----------

// 把语法树打印成 S 表达式形式，供 willen ast 子命令与测试比对使用。
// 例：(VarDecl 甲 (+ 2 (* 3 4)))
std::string dumpExpr(const Expr* e);
std::string dumpStmt(const Stmt* s);
