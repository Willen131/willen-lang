#include "interp.h"
#include "lexer.h"
#include "parser.h"
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <windows.h>

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
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
                  << "  repl          交互式解释器\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "repl") return cmdRepl();

    if (argc < 3) {
        std::cerr << "命令 " << cmd << " 需要一个文件名\n";
        return 1;
    }
    if (cmd == "lex") return cmdLex(argv[2]);
    if (cmd == "ast") return cmdAst(argv[2]);
    if (cmd == "run") return cmdRun(argv[2]);

    std::cerr << "未知命令：" << cmd << "\n";
    return 1;
}
