#include "ast.h"
#include <sstream>

// 把语法树打印成 S 表达式，供 willen ast 子命令与测试比对。
// 输出力求与源码一一对应：运算符沿用源码写法，字面量原样呈现，结构用括号表达。

static std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += c;      break;
        }
    }
    out += "\"";
    return out;
}

static std::string opText(Tok t) {
    switch (t) {
        case Tok::PLUS:         return "+";
        case Tok::MINUS:        return "-";
        case Tok::STAR:         return "*";
        case Tok::SLASH:        return "/";
        case Tok::PERCENT:      return "%";
        case Tok::EQ:           return "==";
        case Tok::NEQ:          return "!=";
        case Tok::LT:           return "<";
        case Tok::LE:           return "<=";
        case Tok::GT:           return ">";
        case Tok::GE:           return ">=";
        case Tok::KW_AND:       return "且";
        case Tok::KW_OR:        return "或";
        case Tok::KW_NOT:       return "非";
        case Tok::ASSIGN:       return "=";
        case Tok::PLUS_ASSIGN:  return "+=";
        case Tok::MINUS_ASSIGN: return "-=";
        case Tok::STAR_ASSIGN:  return "*=";
        case Tok::SLASH_ASSIGN: return "/=";
        default:                return tokName(t);
    }
}

static std::string joinArgs(const std::vector<ExprPtr>& args) {
    std::string out;
    for (const ExprPtr& a : args) out += " " + dumpExpr(a.get());
    return out;
}

std::string dumpExpr(const Expr* e) {
    if (!e) return "?";

    if (auto p = dynamic_cast<const IntLit*>(e))   return std::to_string(p->value);
    if (auto p = dynamic_cast<const FloatLit*>(e)) {
        std::ostringstream os;
        os << p->value;
        return os.str();
    }
    if (auto p = dynamic_cast<const StrLit*>(e))   return quote(p->value);
    if (auto p = dynamic_cast<const BoolLit*>(e))  return p->value ? "真" : "假";
    if (auto p = dynamic_cast<const NullLit*>(e))  return "空";

    if (auto p = dynamic_cast<const TmplLit*>(e)) {
        std::string out = "(Tmpl";
        for (const ExprPtr& piece : p->pieces) out += " " + dumpExpr(piece.get());
        return out + ")";
    }
    if (auto p = dynamic_cast<const ArrayLit*>(e)) {
        std::string out = "[";
        for (size_t i = 0; i < p->items.size(); i++) {
            if (i) out += " ";
            out += dumpExpr(p->items[i].get());
        }
        return out + "]";
    }
    if (auto p = dynamic_cast<const StructLit*>(e)) {
        std::string out = "(StructLit " + p->name;
        for (const auto& f : p->fields) {
            out += " (" + f.first + " " + dumpExpr(f.second.get()) + ")";
        }
        return out + ")";
    }

    if (auto p = dynamic_cast<const VarExpr*>(e))   return p->name;
    if (auto p = dynamic_cast<const RangeExpr*>(e)) {
        return "(.. " + dumpExpr(p->from.get()) + " " + dumpExpr(p->to.get()) + ")";
    }
    if (auto p = dynamic_cast<const BinaryExpr*>(e)) {
        return "(" + opText(p->op) + " " + dumpExpr(p->left.get()) + " " +
               dumpExpr(p->right.get()) + ")";
    }
    if (auto p = dynamic_cast<const UnaryExpr*>(e)) {
        return "(" + opText(p->op) + " " + dumpExpr(p->operand.get()) + ")";
    }
    if (auto p = dynamic_cast<const IndexExpr*>(e)) {
        return "(Index " + dumpExpr(p->object.get()) + " " +
               dumpExpr(p->index.get()) + ")";
    }
    if (auto p = dynamic_cast<const FieldExpr*>(e)) {
        return "(Field " + dumpExpr(p->object.get()) + " " + p->field + ")";
    }
    if (auto p = dynamic_cast<const CallExpr*>(e)) {
        return "(Call " + dumpExpr(p->callee.get()) + joinArgs(p->args) + ")";
    }
    if (auto p = dynamic_cast<const MethodExpr*>(e)) {
        return "(Method " + dumpExpr(p->object.get()) + " " + p->method +
               joinArgs(p->args) + ")";
    }
    if (auto p = dynamic_cast<const AssignExpr*>(e)) {
        return "(" + opText(p->op) + " " + dumpExpr(p->target.get()) + " " +
               dumpExpr(p->value.get()) + ")";
    }
    return "?";
}

std::string dumpStmt(const Stmt* s) {
    if (!s) return "?";

    if (auto p = dynamic_cast<const VarDecl*>(s)) {
        std::string head = p->isConst ? "(ConstDecl " : "(VarDecl ";
        return head + p->name + " " + dumpExpr(p->init.get()) + ")";
    }
    if (auto p = dynamic_cast<const ExprStmt*>(s)) {
        return "(Expr " + dumpExpr(p->expr.get()) + ")";
    }
    if (auto p = dynamic_cast<const BlockStmt*>(s)) {
        std::string out = "(Block";
        for (const StmtPtr& st : p->stmts) out += " " + dumpStmt(st.get());
        return out + ")";
    }
    if (auto p = dynamic_cast<const IfStmt*>(s)) {
        std::string out = "(If " + dumpExpr(p->cond.get()) + " " +
                          dumpStmt(p->thenB.get());
        if (p->elseB) out += " " + dumpStmt(p->elseB.get());
        return out + ")";
    }
    if (auto p = dynamic_cast<const WhileStmt*>(s)) {
        return "(While " + dumpExpr(p->cond.get()) + " " + dumpStmt(p->body.get()) + ")";
    }
    if (auto p = dynamic_cast<const ForEachStmt*>(s)) {
        return "(ForEach " + p->var + " " + dumpExpr(p->iterable.get()) + " " +
               dumpStmt(p->body.get()) + ")";
    }
    if (auto p = dynamic_cast<const RepeatStmt*>(s)) {
        return "(Repeat " + dumpExpr(p->count.get()) + " " + dumpStmt(p->body.get()) + ")";
    }
    if (dynamic_cast<const BreakStmt*>(s))    return "(Break)";
    if (dynamic_cast<const ContinueStmt*>(s)) return "(Continue)";
    if (auto p = dynamic_cast<const ReturnStmt*>(s)) {
        // 「返回」不带值也是合法的，此时不打印占位符
        if (!p->value) return "(Return)";
        return "(Return " + dumpExpr(p->value.get()) + ")";
    }
    if (auto p = dynamic_cast<const FuncDecl*>(s)) {
        std::string out = "(FuncDecl " + p->name + " (";
        for (size_t i = 0; i < p->params.size(); i++) {
            if (i) out += " ";
            out += p->params[i];
        }
        return out + ") " + dumpStmt(p->body.get()) + ")";
    }
    if (auto p = dynamic_cast<const StructDecl*>(s)) {
        std::string out = "(StructDecl " + p->name + " (";
        for (size_t i = 0; i < p->fields.size(); i++) {
            if (i) out += " ";
            out += p->fields[i];
        }
        return out + "))";
    }
    return "?";
}
