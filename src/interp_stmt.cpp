#include "interp.h"
#include <iostream>
#include <string>

Flow Interpreter::exec(const Stmt* s) {
    if (!s) return Flow::Normal;

    if (auto p = dynamic_cast<const VarDecl*>(s)) {
        Value v = p->init ? eval(p->init.get()) : Value{};
        env_->define(p->name, std::move(v), p->isConst);
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const ExprStmt*>(s)) {
        if (p->expr) eval(p->expr.get());
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const StructDecl*>(s)) {
        auto def = std::make_shared<StructDef>();
        def->name = p->name;
        def->fieldNames = p->fields;
        structDefs_[p->name] = def;
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const FuncDecl*>(s)) {
        funcs_[p->name] = p;   // 函数一律在顶层登记，不做闭包
        return Flow::Normal;
    }

    // 块语句自建一层作用域，离开时销毁——这就是块级作用域的实现
    if (auto p = dynamic_cast<const BlockStmt*>(s)) {
        Env scope;
        scope.parent = env_;
        Env* saved = env_;
        env_ = &scope;

        Flow flow = Flow::Normal;
        for (const StmtPtr& st : p->stmts) {
            flow = exec(st.get());
            if (flow != Flow::Normal) break;
        }

        env_ = saved;
        return flow;
    }

    if (auto p = dynamic_cast<const IfStmt*>(s)) {
        if (eval(p->cond.get()).truthy()) return exec(p->thenB.get());
        if (p->elseB) return exec(p->elseB.get());
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const WhileStmt*>(s)) {
        while (eval(p->cond.get()).truthy()) {
            Flow flow = exec(p->body.get());
            if (flow == Flow::Break) break;
            if (flow == Flow::Return) return flow;
            // Continue 与 Normal 都进入下一轮
        }
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const ForEachStmt*>(s)) {
        Value iterable = eval(p->iterable.get());

        // 先把可遍历对象取成一份快照。这样循环体内修改原数组
        // 不会让迭代过程错乱，行为可预期。
        std::vector<Value> items;
        if (auto arr = std::get_if<ArrayPtr>(&iterable.v)) {
            items = **arr;
        } else if (auto str = std::get_if<std::string>(&iterable.v)) {
            for (char c : *str) items.push_back(Value::makeStr(std::string(1, c)));
        } else {
            throw RuntimeError{iterable.typeName() + " 不能被遍历", s->line};
        }

        for (const Value& item : items) {
            env_->vars[p->var] = item;
            Flow flow = exec(p->body.get());
            if (flow == Flow::Break) break;
            if (flow == Flow::Return) return flow;
        }
        return Flow::Normal;
    }

    if (auto p = dynamic_cast<const RepeatStmt*>(s)) {
        Value times = eval(p->count.get());
        if (!times.isInt()) {
            throw RuntimeError{"重复次数必须是整数，实际是 " + times.typeName(), s->line};
        }
        for (int64_t i = 0; i < times.toInt(); i++) {
            Flow flow = exec(p->body.get());
            if (flow == Flow::Break) break;
            if (flow == Flow::Return) return flow;
        }
        return Flow::Normal;
    }

    if (dynamic_cast<const BreakStmt*>(s))    return Flow::Break;
    if (dynamic_cast<const ContinueStmt*>(s)) return Flow::Continue;

    if (auto p = dynamic_cast<const ReturnStmt*>(s)) {
        returnValue_ = p->value ? eval(p->value.get()) : Value{};
        return Flow::Return;
    }

    throw RuntimeError{"无法执行的语句", s->line};
}

Value Interpreter::callFunction(const FuncDecl* fn, std::vector<Value>& args,
                                int line) {
    if (args.size() != fn->params.size()) {
        throw RuntimeError{"函数 " + fn->name + " 需要 " +
                           std::to_string(fn->params.size()) + " 个参数，实际传入 " +
                           std::to_string(args.size()) + " 个", line};
    }

    if (callDepth_ >= kMaxCallDepth) {
        throw RuntimeError{"递归层数过深（上限 " + std::to_string(kMaxCallDepth) +
                           "），请检查是否存在无限递归", line};
    }
    callDepth_++;

    // 函数的父环境是全局，不是调用点——所以函数看不见调用者的局部变量。
    // 这是「不做闭包」这个设计决定的直接体现。
    Env frame;
    frame.parent = &globals_;
    for (size_t i = 0; i < fn->params.size(); i++) {
        frame.vars[fn->params[i]] = args[i];
    }

    Env* savedEnv = env_;
    Value savedReturn = returnValue_;
    env_ = &frame;
    returnValue_ = Value{};

    Flow flow = exec(fn->body.get());

    // 函数没走到 返回 就结束（或只写了 返回），结果都是空
    Value result = (flow == Flow::Return) ? returnValue_ : Value{};

    returnValue_ = savedReturn;
    env_ = savedEnv;
    callDepth_--;
    return result;
}

void Interpreter::assignTo(const Expr* target, const Value& value, int line) {
    if (auto v = dynamic_cast<const VarExpr*>(target)) {
        for (Env* e = env_; e; e = e->parent) {
            auto it = e->vars.find(v->name);
            if (it == e->vars.end()) continue;
            if (e->consts.count(v->name)) {
                throw RuntimeError{"不能给常量「" + v->name + "」重新赋值", line};
            }
            it->second = value;
            return;
        }
        throw RuntimeError{"未定义的变量「" + v->name + "」", line};
    }

    if (auto ix = dynamic_cast<const IndexExpr*>(target)) {
        Value obj = eval(ix->object.get());
        Value idx = eval(ix->index.get());
        auto arr = std::get_if<ArrayPtr>(&obj.v);
        if (!arr) throw RuntimeError{"只有数组元素可以这样赋值", line};
        if (!idx.isInt()) throw RuntimeError{"下标必须是整数", line};

        int64_t i = idx.toInt();
        int64_t n = static_cast<int64_t>((*arr)->size());
        if (i < 0 || i >= n) {
            throw RuntimeError{"数组下标越界：" + std::to_string(i) +
                               "，长度为 " + std::to_string(n), line};
        }
        (**arr)[i] = value;
        return;
    }

    if (auto f = dynamic_cast<const FieldExpr*>(target)) {
        Value obj = eval(f->object.get());
        auto inst = std::get_if<StructPtr>(&obj.v);
        if (!inst) throw RuntimeError{"只有结构体字段可以这样赋值", line};

        int idx = (*inst)->def->indexOf(f->field);
        if (idx < 0) {
            throw RuntimeError{"结构体 " + (*inst)->def->name +
                               " 没有字段 " + f->field, line};
        }
        (*inst)->fields[idx] = value;
        return;
    }

    throw RuntimeError{"赋值号左边必须是变量、数组元素或结构体字段", line};
}
