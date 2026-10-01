#include "value.h"
#include <sstream>

int StructDef::indexOf(const std::string& field) const {
    for (size_t i = 0; i < fieldNames.size(); i++) {
        if (fieldNames[i] == field) return static_cast<int>(i);
    }
    return -1;
}

Value Value::makeInt(int64_t i)   { Value v; v.v = i; return v; }
Value Value::makeFloat(double d)  { Value v; v.v = d; return v; }
Value Value::makeStr(std::string s) { Value v; v.v = std::move(s); return v; }
Value Value::makeBool(bool b)     { Value v; v.v = b; return v; }

Value Value::makeArray() {
    Value v;
    v.v = std::make_shared<std::vector<Value>>();
    return v;
}

Value Value::makeStruct(std::shared_ptr<StructDef> def) {
    auto inst = std::make_shared<StructInstance>();
    inst->def = std::move(def);
    inst->fields.resize(inst->def->fieldNames.size());
    Value v;
    v.v = std::move(inst);
    return v;
}

bool Value::isNumber() const {
    return std::holds_alternative<int64_t>(v) || std::holds_alternative<double>(v);
}

bool Value::isInt() const {
    return std::holds_alternative<int64_t>(v);
}

double Value::toDouble() const {
    if (auto p = std::get_if<int64_t>(&v)) return static_cast<double>(*p);
    if (auto p = std::get_if<double>(&v))  return *p;
    throw RuntimeError{"这里需要一个数字，实际是 " + typeName(), 0};
}

int64_t Value::toInt() const {
    if (auto p = std::get_if<int64_t>(&v)) return *p;
    if (auto p = std::get_if<double>(&v))  return static_cast<int64_t>(*p);
    throw RuntimeError{"这里需要一个整数，实际是 " + typeName(), 0};
}

std::string Value::typeName() const {
    static const char* names[] = {
        "空", "整数", "小数", "字符串", "布尔", "数组", "结构体"
    };
    return names[v.index()];
}

// 真值判断：只有 假 与 空 为假。0、""、空数组一律视为真——
// 隐式转换是脚本语言里最常见的坑，这里干脆不做。
bool Value::truthy() const {
    if (auto p = std::get_if<bool>(&v)) return *p;
    if (std::holds_alternative<std::monostate>(v)) return false;
    return true;
}

std::string Value::toString() const {
    if (std::holds_alternative<std::monostate>(v)) return "空";

    if (auto p = std::get_if<int64_t>(&v)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&v)) {
        std::ostringstream os;
        os << *p;
        return os.str();
    }
    if (auto p = std::get_if<bool>(&v))        return *p ? "真" : "假";
    if (auto p = std::get_if<std::string>(&v)) return *p;

    if (auto p = std::get_if<ArrayPtr>(&v)) {
        std::string out = "[";
        const std::vector<Value>& items = **p;
        for (size_t i = 0; i < items.size(); i++) {
            if (i) out += ", ";
            out += items[i].toString();
        }
        return out + "]";
    }
    if (auto p = std::get_if<StructPtr>(&v)) {
        const StructInstance& inst = **p;
        std::string out = inst.def->name + "{";
        for (size_t i = 0; i < inst.fields.size(); i++) {
            if (i) out += ", ";
            out += inst.def->fieldNames[i] + ": " + inst.fields[i].toString();
        }
        return out + "}";
    }
    return "?";
}

bool valueEquals(const Value& a, const Value& b) {
    // 整数与小数可以跨类型比较：1 == 1.0 为真
    if (a.isNumber() && b.isNumber()) {
        if (a.isInt() && b.isInt()) {
            return std::get<int64_t>(a.v) == std::get<int64_t>(b.v);
        }
        return a.toDouble() == b.toDouble();
    }

    if (a.v.index() != b.v.index()) return false;

    if (auto p = std::get_if<std::string>(&a.v)) return *p == std::get<std::string>(b.v);
    if (auto p = std::get_if<bool>(&a.v))        return *p == std::get<bool>(b.v);
    if (std::holds_alternative<std::monostate>(a.v)) return true;

    if (auto p = std::get_if<ArrayPtr>(&a.v)) {
        const std::vector<Value>& x = **p;
        const std::vector<Value>& y = *std::get<ArrayPtr>(b.v);
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); i++) {
            if (!valueEquals(x[i], y[i])) return false;
        }
        return true;
    }
    if (auto p = std::get_if<StructPtr>(&a.v)) {
        const StructInstance& x = **p;
        const StructInstance& y = *std::get<StructPtr>(b.v);
        if (x.def->name != y.def->name) return false;
        if (x.fields.size() != y.fields.size()) return false;
        for (size_t i = 0; i < x.fields.size(); i++) {
            if (!valueEquals(x.fields[i], y.fields[i])) return false;
        }
        return true;
    }
    return false;
}
