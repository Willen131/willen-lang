#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// Willen 的值类型系统。
//
// 七种类型共用一个 variant，下标即类型：
//   0=空  1=整数(int64)  2=小数(double)  3=字符串  4=布尔  5=数组  6=结构体
//
// 数组与结构体用 shared_ptr 共享：变量赋值、函数传参都只复制指针，
// 因此对它们的修改在所有引用处可见——这是设计文档里明确的引用语义。

struct Value;
struct StructDef;

using ArrayPtr = std::shared_ptr<std::vector<Value>>;

// 结构体定义：字段名到下标。同名的结构只保留一份定义。
struct StructDef {
    std::string name;
    std::vector<std::string> fieldNames;
    int indexOf(const std::string& field) const;   // 找不到返回 -1
};

struct StructInstance {
    std::shared_ptr<StructDef> def;
    std::vector<Value> fields;                     // 与 def->fieldNames 一一对应
};
using StructPtr = std::shared_ptr<StructInstance>;

struct Value {
    std::variant<std::monostate, int64_t, double, std::string, bool,
                 ArrayPtr, StructPtr> v;

    Value() = default;

    static Value makeInt(int64_t i);
    static Value makeFloat(double d);
    static Value makeStr(std::string s);
    static Value makeBool(bool b);
    static Value makeArray();
    static Value makeStruct(std::shared_ptr<StructDef> def);

    bool isNumber() const;        // 整数或小数
    bool isInt() const;
    double toDouble() const;      // 非数字时抛运行时错误
    int64_t toInt() const;

    std::string toString() const;   // 打印用：整数无小数点、数组形如 [1, 2, 3]
    std::string typeName() const;   // "整数" "小数" "字符串" "布尔" "空" "数组" "结构体"

    // 真值判断：只有 假 与 空 为假，其余一律为真（含 0、""、空数组）
    bool truthy() const;
};

// 深度比较：基本类型按值，数组与结构体递归比较内容。
// 不用指针比较，是为了让 [1,2] == [1,2] 得到 真——测试集写起来才符合直觉。
bool valueEquals(const Value& a, const Value& b);

// 由求值器抛出、由 Interpreter::run 统一接住的运行时错误。
// 这不是语言层面的异常机制（Willen 没有 try/catch），
// 只是 C++ 内部传递错误的手段。
struct RuntimeError {
    std::string message;
    int line = 0;
};
