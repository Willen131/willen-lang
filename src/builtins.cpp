#include "builtins.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <random>

// ---------------- 参数检查辅助 ----------------

static void needArgs(const std::string& name, size_t got, size_t want, int line) {
    if (got != want) {
        throw RuntimeError{name + " 需要 " + std::to_string(want) +
                           " 个参数，实际传入 " + std::to_string(got) + " 个", line};
    }
}

static int64_t needInt(const Value& v, const std::string& what, int line) {
    if (!v.isInt()) {
        throw RuntimeError{what + "必须是整数，实际是 " + v.typeName(), line};
    }
    return v.toInt();
}

static std::string needStr(const Value& v, const std::string& what, int line) {
    if (auto p = std::get_if<std::string>(&v.v)) return *p;
    throw RuntimeError{what + "必须是字符串，实际是 " + v.typeName(), line};
}

// 排序用的比较。数字之间按数值、字符串之间按字典序，混着比就报错。
static bool lessValue(const Value& a, const Value& b, int line) {
    if (a.isNumber() && b.isNumber()) return a.toDouble() < b.toDouble();
    if (auto x = std::get_if<std::string>(&a.v)) {
        if (auto y = std::get_if<std::string>(&b.v)) return *x < *y;
    }
    throw RuntimeError{a.typeName() + " 与 " + b.typeName() + " 不能排序比较", line};
}

// ---------------- 全局内置函数 ----------------

bool callBuiltin(const std::string& name, std::vector<Value>& args,
                 Value& result, int line) {
    if (name == "打印") {
        for (size_t i = 0; i < args.size(); i++) {
            if (i) std::cout << " ";
            std::cout << args[i].toString();
        }
        std::cout << "\n";
        result = Value{};
        return true;
    }

    if (name == "输入") {
        if (args.size() > 1) {
            throw RuntimeError{"输入 最多接受 1 个参数（提示语）", line};
        }
        if (!args.empty()) {
            std::cout << args[0].toString();
            std::cout.flush();
        }
        std::string lineIn;
        if (!std::getline(std::cin, lineIn)) lineIn = "";
        if (!lineIn.empty() && lineIn.back() == '\r') lineIn.pop_back();
        result = Value::makeStr(lineIn);
        return true;
    }

    if (name == "长度") {
        needArgs(name, args.size(), 1, line);
        if (auto arr = std::get_if<ArrayPtr>(&args[0].v)) {
            result = Value::makeInt(static_cast<int64_t>((*arr)->size()));
        } else if (auto s = std::get_if<std::string>(&args[0].v)) {
            result = Value::makeInt(static_cast<int64_t>(s->size()));
        } else {
            throw RuntimeError{"长度 需要数组或字符串，实际是 " +
                               args[0].typeName(), line};
        }
        return true;
    }

    if (name == "整数") {
        needArgs(name, args.size(), 1, line);
        const Value& v = args[0];
        if (v.isInt()) {
            result = v;
        } else if (v.isNumber()) {
            result = Value::makeInt(static_cast<int64_t>(v.toDouble()));
        } else if (auto s = std::get_if<std::string>(&v.v)) {
            try {
                size_t used = 0;
                int64_t n = std::stoll(*s, &used);
                if (used != s->size()) throw std::invalid_argument("有多余字符");
                result = Value::makeInt(n);
            } catch (...) {
                throw RuntimeError{"不能把「" + *s + "」转换成整数", line};
            }
        } else {
            throw RuntimeError{"不能把 " + v.typeName() + " 转换成整数", line};
        }
        return true;
    }

    if (name == "小数") {
        needArgs(name, args.size(), 1, line);
        const Value& v = args[0];
        if (v.isNumber()) {
            result = Value::makeFloat(v.toDouble());
        } else if (auto s = std::get_if<std::string>(&v.v)) {
            try {
                size_t used = 0;
                double d = std::stod(*s, &used);
                if (used != s->size()) throw std::invalid_argument("有多余字符");
                result = Value::makeFloat(d);
            } catch (...) {
                throw RuntimeError{"不能把「" + *s + "」转换成小数", line};
            }
        } else {
            throw RuntimeError{"不能把 " + v.typeName() + " 转换成小数", line};
        }
        return true;
    }

    if (name == "字符串") {
        needArgs(name, args.size(), 1, line);
        result = Value::makeStr(args[0].toString());
        return true;
    }

    if (name == "类型") {
        needArgs(name, args.size(), 1, line);
        result = Value::makeStr(args[0].typeName());
        return true;
    }

    if (name == "随机数") {
        // 随机数发生器只初始化一次，避免同一次运行里反复取种子
        static std::mt19937 rng(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));

        int64_t lo = 0, hi = 99;
        if (args.size() == 1) {
            hi = needInt(args[0], "随机数的上界", line);
        } else if (args.size() == 2) {
            lo = needInt(args[0], "随机数的下界", line);
            hi = needInt(args[1], "随机数的上界", line);
        } else if (args.size() > 2) {
            throw RuntimeError{"随机数 最多接受 2 个参数（下界、上界）", line};
        }
        if (lo > hi) throw RuntimeError{"随机数的下界不能大于上界", line};

        std::uniform_int_distribution<int64_t> dist(lo, hi);
        result = Value::makeInt(dist(rng));
        return true;
    }

    if (name == "时间") {
        needArgs(name, args.size(), 0, line);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result = Value::makeInt(ms);
        return true;
    }

    return false;
}

// ---------------- 内置方法 ----------------

bool callMethod(const Value& object, const std::string& method,
                std::vector<Value>& args, Value& result, int line) {

    // ======== 数组方法 ========
    if (auto arr = std::get_if<ArrayPtr>(&object.v)) {
        std::vector<Value>& v = **arr;

        if (method == "追加") {
            needArgs("追加", args.size(), 1, line);
            v.push_back(args[0]);
            result = object;
            return true;
        }
        if (method == "插入") {
            needArgs("插入", args.size(), 2, line);
            int64_t i = needInt(args[0], "插入位置", line);
            if (i < 0 || i > static_cast<int64_t>(v.size())) {
                throw RuntimeError{"插入位置越界：" + std::to_string(i) +
                                   "，长度为 " + std::to_string(v.size()), line};
            }
            v.insert(v.begin() + i, args[1]);
            result = object;
            return true;
        }
        if (method == "删除") {
            needArgs("删除", args.size(), 1, line);
            int64_t i = needInt(args[0], "删除位置", line);
            if (i < 0 || i >= static_cast<int64_t>(v.size())) {
                throw RuntimeError{"删除位置越界：" + std::to_string(i) +
                                   "，长度为 " + std::to_string(v.size()), line};
            }
            v.erase(v.begin() + i);
            result = object;
            return true;
        }
        if (method == "弹") {
            needArgs("弹", args.size(), 0, line);
            if (v.empty()) throw RuntimeError{"不能从空数组里弹出元素", line};
            result = v.back();
            v.pop_back();
            return true;
        }
        if (method == "包含") {
            needArgs("包含", args.size(), 1, line);
            for (const Value& item : v) {
                if (valueEquals(item, args[0])) {
                    result = Value::makeBool(true);
                    return true;
                }
            }
            result = Value::makeBool(false);
            return true;
        }
        if (method == "查找") {
            needArgs("查找", args.size(), 1, line);
            for (size_t i = 0; i < v.size(); i++) {
                if (valueEquals(v[i], args[0])) {
                    result = Value::makeInt(static_cast<int64_t>(i));
                    return true;
                }
            }
            result = Value::makeInt(-1);
            return true;
        }
        if (method == "反转") {
            needArgs("反转", args.size(), 0, line);
            std::reverse(v.begin(), v.end());
            result = object;
            return true;
        }
        if (method == "排序") {
            needArgs("排序", args.size(), 0, line);
            std::sort(v.begin(), v.end(),
                      [line](const Value& a, const Value& b) {
                          return lessValue(a, b, line);
                      });
            result = object;
            return true;
        }
        if (method == "连接") {
            needArgs("连接", args.size(), 1, line);
            std::string sep = needStr(args[0], "连接符", line);
            std::string out;
            for (size_t i = 0; i < v.size(); i++) {
                if (i) out += sep;
                out += v[i].toString();
            }
            result = Value::makeStr(out);
            return true;
        }
        if (method == "切片") {
            needArgs("切片", args.size(), 2, line);
            int64_t a = needInt(args[0], "切片起点", line);
            int64_t b = needInt(args[1], "切片终点", line);
            int64_t n = static_cast<int64_t>(v.size());
            if (a < 0 || b > n || a > b) {
                throw RuntimeError{"切片范围不合法：[" + std::to_string(a) + ", " +
                                   std::to_string(b) + ")，长度为 " +
                                   std::to_string(n), line};
            }
            result = Value::makeArray();
            std::vector<Value>& out = *std::get<ArrayPtr>(result.v);
            for (int64_t i = a; i < b; i++) out.push_back(v[i]);
            return true;
        }
    }

    // ======== 字符串方法 ========
    // 字符串不可变，以下方法一律返回新字符串
    if (auto sp = std::get_if<std::string>(&object.v)) {
        const std::string& s = *sp;

        if (method == "大写") {
            needArgs("大写", args.size(), 0, line);
            std::string out = s;
            for (char& c : out) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            result = Value::makeStr(out);
            return true;
        }
        if (method == "小写") {
            needArgs("小写", args.size(), 0, line);
            std::string out = s;
            for (char& c : out) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            result = Value::makeStr(out);
            return true;
        }
        if (method == "子串") {
            needArgs("子串", args.size(), 2, line);
            int64_t a = needInt(args[0], "子串起点", line);
            int64_t b = needInt(args[1], "子串终点", line);
            int64_t n = static_cast<int64_t>(s.size());
            if (a < 0 || b > n || a > b) {
                throw RuntimeError{"子串范围不合法：[" + std::to_string(a) + ", " +
                                   std::to_string(b) + ")，长度为 " +
                                   std::to_string(n), line};
            }
            result = Value::makeStr(s.substr(static_cast<size_t>(a),
                                             static_cast<size_t>(b - a)));
            return true;
        }
        if (method == "查找") {
            needArgs("查找", args.size(), 1, line);
            std::string sub = needStr(args[0], "查找内容", line);
            size_t pos = s.find(sub);
            result = Value::makeInt(pos == std::string::npos
                                        ? -1 : static_cast<int64_t>(pos));
            return true;
        }
        if (method == "替换") {
            needArgs("替换", args.size(), 2, line);
            std::string from = needStr(args[0], "被替换的内容", line);
            std::string to   = needStr(args[1], "替换成的内容", line);
            if (from.empty()) throw RuntimeError{"被替换的内容不能为空", line};

            std::string out;
            size_t pos = 0;
            for (;;) {
                size_t hit = s.find(from, pos);
                if (hit == std::string::npos) {
                    out += s.substr(pos);
                    break;
                }
                out += s.substr(pos, hit - pos);
                out += to;
                pos = hit + from.size();
            }
            result = Value::makeStr(out);
            return true;
        }
        if (method == "分割") {
            needArgs("分割", args.size(), 1, line);
            std::string sep = needStr(args[0], "分隔符", line);
            if (sep.empty()) throw RuntimeError{"分隔符不能为空", line};

            result = Value::makeArray();
            std::vector<Value>& out = *std::get<ArrayPtr>(result.v);
            size_t pos = 0;
            for (;;) {
                size_t hit = s.find(sep, pos);
                if (hit == std::string::npos) {
                    out.push_back(Value::makeStr(s.substr(pos)));
                    break;
                }
                out.push_back(Value::makeStr(s.substr(pos, hit - pos)));
                pos = hit + sep.size();
            }
            return true;
        }
        if (method == "去空白") {
            needArgs("去空白", args.size(), 0, line);
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
            result = Value::makeStr(s.substr(a, b - a));
            return true;
        }
        if (method == "包含") {
            needArgs("包含", args.size(), 1, line);
            std::string sub = needStr(args[0], "查找内容", line);
            result = Value::makeBool(s.find(sub) != std::string::npos);
            return true;
        }
        if (method == "字符") {
            needArgs("字符", args.size(), 1, line);
            int64_t i = needInt(args[0], "字符下标", line);
            int64_t n = static_cast<int64_t>(s.size());
            if (i < 0 || i >= n) {
                throw RuntimeError{"字符下标越界：" + std::to_string(i) +
                                   "，长度为 " + std::to_string(n), line};
            }
            result = Value::makeStr(std::string(1, s[static_cast<size_t>(i)]));
            return true;
        }
    }

    throw RuntimeError{object.typeName() + " 没有方法 " + method, line};
}
