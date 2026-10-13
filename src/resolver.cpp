#include "resolver.h"

void Resolver::pushScope() { scopes_.push_back({}); }
void Resolver::popScope()  { scopes_.pop_back(); }

// 在当前块里为新变量分配一个槽位。
// 全局作用域不分配——全局变量按名字查表，数量少，哈希开销可忽略。
int Resolver::declare(const std::string& name) {
    if (!inFunction_) return -1;
    int slot = nextSlot_++;
    scopes_.back()[name] = slot;
    return slot;
}

// 沿作用域栈由内向外查找。找不到说明是全局变量（或未定义，交给运行期报错）。
int Resolver::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return -1;
}

void Resolver::resolve(std::vector<StmtPtr>& program) {
    // 顶层就是全局作用域，不做槽位分配
    inFunction_ = false;
    scopes_.clear();
    nextSlot_ = 0;
    for (const StmtPtr& s : program) resolveStmt(s.get());
}

void Resolver::resolveStmt(Stmt* s) {
    if (!s) return;

    // 先解析初值再登记名字——与运行期顺序一致：
    // 「设 甲 = 甲」里的甲应当是外层的那个
    if (auto p = dynamic_cast<VarDecl*>(s)) {
        resolveExpr(p->init.get());
        p->slot = declare(p->name);
        return;
    }

    if (auto p = dynamic_cast<const ExprStmt*>(s)) {
        resolveExpr(p->expr.get());
        return;
    }

    // 结构定义只登记类型，不涉及变量
    if (dynamic_cast<const StructDecl*>(s)) return;

    // 进入函数：换一套全新的作用域栈与槽位编号
    if (auto p = dynamic_cast<FuncDecl*>(s)) {
        std::vector<std::unordered_map<std::string, int>> savedScopes =
            std::move(scopes_);
        int savedSlot = nextSlot_;
        bool savedInFunction = inFunction_;

        scopes_.clear();
        nextSlot_ = 0;
        inFunction_ = true;

        pushScope();
        for (const std::string& param : p->params) declare(param);
        resolveStmt(p->body.get());
        popScope();

        p->slotCount = nextSlot_;

        scopes_ = std::move(savedScopes);
        nextSlot_ = savedSlot;
        inFunction_ = savedInFunction;
        return;
    }

    if (auto p = dynamic_cast<const BlockStmt*>(s)) {
        pushScope();
        for (const StmtPtr& st : p->stmts) resolveStmt(st.get());
        popScope();
        return;
    }

    if (auto p = dynamic_cast<const IfStmt*>(s)) {
        resolveExpr(p->cond.get());
        resolveStmt(p->thenB.get());
        resolveStmt(p->elseB.get());
        return;
    }

    if (auto p = dynamic_cast<const WhileStmt*>(s)) {
        resolveExpr(p->cond.get());
        resolveStmt(p->body.get());
        return;
    }

    if (auto p = dynamic_cast<ForEachStmt*>(s)) {
        resolveExpr(p->iterable.get());
        pushScope();
        p->varSlot = declare(p->var);
        resolveStmt(p->body.get());
        popScope();
        return;
    }

    if (auto p = dynamic_cast<const RepeatStmt*>(s)) {
        resolveExpr(p->count.get());
        resolveStmt(p->body.get());
        return;
    }

    if (auto p = dynamic_cast<const ReturnStmt*>(s)) {
        resolveExpr(p->value.get());
        return;
    }

    // BreakStmt / ContinueStmt 没有子节点
}

void Resolver::resolveExpr(Expr* e) {
    if (!e) return;

    if (auto p = dynamic_cast<VarExpr*>(e)) {
        // 找不到就是全局变量；真未定义的话由运行期报「未定义的变量」
        p->slot = lookup(p->name);
        return;
    }

    if (auto p = dynamic_cast<const BinaryExpr*>(e)) {
        resolveExpr(p->left.get());
        resolveExpr(p->right.get());
        return;
    }
    if (auto p = dynamic_cast<const UnaryExpr*>(e)) {
        resolveExpr(p->operand.get());
        return;
    }
    if (auto p = dynamic_cast<const IndexExpr*>(e)) {
        resolveExpr(p->object.get());
        resolveExpr(p->index.get());
        return;
    }
    if (auto p = dynamic_cast<const FieldExpr*>(e)) {
        resolveExpr(p->object.get());
        return;
    }
    if (auto p = dynamic_cast<const CallExpr*>(e)) {
        resolveExpr(p->callee.get());
        for (const ExprPtr& a : p->args) resolveExpr(a.get());
        return;
    }
    if (auto p = dynamic_cast<const MethodExpr*>(e)) {
        resolveExpr(p->object.get());
        for (const ExprPtr& a : p->args) resolveExpr(a.get());
        return;
    }
    if (auto p = dynamic_cast<const AssignExpr*>(e)) {
        resolveExpr(p->target.get());
        resolveExpr(p->value.get());
        return;
    }
    if (auto p = dynamic_cast<const RangeExpr*>(e)) {
        resolveExpr(p->from.get());
        resolveExpr(p->to.get());
        return;
    }
    if (auto p = dynamic_cast<const ArrayLit*>(e)) {
        for (const ExprPtr& item : p->items) resolveExpr(item.get());
        return;
    }
    if (auto p = dynamic_cast<const StructLit*>(e)) {
        for (const auto& f : p->fields) resolveExpr(f.second.get());
        return;
    }
    if (auto p = dynamic_cast<const TmplLit*>(e)) {
        for (const ExprPtr& piece : p->pieces) resolveExpr(piece.get());
        return;
    }

    // 其余都是字面量节点，没有子表达式
}
