#include "interp.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <clocale>
#include <filesystem>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <windows.h>

// ---- 路径编码转换 ----
//
// Windows 上窄字符路径是按系统 ANSI 编码（中文环境为 GBK）解释的，
// 而本程序内部的路径字符串一律是 UTF-8；std::filesystem 在 C++17 下
// 唯一的转换入口 u8path 又依赖当前 locale，默认 locale 下遇到非 ASCII
// 会直接抛 "Illegal byte sequence"（实测 setlocale 也救不回来）。
// 因此直接走 Windows API 做转换，行为完全可控。

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &w[0], n);
    return w;
}

static std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// Windows 把命令行参数按系统 ANSI 编码（中文环境是 GBK）传给 main，
// 这里统一转成程序内部使用的 UTF-8。否则用户敲
// `willen run examples/斐波那契.wl` 时，程序收到的是一串 GBK 字节。
static std::string fromSystemEncoding(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return s;
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()),
                        &w[0], n);
    return wideToUtf8(w);
#else
    return s;
#endif
}

// UTF-8 路径字符串 → std::filesystem::path
static std::filesystem::path toPath(const std::string& utf8) {
#ifdef _WIN32
    return std::filesystem::path(utf8ToWide(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}

// std::filesystem::path → UTF-8 路径字符串
static std::string fromPath(const std::filesystem::path& p) {
#ifdef _WIN32
    return wideToUtf8(p.wstring());
#else
    return p.string();
#endif
}

// 读取整个文件
static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// 把字符串内容转义回可见形式，避免真实换行/制表符破坏逐行输出格式。
static std::string escapeForDisplay(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += ch;     break;
        }
    }
    return out;
}

// 模板文本片段的转义。比普通字符串多转义花括号——因为插值片段用 { } 标记，
// 文本里若出现未转义的 { } 就会与插值标记混淆，使输出无法反推原始结构。
static std::string escapeForTemplateText(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '{':  out += "\\{";  break;
            case '}':  out += "\\}";  break;
            default:   out += ch;     break;
        }
    }
    return out;
}

// willen lex <文件> —— 打印词法单元序列，供词法器调试与测试比对。
// 模板字符串显示为 TEMPLATE：插值片段用 { } 标出，文本片段中的花括号转义为 \{ \}。
// 这样输出与源码一一对应且零歧义——文本片段里出现 [ ] 不会被误认成插值标记。
static int cmdLex(const std::string& path) {
    std::string src;
    if (!readFile(path, src)) {
        std::cerr << "无法打开文件：" << path << "\n";
        return 1;
    }
    Lexer lex(src, path);
    std::vector<Token> tokens = lex.tokenize();
    for (const Token& t : tokens) {
        std::cout << t.line << " ";
        if (t.type == Tok::STR_LIT) {
            if (t.isTemplate) {
                std::cout << "TEMPLATE ";
                for (const TemplatePart& p : t.parts) {
                    if (p.isExpr) std::cout << "{" << p.text << "}";
                    else          std::cout << escapeForTemplateText(p.text);
                }
            } else {
                std::cout << "STR_LIT " << escapeForDisplay(t.text);
            }
        } else {
            std::cout << tokName(t.type);
            if (t.type == Tok::IDENT || t.type == Tok::INT_LIT ||
                t.type == Tok::FLOAT_LIT) {
                std::cout << " " << t.text;
            }
        }
        std::cout << "\n";
    }
    return lex.hadError() ? 1 : 0;
}

// willen ast <文件> —— 打印语法树的 S 表达式形式，供解析器调试与测试比对。
static int cmdAst(const std::string& path) {
    std::string src;
    if (!readFile(path, src)) {
        std::cerr << "无法打开文件：" << path << "\n";
        return 1;
    }
    Lexer lex(src, path);
    std::vector<Token> tokens = lex.tokenize();
    if (lex.hadError()) return 1;

    Parser parser(std::move(tokens), path);
    std::vector<StmtPtr> stmts = parser.parse();
    if (parser.hadError()) return 1;

    for (const StmtPtr& s : stmts) {
        std::cout << dumpStmt(s.get()) << "\n";
    }
    return 0;
}

// willen repl —— 交互式解释器。变量与函数定义在整个会话内持续有效。
static int cmdRepl() {
    std::cout << "Willen 交互式解释器。输入 退出 结束，Ctrl+D 亦可。\n";

    Interpreter interp;
    std::string buffer, line;

    for (;;) {
        std::cout << (buffer.empty() ? "> " : "... ");
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (buffer.empty() && (line == "退出" || line == "exit")) break;
        if (line.empty() && buffer.empty()) continue;

        buffer += line + "\n";

        // 花括号没配平就继续读，这样函数与循环可以跨行输入
        int depth = 0;
        for (char c : buffer) {
            if (c == '{') depth++;
            else if (c == '}') depth--;
        }
        if (depth > 0) continue;

        Lexer lex(buffer, "<repl>");
        std::vector<Token> tokens = lex.tokenize();
        if (lex.hadError()) { buffer.clear(); continue; }

        Parser parser(std::move(tokens), "<repl>");
        std::vector<StmtPtr> stmts = parser.parse();
        if (parser.hadError()) { buffer.clear(); continue; }

        Value out;
        bool hasOut = false;
        interp.runRepl(std::move(stmts), out, hasOut);
        if (hasOut) std::cout << out.toString() << "\n";
        buffer.clear();
    }

    std::cout << "\n";
    return 0;
}

// 解析并执行一段源码。供测试运行器按「测试库 + 测试脚本」两段调用。
static bool runSource(Interpreter& interp, const std::string& src,
                      const std::string& name) {
    Lexer lex(src, name);
    std::vector<Token> tokens = lex.tokenize();
    if (lex.hadError()) return false;

    Parser parser(std::move(tokens), name);
    std::vector<StmtPtr> stmts = parser.parse();
    if (parser.hadError()) return false;

    return interp.run(std::move(stmts));
}

// 逐行比对，只报告第一处不同——测试失败时最需要知道的是「哪里开始不对」
static void reportFirstDiff(const std::string& expected, const std::string& actual) {
    std::vector<std::string> want, got;
    std::string line;
    std::istringstream es(expected), as(actual);
    while (std::getline(es, line)) want.push_back(line);
    while (std::getline(as, line)) got.push_back(line);

    size_t n = std::max(want.size(), got.size());
    for (size_t i = 0; i < n; i++) {
        std::string w = i < want.size() ? want[i] : "(没有这一行)";
        std::string g = i < got.size()  ? got[i]  : "(没有这一行)";
        if (w != g) {
            std::cout << "       第 " << (i + 1) << " 行不同\n"
                      << "         期望：" << w << "\n"
                      << "         实际：" << g << "\n";
            return;
        }
    }
}

// willen test <目录> —— 黑盒测试运行器
//
// 扫描目录下的 .wl 文件（测试库除外），凡是配有同名 .expected 的，
// 就先加载测试库、再执行测试脚本，把标准输出与期望逐字节比对。
// 测试脚本与断言库全部用 Willen 语言编写，运行器只负责调度与比对。
static int cmdTest(const std::string& dirArg) {
    namespace fs = std::filesystem;
    const std::string LIB = "测试库.wl";

    // 去掉路径末尾的斜杠。否则「tests/」会拼出「tests//测试库.wl」这样的
    // 双斜杠路径，在 Windows 上打不开文件。
    std::string dir = dirArg;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    if (dir.empty()) dir = ".";

    std::string libSrc;
    if (!readFile(dir + "/" + LIB, libSrc)) {
        std::cerr << "找不到测试库：" << dir << "/" << LIB << "\n";
        return 1;
    }

    // 全程用 fs::path 对象做文件系统操作。
    // 从 std::string 构造 path 会按 ANSI 编码解释，中文文件名会出错；
    // 用 u8path 构造、用 u8string 取回，才能正确往返。
    std::vector<fs::path> files;
    int skipped = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(toPath(dir), ec)) {
        if (!entry.is_regular_file()) continue;

        fs::path p = entry.path();
        std::string name = fromPath(p.filename());
        if (name.size() < 4 || name.compare(name.size() - 3, 3, ".wl") != 0) continue;
        if (name == LIB) continue;

        // 既没有 .expected 也没有 .expected_err 的 .wl 不是测试用例
        //（可能是示例或数据），跳过
        fs::path okPath = p, errPath = p;
        okPath.replace_extension(".expected");
        errPath.replace_extension(".expected_err");
        if (!fs::exists(okPath) && !fs::exists(errPath)) { skipped++; continue; }

        files.push_back(p);
    }
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cout << "目录 " << dir
                  << " 下没有测试用例（需要 .wl 与同名 .expected 配对）\n";
        return 1;
    }

    int passed = 0, failed = 0;
    for (const fs::path& p : files) {
        std::string name = fromPath(p.filename());
        std::string path = fromPath(p);

        fs::path okPath = p, errPath = p;
        okPath.replace_extension(".expected");
        errPath.replace_extension(".expected_err");

        // 每个用例用全新的解释器，避免用例之间互相污染
        Interpreter interp;

        // 两个流都要捕获：正常用例比对 stdout，预期出错的用例比对 stderr
        std::ostringstream capturedOut, capturedErr;
        std::streambuf* savedOut = std::cout.rdbuf(capturedOut.rdbuf());
        std::streambuf* savedErr = std::cerr.rdbuf(capturedErr.rdbuf());

        bool ok = runSource(interp, libSrc, dir + "/" + LIB);
        if (ok) {
            std::string src;
            ok = readFile(path, src) && runSource(interp, src, path);
        }

        std::cout.rdbuf(savedOut);
        std::cerr.rdbuf(savedErr);

        // ---- 预期出错的用例：脚本必须失败，且错误信息与期望一致 ----
        // 这类用例用于验证运行时错误的中文提示，因此比对的是 stderr。
        if (fs::exists(errPath)) {
            std::string expectedErr;
            readFile(fromPath(errPath), expectedErr);

            if (!ok && capturedErr.str() == expectedErr) {
                passed++;
                std::cout << "[通过] " << name << "（预期出错）\n";
            } else {
                failed++;
                std::cout << "[失败] " << name << "（预期出错）\n";
                if (ok) {
                    std::cout << "       脚本本应出错，却正常结束了\n";
                } else {
                    reportFirstDiff(expectedErr, capturedErr.str());
                }
            }
            continue;
        }

        // ---- 正常用例：脚本必须成功，且输出与期望一致 ----
        std::string expected;
        readFile(fromPath(okPath), expected);

        if (ok && capturedOut.str() == expected) {
            passed++;
            std::cout << "[通过] " << name << "\n";
        } else {
            failed++;
            std::cout << "[失败] " << name << "\n";
            if (!ok) {
                std::string e = capturedErr.str();
                while (!e.empty() && (e.back() == '\n' || e.back() == '\r')) {
                    e.pop_back();
                }
                std::cout << "       脚本执行出错：" << e << "\n";
            } else {
                reportFirstDiff(expected, capturedOut.str());
            }
        }
    }

    std::cout << "--------------------------------\n";
    std::cout << "共 " << files.size() << " 个测试文件，通过 " << passed
              << "，失败 " << failed;
    if (skipped > 0) {
        std::cout << "（另有 " << skipped << " 个 .wl 无 .expected，已跳过）";
    }
    std::cout << "\n";
    return failed == 0 ? 0 : 1;
}

// willen run <文件> —— 执行脚本
static int cmdRun(const std::string& path) {
    std::string src;
    if (!readFile(path, src)) {
        std::cerr << "无法打开文件：" << path << "\n";
        return 1;
    }
    Lexer lex(src, path);
    std::vector<Token> tokens = lex.tokenize();
    if (lex.hadError()) return 1;

    Parser parser(std::move(tokens), path);
    std::vector<StmtPtr> stmts = parser.parse();
    if (parser.hadError()) return 1;

    Interpreter interp;
    return interp.run(std::move(stmts)) ? 0 : 1;
}

int main(int argc, char** argv) {
    // 命令行参数先转成 UTF-8，之后一律用 args 而非 argv
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) args.push_back(fromSystemEncoding(argv[i]));

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 关闭 stdout 的文本模式换行转换：Windows 下文本模式会把 "\n"
    // 写成 "\r\n"，导致同一份期望输出在 Windows 与其它平台字节不一致，
    // 测试比对与 Git 版本管理都会出现假差异。
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    if (argc < 2) {
        std::cerr << "用法：willen <命令> [文件]\n"
                  << "命令：\n"
                  << "  run  <文件>   执行脚本\n"
                  << "  lex  <文件>   打印词法单元\n"
                  << "  ast  <文件>   打印语法树\n"
                  << "  test <目录>   运行黑盒测试集\n"
                  << "  repl          交互式解释器\n";
        return 1;
    }

    std::string cmd = args[1];
    if (cmd == "repl") return cmdRepl();

    if (argc < 3) {
        std::cerr << "命令 " << cmd << " 需要一个文件名\n";
        return 1;
    }
    if (cmd == "lex") return cmdLex(args[2]);
    if (cmd == "ast") return cmdAst(args[2]);
    if (cmd == "run") return cmdRun(args[2]);
    if (cmd == "test") return cmdTest(args[2]);

    std::cerr << "未知命令：" << cmd << "\n";
    return 1;
}
