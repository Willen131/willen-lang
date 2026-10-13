#pragma once
#include "ast.h"
#include "value.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 语句执行的控制流信号。
// exec 返回它：循环语句据此决定继续下一轮、跳出、还是把 return 继续往上抛。
enum class Flow { Normal, Break, Continue, Return };

// 全局作用域。
//
// 引入槽位化之后，函数内的局部变量由 Resolver 在解析期绑定成下标，
// 运行期直接按 vector 下标寻址，不再需要作用域链——因此这里只保存
// 全局变量，也不再需要 parent 指针。
//
// consts 记录哪些名字是常量：给常量重复赋值要报错。
struct Env {
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, bool> consts;

    bool isConst(const std::string& name) const { return consts.count(name) > 0; }
};

// 语法树求值器。
//
// 两路分发：eval 处理表达式，exec 处理语句。
// 运行时错误靠抛出 RuntimeError 向上传播，由 run() 统一接住并打印——
// 这样任何一层出错都不必逐层检查返回值。
class Interpreter {
public:
    Interpreter();

    // 参数按值接收：语法树由解释器接管并保活，理由见 programs_ 的注释。
    bool run(std::vector<StmtPtr> program);

    // 供 repl 使用：执行一批语句；若最后一条是表达式语句，
    // 把它的值交给 out 并由 hasOut 标记。这样在交互界面里敲一个表达式
    // 就能直接看到结果，不必每次套一层 打印(...)。
    bool runRepl(std::vector<StmtPtr> stmts, Value& out, bool& hasOut);

private:
    // 递归深度上限。超过即报中文错误，而不是任由 C++ 调用栈溢出崩溃。
    static constexpr int kMaxCallDepth = 200;

    Env globals_;

    // 当前函数帧的槽位。局部变量按 Resolver 分配的下标直接寻址，
    // 省掉了原来每次访问都要做的字符串哈希。
    std::vector<Value> slots_;

    std::unordered_map<std::string, std::shared_ptr<StructDef>> structDefs_;

    // 函数表。Willen 不做闭包，函数一律定义在顶层，因此只需按名字索引。
    std::unordered_map<std::string, const FuncDecl*> funcs_;
    Value returnValue_;              // 函数返回值的传递通道
    int callDepth_ = 0;

    // 语法树保活。
    //
    // funcs_ 里存的是指向 FuncDecl 节点的裸指针，因此被登记过的语法树
    // 必须在解释器存活期间一直有效。单次运行（willen run）不会有问题，
    // 但 repl 每次输入都解析出一棵新树，若不接管所有权，函数表里的指针
    // 会在下一行输入到来时悬空——表现为「定义了函数却调不到」。
    std::vector<std::vector<StmtPtr>> programs_;

    Value eval(const Expr* e);
    Flow exec(const Stmt* s);
    Value callFunction(const FuncDecl* fn, std::vector<Value>& args, int line);
    void assignTo(const Expr* target, const Value& value, int line);
};
