#pragma once
#include "ast.h"
#include "value.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 作用域。沿 parent 指针向上查找，构成词法作用域链。
//
// consts 记录哪些名字是常量：给常量重复赋值要报错。这个约束只能在
// 运行期检查——解析期无法判定一次赋值落在哪个作用域里。
struct Env {
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, bool> consts;
    Env* parent = nullptr;

    Value* find(const std::string& name);
    void define(const std::string& name, Value value, bool isConst);
};

// 语法树求值器。
//
// 两路分发：eval 处理表达式，exec 处理语句。
// 运行时错误靠抛出 RuntimeError 向上传播，由 run() 统一接住并打印——
// 这样任何一层出错都不必逐层检查返回值。
class Interpreter {
public:
    Interpreter();

    // 返回是否正常跑完。出错时错误信息已打印到 stderr。
    bool run(const std::vector<StmtPtr>& program);

private:
    Env globals_;
    Env* env_;                       // 当前作用域；顶层时指向 globals_
    std::unordered_map<std::string, std::shared_ptr<StructDef>> structDefs_;

    Value eval(const Expr* e);
    void exec(const Stmt* s);
    void assignTo(const Expr* target, const Value& value, int line);
};
