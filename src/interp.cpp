#include "interp.h"
#include "builtins.h"
#include <iostream>
#include <string>

// ---------------- 运算辅助 ----------------

static int compareValues(const Value& l, const Value& r, int line) {
    if (l.isNumber() && r.isNumber()) {
        double a = l.toDouble(), b = r.toDouble();
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (auto a = std::get_if<std::string>(&l.v)) {
        if (auto b = std::get_if<std::string>(&r.v)) {
            int c = a->compare(*b);
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
    }
    throw RuntimeError{l.typeName() + " 与 " + r.typeName() + " 不能比较大小", line};
}

// 算术运算。两个整数相除仍是整数（7 / 2 得 3），
// 只要有一侧是小数，结果就提升为小数——这条规则要在语言规范里写明。
static Value arith(Tok op, const Value& l, const Value& r, int line) {
    if (!l.isNumber() || !r.isNumber()) {
        throw RuntimeError{l.typeName() + " 与 " + r.typeName() +
                           " 不能做算术运算", line};
    }

    bool bothInt = l.isInt() && r.isInt();

    if (bothInt) {
        int64_t a = l.toInt(), b = r.toInt();
        switch (op) {
            case Tok::PLUS:  return Value::makeInt(a + b);
            case Tok::MINUS: return Value::makeInt(a - b);
            case Tok::STAR:  return Value::makeInt(a * b);
            case Tok::SLASH:
                if (b == 0) throw RuntimeError{"除数不能为零", line};
                return Value::makeInt(a / b);
            case Tok::PERCENT:
                if (b == 0) throw RuntimeError{"除数不能为零", line};
                return Value::makeInt(a % b);
            default: break;
        }
    }

    double a = l.toDouble(), b = r.toDouble();
    switch (op) {
        case Tok::PLUS:  return Value::makeFloat(a + b);
        case Tok::MINUS: return Value::makeFloat(a - b);
        case Tok::STAR:  return Value::makeFloat(a * b);
        case Tok::SLASH:
            if (b == 0) throw RuntimeError{"除数不能为零", line};
            return Value::makeFloat(a / b);
        case Tok::PERCENT:
            throw RuntimeError{"取模运算的两个操作数都必须是整数", line};
        default: break;
    }
    throw RuntimeError{"不支持的运算符", line};
}

static Value applyBinary(Tok op, const Value& l, const Value& r, int line) {
    switch (op) {
        case Tok::EQ:  return Value::makeBool(valueEquals(l, r));
        case Tok::NEQ: return Value::makeBool(!valueEquals(l, r));
        case Tok::LT:  return Value::makeBool(compareValues(l, r, line) < 0);
        case Tok::LE:  return Value::makeBool(compareValues(l, r, line) <= 0);
        case Tok::GT:  return Value::makeBool(compareValues(l, r, line) > 0);
        case Tok::GE:  return Value::makeBool(compareValues(l, r, line) >= 0);
        case Tok::PLUS:
            // 任一侧是字符串就做拼接：14 + "个" 得 "14个"
            if (std::holds_alternative<std::string>(l.v) ||
                std::holds_alternative<std::string>(r.v)) {
                return Value::makeStr(l.toString() + r.toString());
            }
            return arith(op, l, r, line);
        case Tok::MINUS:
        case Tok::STAR:
        case Tok::SLASH:
        case Tok::PERCENT:
            return arith(op, l, r, line);
        default:
            throw RuntimeError{"不支持的运算符", line};
    }
}

// ---------------- 求值器 ----------------

Interpreter::Interpreter() {}

bool Interpreter::run(std::vector<StmtPtr> program) {
    programs_.push_back(std::move(program));
    const std::vector<StmtPtr>& prog = programs_.back();

    try {
        for (const StmtPtr& s : prog) exec(s.get());
        return true;
    } catch (const RuntimeError& err) {
        std::cerr << "第 " << err.line << " 行：" << err.message << "\n";
        return false;
    }
}

bool Interpreter::runRepl(std::vector<StmtPtr> stmts, Value& out, bool& hasOut) {
    programs_.push_back(std::move(stmts));
    const std::vector<StmtPtr>& prog = programs_.back();

    hasOut = false;
    try {
        for (size_t i = 0; i < prog.size(); i++) {
            bool isLast = (i + 1 == prog.size());
            if (isLast) {
                if (auto p = dynamic_cast<const ExprStmt*>(prog[i].get())) {
                    if (p->expr) {
                        out = eval(p->expr.get());
                        hasOut = true;
                    }
                    continue;
                }
            }
            exec(prog[i].get());
        }
        return true;
    } catch (const RuntimeError& err) {
        std::cerr << "第 " << err.line << " 行：" << err.message << "\n";
        return false;
    }
}

Value Interpreter::eval(const Expr* e) {
    if (!e) return Value{};

    // 用 kind 做一次 switch 分发。
    // 原先这里是一长串 dynamic_cast，每求值一个节点都要逐个尝试十几个
    // 类型——dynamic_cast 要查运行时类型信息，比一次跳转慢得多。
    // 改成 switch 后只剩一次跳转加一次 static_cast。
    switch (e->kind) {

        case ExprKind::Int:
            return Value::makeInt(static_cast<const IntLit*>(e)->value);
        case ExprKind::Float:
            return Value::makeFloat(static_cast<const FloatLit*>(e)->value);
        case ExprKind::Str:
            return Value::makeStr(static_cast<const StrLit*>(e)->value);
        case ExprKind::Bool:
            return Value::makeBool(static_cast<const BoolLit*>(e)->value);
        case ExprKind::Null:
            return Value{};

        // 模板：片段在解析期就已降解好，这里只需把各片段拼起来
        case ExprKind::Tmpl: {
            auto p = static_cast<const TmplLit*>(e);
            std::string out;
            for (const ExprPtr& piece : p->pieces) out += eval(piece.get()).toString();
            return Value::makeStr(out);
        }

        case ExprKind::Array: {
            auto p = static_cast<const ArrayLit*>(e);
            Value arr = Value::makeArray();
            std::vector<Value>& items = *std::get<ArrayPtr>(arr.v);
            for (const ExprPtr& item : p->items) items.push_back(eval(item.get()));
            return arr;
        }

        case ExprKind::Struct: {
            auto p = static_cast<const StructLit*>(e);
            auto it = structDefs_.find(p->name);
            if (it == structDefs_.end()) {
                throw RuntimeError{"未定义的结构体「" + p->name + "」", e->line};
            }
            std::shared_ptr<StructDef> def = it->second;
            Value inst = Value::makeStruct(def);
            std::vector<Value>& fields = std::get<StructPtr>(inst.v)->fields;
            for (const auto& f : p->fields) {
                int idx = def->indexOf(f.first);
                if (idx < 0) {
                    throw RuntimeError{"结构体 " + p->name + " 没有字段 " + f.first,
                                       e->line};
                }
                fields[idx] = eval(f.second.get());
            }
            return inst;
        }

        case ExprKind::Var: {
            auto p = static_cast<const VarExpr*>(e);
            // 局部变量：解析期已绑定槽位，这里只剩一次 vector 寻址
            if (p->slot >= 0) {
                size_t i = static_cast<size_t>(p->slot);
                if (i < slots_.size()) return slots_[i];
                throw RuntimeError{"内部错误：槽位越界", e->line};
            }
            // 全局变量：按名字查表
            auto it = globals_.vars.find(p->name);
            if (it == globals_.vars.end()) {
                throw RuntimeError{"未定义的变量「" + p->name + "」", e->line};
            }
            return it->second;
        }

        case ExprKind::Range: {
            auto p = static_cast<const RangeExpr*>(e);
            Value from = eval(p->from.get());
            Value to = eval(p->to.get());
            if (!from.isInt() || !to.isInt()) {
                throw RuntimeError{"范围的两端都必须是整数", e->line};
            }
            Value arr = Value::makeArray();
            std::vector<Value>& items = *std::get<ArrayPtr>(arr.v);
            for (int64_t i = from.toInt(); i <= to.toInt(); i++) {
                items.push_back(Value::makeInt(i));
            }
            return arr;
        }

        case ExprKind::Binary: {
            auto p = static_cast<const BinaryExpr*>(e);
            // 且 / 或 必须短路：右侧表达式可能依赖左侧的判断结果
            if (p->op == Tok::KW_AND) {
                if (!eval(p->left.get()).truthy()) return Value::makeBool(false);
                return Value::makeBool(eval(p->right.get()).truthy());
            }
            if (p->op == Tok::KW_OR) {
                if (eval(p->left.get()).truthy()) return Value::makeBool(true);
                return Value::makeBool(eval(p->right.get()).truthy());
            }
            return applyBinary(p->op, eval(p->left.get()), eval(p->right.get()),
                               e->line);
        }

        case ExprKind::Unary: {
            auto p = static_cast<const UnaryExpr*>(e);
            Value v = eval(p->operand.get());
            if (p->op == Tok::KW_NOT) return Value::makeBool(!v.truthy());
            if (p->op == Tok::MINUS) {
                if (!v.isNumber()) {
                    throw RuntimeError{"不能对 " + v.typeName() + " 取负", e->line};
                }
                return v.isInt() ? Value::makeInt(-v.toInt())
                                 : Value::makeFloat(-v.toDouble());
            }
            throw RuntimeError{"不支持的一元运算符", e->line};
        }

        case ExprKind::Index: {
            auto p = static_cast<const IndexExpr*>(e);
            Value obj = eval(p->object.get());
            Value idx = eval(p->index.get());
            if (!idx.isInt()) throw RuntimeError{"下标必须是整数", e->line};
            int64_t i = idx.toInt();

            if (auto arr = std::get_if<ArrayPtr>(&obj.v)) {
                int64_t n = static_cast<int64_t>((*arr)->size());
                if (i < 0 || i >= n) {
                    throw RuntimeError{"数组下标越界：" + std::to_string(i) +
                                       "，长度为 " + std::to_string(n), e->line};
                }
                return (**arr)[i];
            }
            if (auto s = std::get_if<std::string>(&obj.v)) {
                int64_t n = static_cast<int64_t>(s->size());
                if (i < 0 || i >= n) {
                    throw RuntimeError{"字符串下标越界：" + std::to_string(i) +
                                       "，长度为 " + std::to_string(n), e->line};
                }
                return Value::makeStr(std::string(1, (*s)[i]));
            }
            throw RuntimeError{obj.typeName() + " 不能用下标访问", e->line};
        }

        case ExprKind::Field: {
            auto p = static_cast<const FieldExpr*>(e);
            Value obj = eval(p->object.get());
            if (auto inst = std::get_if<StructPtr>(&obj.v)) {
                int idx = (*inst)->def->indexOf(p->field);
                if (idx < 0) {
                    throw RuntimeError{"结构体 " + (*inst)->def->name +
                                       " 没有字段 " + p->field, e->line};
                }
                return (*inst)->fields[idx];
            }
            if (p->field == "长度") {
                if (auto arr = std::get_if<ArrayPtr>(&obj.v)) {
                    return Value::makeInt(static_cast<int64_t>((*arr)->size()));
                }
                if (auto s = std::get_if<std::string>(&obj.v)) {
                    return Value::makeInt(static_cast<int64_t>(s->size()));
                }
            }
            throw RuntimeError{obj.typeName() + " 没有字段 " + p->field, e->line};
        }

        case ExprKind::Call: {
            auto p = static_cast<const CallExpr*>(e);
            // 被调用者必须是变量名——函数表按名字索引
            if (p->callee->kind != ExprKind::Var) {
                throw RuntimeError{"只能调用具名函数", e->line};
            }
            const std::string& fname =
                static_cast<const VarExpr*>(p->callee.get())->name;

            std::vector<Value> args;
            for (const ExprPtr& a : p->args) args.push_back(eval(a.get()));

            // 内置函数优先，其次是用户定义的函数
            Value result;
            if (callBuiltin(fname, args, result, e->line)) return result;

            auto it = funcs_.find(fname);
            if (it == funcs_.end()) {
                throw RuntimeError{"未定义的函数「" + fname + "」", e->line};
            }
            return callFunction(it->second, args, e->line);
        }

        case ExprKind::Method: {
            auto p = static_cast<const MethodExpr*>(e);
            Value obj = eval(p->object.get());
            std::vector<Value> args;
            for (const ExprPtr& a : p->args) args.push_back(eval(a.get()));

            Value result;
            if (callMethod(obj, p->method, args, result, e->line)) return result;
            throw RuntimeError{obj.typeName() + " 没有方法 " + p->method, e->line};
        }

        case ExprKind::Assign: {
            auto p = static_cast<const AssignExpr*>(e);
            Value val = eval(p->value.get());
            if (p->op != Tok::ASSIGN) {
                Tok binOp = Tok::PLUS;
                switch (p->op) {
                    case Tok::PLUS_ASSIGN:  binOp = Tok::PLUS;  break;
                    case Tok::MINUS_ASSIGN: binOp = Tok::MINUS; break;
                    case Tok::STAR_ASSIGN:  binOp = Tok::STAR;  break;
                    case Tok::SLASH_ASSIGN: binOp = Tok::SLASH; break;
                    default: break;
                }
                val = applyBinary(binOp, eval(p->target.get()), val, e->line);
            }
            assignTo(p->target.get(), val, e->line);
            return val;
        }
    }

    throw RuntimeError{"无法求值的表达式", e->line};
}

