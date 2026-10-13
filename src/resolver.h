#pragma once
#include "ast.h"
#include <string>
#include <unordered_map>
#include <vector>

// 解析期把变量引用绑定到槽位下标。
//
// 为什么要做这件事：
//   原先每次变量访问都要在 unordered_map<string, Value> 上做一次字符串
//   哈希与比较。基准测试显示这正是最大的开销来源——sort 基准比 CPython
//   慢 64 倍，热点就是数组下标访问里的两次变量查找（查数组名、查下标名）。
//   把变量在解析期绑定成下标之后，运行期只剩一次 vector 寻址。
//
// 绑定规则：
//   - 函数内的局部变量（含参数、遍历的循环变量）分配槽位
//   - 全局变量不分配槽位，仍按名字查全局表（数量少，开销可忽略）
//   - 同名变量在不同块里会分到不同槽位，因此块级作用域语义完全不变
//   - 函数每次调用都用同一套槽位，天然支持递归（各自持有独立的帧）
class Resolver {
public:
    // 就地修改语法树：填充 VarExpr::slot、VarDecl::slot、
    // ForEachStmt::varSlot 与 FuncDecl::slotCount
    void resolve(std::vector<StmtPtr>& program);

private:
    // 当前函数内的作用域栈，栈顶是最近进入的块
    std::vector<std::unordered_map<std::string, int>> scopes_;
    int nextSlot_ = 0;
    bool inFunction_ = false;

    void pushScope();
    void popScope();
    int declare(const std::string& name);   // 分配新槽位并登记
    int lookup(const std::string& name) const;  // 沿作用域栈查找

    void resolveStmt(Stmt* s);
    void resolveExpr(Expr* e);
};
