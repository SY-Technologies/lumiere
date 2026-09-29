#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/parser/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lumiere
{

namespace
{

// ---------------------------------------------------------------------------
// Pattern AST
// ---------------------------------------------------------------------------

struct ClassRange
{
    char32_t lo;
    char32_t hi;
};

enum class NodeKind
{
    Literal,
    Any,
    Class,
    Concat,
    Alt,
    Star,
    Plus,
    Opt,
    Repeat,
    Group,
    AnchorStart,
    AnchorEnd,
};

struct RegexNode;
using RegexNodePtr = std::unique_ptr<RegexNode>;

struct RegexNode
{
    NodeKind kind;
    char32_t literal = 0;
    bool class_negated = false;
    std::vector<ClassRange> class_ranges;
    std::vector<RegexNodePtr> children; // Concat: sequence; Alt: alternatives
    RegexNodePtr child;                 // Star/Plus/Opt/Repeat/Group
    int group_index = 0;
    int repeat_min = 0;
    int repeat_max = -1; // -1 means unbounded
};

RegexNodePtr make_node(NodeKind kind)
{
    auto node = std::make_unique<RegexNode>();
    node->kind = kind;
    return node;
}

bool is_ascii_digit(char32_t c)
{
    return c >= U'0' && c <= U'9';
}

bool is_ascii_alnum(char32_t c)
{
    return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

std::vector<ClassRange> digit_ranges()
{
    return {{U'0', U'9'}};
}

std::vector<ClassRange> word_ranges()
{
    return {{U'0', U'9'}, {U'A', U'Z'}, {U'_', U'_'}, {U'a', U'z'}};
}

std::vector<ClassRange> space_ranges()
{
    // \t\n\v\f\r are the contiguous range 0x09-0x0D; plain space is 0x20.
    return {{0x09, 0x0D}, {U' ', U' '}};
}

// ---------------------------------------------------------------------------
// Pattern parser
// ---------------------------------------------------------------------------

struct RegexParseError
{
    std::string cause;
};

// Recursive-descent parser over the pattern's Unicode scalars (not bytes),
// matching this codebase's scalar-indexed convention for text elsewhere
// (Collections, JSON). Deliberately excludes backreferences, lookbehind,
// conditionals, embedded code, and recursion -- the supported grammar is
// exactly what a Thompson-construction NFA can match in linear time, per
// docs/stdlib-foundations.md's explicit requirement for this module.
class PatternParser
{
public:
    explicit PatternParser(const std::string &source)
    {
        std::size_t offset = 0;
        while (offset < source.size())
        {
            char32_t ch = 0;
            const auto next = utf8::decode_one(source, offset, ch);
            if (!next)
            {
                throw RegexParseError{"le motif contient une séquence UTF-8 invalide"};
            }
            m_scalars.push_back(ch);
            offset = *next;
        }
    }

    RegexNodePtr parse()
    {
        RegexNodePtr root = parse_alternation();
        if (!at_end())
        {
            fail("caractère inattendu dans le motif");
        }
        return root;
    }

    int group_count() const
    {
        return m_next_group_index - 1;
    }

private:
    std::vector<char32_t> m_scalars;
    std::size_t m_pos = 0;
    int m_next_group_index = 1;

    [[noreturn]] void fail(const std::string &cause)
    {
        throw RegexParseError{cause};
    }

    bool at_end() const
    {
        return m_pos >= m_scalars.size();
    }

    char32_t peek() const
    {
        return at_end() ? 0 : m_scalars[m_pos];
    }

    char32_t advance()
    {
        return m_scalars[m_pos++];
    }

    bool match(char32_t c)
    {
        if (!at_end() && peek() == c)
        {
            ++m_pos;
            return true;
        }
        return false;
    }

    void expect(char32_t c, const std::string &what)
    {
        if (!match(c))
        {
            fail(what);
        }
    }

    RegexNodePtr parse_alternation()
    {
        std::vector<RegexNodePtr> alts;
        alts.push_back(parse_concat());
        while (match(U'|'))
        {
            alts.push_back(parse_concat());
        }
        if (alts.size() == 1)
        {
            return std::move(alts.front());
        }
        auto node = make_node(NodeKind::Alt);
        node->children = std::move(alts);
        return node;
    }

    RegexNodePtr parse_concat()
    {
        auto node = make_node(NodeKind::Concat);
        while (!at_end() && peek() != U'|' && peek() != U')')
        {
            node->children.push_back(parse_term());
        }
        return node;
    }

    RegexNodePtr parse_term()
    {
        return parse_quantifier(parse_atom());
    }

    // {m,n} bounds: only well-formed digit sequences (with an optional
    // trailing comma and second number) are treated as a quantifier. A
    // malformed '{' -- e.g. one with no digits -- is left as a literal '{',
    // which is the common, forgiving behavior most regex flavors use.
    std::optional<std::pair<int, int>> try_parse_bound()
    {
        const std::size_t save = m_pos;
        if (at_end() || !is_ascii_digit(peek()))
        {
            m_pos = save;
            return std::nullopt;
        }
        int min_value = 0;
        while (!at_end() && is_ascii_digit(peek()))
        {
            min_value = min_value * 10 + int(advance() - U'0');
            if (min_value > 100000)
            {
                m_pos = save;
                return std::nullopt;
            }
        }
        int max_value = min_value;
        if (match(U','))
        {
            if (!at_end() && is_ascii_digit(peek()))
            {
                max_value = 0;
                while (!at_end() && is_ascii_digit(peek()))
                {
                    max_value = max_value * 10 + int(advance() - U'0');
                    if (max_value > 100000)
                    {
                        m_pos = save;
                        return std::nullopt;
                    }
                }
            }
            else
            {
                max_value = -1; // unbounded: {m,}
            }
        }
        if (at_end() || peek() != U'}')
        {
            m_pos = save;
            return std::nullopt;
        }
        advance(); // consume '}'
        return std::make_pair(min_value, max_value);
    }

    RegexNodePtr parse_quantifier(RegexNodePtr atom)
    {
        if (at_end())
        {
            return atom;
        }
        const char32_t c = peek();
        if (c == U'*')
        {
            ++m_pos;
            auto node = make_node(NodeKind::Star);
            node->child = std::move(atom);
            return node;
        }
        if (c == U'+')
        {
            ++m_pos;
            auto node = make_node(NodeKind::Plus);
            node->child = std::move(atom);
            return node;
        }
        if (c == U'?')
        {
            ++m_pos;
            auto node = make_node(NodeKind::Opt);
            node->child = std::move(atom);
            return node;
        }
        if (c == U'{')
        {
            const std::size_t save = m_pos;
            ++m_pos;
            const auto bound = try_parse_bound();
            if (!bound)
            {
                m_pos = save;
                return atom;
            }
            if (bound->second >= 0 && bound->second < bound->first)
            {
                fail("borne de répétition invalide: la borne haute est inférieure à la borne basse");
            }
            auto node = make_node(NodeKind::Repeat);
            node->child = std::move(atom);
            node->repeat_min = bound->first;
            node->repeat_max = bound->second;
            return node;
        }
        return atom;
    }

    RegexNodePtr parse_atom()
    {
        if (at_end())
        {
            fail("motif incomplet");
        }
        const char32_t c = advance();
        switch (c)
        {
        case U'.':
            return make_node(NodeKind::Any);
        case U'^':
            return make_node(NodeKind::AnchorStart);
        case U'$':
            return make_node(NodeKind::AnchorEnd);
        case U'(':
        {
            const int index = m_next_group_index++;
            RegexNodePtr inner = parse_alternation();
            expect(U')', "parenthèse fermante ')' attendue");
            auto node = make_node(NodeKind::Group);
            node->child = std::move(inner);
            node->group_index = index;
            return node;
        }
        case U'[':
            return parse_class();
        case U'\\':
            return parse_escape();
        case U'*':
        case U'+':
        case U'?':
            fail("quantificateur sans opérande");
        case U')':
            fail("parenthèse fermante ')' inattendue");
        default:
        {
            auto node = make_node(NodeKind::Literal);
            node->literal = c;
            return node;
        }
        }
    }

    RegexNodePtr make_shorthand(std::vector<ClassRange> ranges, bool negated)
    {
        auto node = make_node(NodeKind::Class);
        node->class_ranges = std::move(ranges);
        node->class_negated = negated;
        return node;
    }

    RegexNodePtr parse_escape()
    {
        if (at_end())
        {
            fail("séquence d'échappement incomplète en fin de motif");
        }
        const char32_t c = advance();
        switch (c)
        {
        case U'd':
            return make_shorthand(digit_ranges(), false);
        case U'D':
            return make_shorthand(digit_ranges(), true);
        case U'w':
            return make_shorthand(word_ranges(), false);
        case U'W':
            return make_shorthand(word_ranges(), true);
        case U's':
            return make_shorthand(space_ranges(), false);
        case U'S':
            return make_shorthand(space_ranges(), true);
        case U'n':
        case U't':
        case U'r':
        case U'f':
        case U'v':
        case U'0':
        {
            auto node = make_node(NodeKind::Literal);
            node->literal = escape_control(c);
            return node;
        }
        default:
            if (!is_ascii_alnum(c))
            {
                auto node = make_node(NodeKind::Literal);
                node->literal = c;
                return node;
            }
            fail("séquence d'échappement inconnue dans le motif");
        }
    }

    static char32_t escape_control(char32_t c)
    {
        switch (c)
        {
        case U'n':
            return U'\n';
        case U't':
            return U'\t';
        case U'r':
            return U'\r';
        case U'f':
            return U'\f';
        case U'v':
            return U'\v';
        case U'0':
            return U'\0';
        default:
            return c;
        }
    }

    // Parses one class member's character (after any leading backslash has
    // already been consumed by the caller when applicable). Only the
    // non-negated shorthands (\d \w \s) are supported directly inside a
    // bracket expression: a negated shorthand there (\D \W \S) would need
    // real complement-set ranges to union correctly with the rest of the
    // class, which this v1 implementation does not build; it is rejected
    // with a clear message rather than silently mishandled.
    RegexNodePtr parse_class()
    {
        auto node = make_node(NodeKind::Class);
        if (match(U'^'))
        {
            node->class_negated = true;
        }
        bool first = true;
        while (true)
        {
            if (at_end())
            {
                fail("classe de caractères non terminée");
            }
            if (peek() == U']' && !first)
            {
                advance();
                break;
            }
            if (peek() == U']' && first)
            {
                fail("classe de caractères vide");
            }
            first = false;

            if (peek() == U'\\')
            {
                advance();
                if (at_end())
                {
                    fail("séquence d'échappement incomplète dans une classe");
                }
                const char32_t e = advance();
                if (e == U'd' || e == U'w' || e == U's')
                {
                    std::vector<ClassRange> ranges =
                        e == U'd' ? digit_ranges() : (e == U'w' ? word_ranges() : space_ranges());
                    for (const auto &r : ranges)
                    {
                        node->class_ranges.push_back(r);
                    }
                    continue;
                }
                if (e == U'D' || e == U'W' || e == U'S')
                {
                    fail("\\D, \\W et \\S ne sont pas pris en charge à l'intérieur d'une classe []");
                }
                if (!is_ascii_alnum(e))
                {
                    push_class_char(*node, escape_control(e));
                    continue;
                }
                fail("séquence d'échappement inconnue dans une classe");
            }

            push_class_char(*node, advance());
        }
        if (node->class_ranges.empty())
        {
            fail("classe de caractères vide");
        }
        return node;
    }

    // Consumes one class member starting at `lo` (already read), handling an
    // optional trailing "-hi" to form a range. A '-' immediately before ']'
    // is a literal hyphen, matching common regex convention.
    void push_class_char(RegexNode &node, char32_t lo)
    {
        if (!at_end() && peek() == U'-')
        {
            const std::size_t save = m_pos;
            advance();
            if (!at_end() && peek() != U']')
            {
                char32_t hi;
                if (peek() == U'\\')
                {
                    advance();
                    if (at_end())
                    {
                        fail("séquence d'échappement incomplète dans une classe");
                    }
                    const char32_t e = advance();
                    if (!is_ascii_alnum(e))
                    {
                        hi = escape_control(e);
                    }
                    else
                    {
                        fail("séquence d'échappement inconnue dans une classe");
                    }
                }
                else
                {
                    hi = advance();
                }
                if (hi < lo)
                {
                    fail("intervalle de classe invalide: borne haute inférieure à la borne basse");
                }
                node.class_ranges.push_back({lo, hi});
                return;
            }
            m_pos = save;
        }
        node.class_ranges.push_back({lo, lo});
    }
};

// ---------------------------------------------------------------------------
// Compiled program (Thompson-construction NFA, Pike's VM instruction set)
// ---------------------------------------------------------------------------

struct Inst
{
    enum class Op
    {
        CHAR,
        ANY,
        CLASS,
        BOL,
        EOL,
        SAVE,
        JMP,
        SPLIT,
        MATCH,
    };
    Op op;
    char32_t ch = 0;
    int x = -1;
    int y = -1;
    int slot = -1;
    int class_index = -1;
};

struct RegexProgram
{
    std::vector<Inst> instructions;
    std::vector<std::vector<ClassRange>> classes;
    std::vector<bool> class_negated;
    int group_count = 0;
};

class RegexCompiler
{
public:
    RegexProgram compile(const RegexNode &root, int group_count)
    {
        RegexProgram program;
        program.group_count = group_count;
        m_program = &program;
        emit_save(0);
        compile_node(root);
        emit_save(1);
        emit_match();
        m_program = nullptr;
        return program;
    }

private:
    RegexProgram *m_program = nullptr;

    int here() const
    {
        return int(m_program->instructions.size());
    }

    int emit(Inst inst)
    {
        m_program->instructions.push_back(inst);
        return here() - 1;
    }

    void emit_save(int slot)
    {
        Inst inst;
        inst.op = Inst::Op::SAVE;
        inst.slot = slot;
        emit(inst);
    }

    void emit_match()
    {
        Inst inst;
        inst.op = Inst::Op::MATCH;
        emit(inst);
    }

    int emit_jmp()
    {
        Inst inst;
        inst.op = Inst::Op::JMP;
        return emit(inst);
    }

    int emit_split()
    {
        Inst inst;
        inst.op = Inst::Op::SPLIT;
        return emit(inst);
    }

    void emit_char(char32_t c)
    {
        Inst inst;
        inst.op = Inst::Op::CHAR;
        inst.ch = c;
        emit(inst);
    }

    void emit_any()
    {
        Inst inst;
        inst.op = Inst::Op::ANY;
        emit(inst);
    }

    void emit_class(std::vector<ClassRange> ranges, bool negated)
    {
        const int index = int(m_program->classes.size());
        m_program->classes.push_back(std::move(ranges));
        m_program->class_negated.push_back(negated);
        Inst inst;
        inst.op = Inst::Op::CLASS;
        inst.class_index = index;
        emit(inst);
    }

    void compile_node(const RegexNode &node)
    {
        switch (node.kind)
        {
        case NodeKind::Literal:
            emit_char(node.literal);
            return;
        case NodeKind::Any:
            emit_any();
            return;
        case NodeKind::Class:
            emit_class(node.class_ranges, node.class_negated);
            return;
        case NodeKind::AnchorStart:
        {
            Inst inst;
            inst.op = Inst::Op::BOL;
            emit(inst);
            return;
        }
        case NodeKind::AnchorEnd:
        {
            Inst inst;
            inst.op = Inst::Op::EOL;
            emit(inst);
            return;
        }
        case NodeKind::Concat:
            for (const auto &child : node.children)
            {
                compile_node(*child);
            }
            return;
        case NodeKind::Alt:
            compile_alt(node.children);
            return;
        case NodeKind::Star:
            compile_star(*node.child);
            return;
        case NodeKind::Plus:
            compile_plus(*node.child);
            return;
        case NodeKind::Opt:
            compile_opt(*node.child);
            return;
        case NodeKind::Repeat:
            compile_repeat(node);
            return;
        case NodeKind::Group:
            emit_save(2 * node.group_index);
            compile_node(*node.child);
            emit_save(2 * node.group_index + 1);
            return;
        }
    }

    void compile_alt(const std::vector<RegexNodePtr> &alts)
    {
        std::vector<int> jmp_patches;
        for (std::size_t i = 0; i + 1 < alts.size(); ++i)
        {
            const int split_idx = emit_split();
            m_program->instructions[split_idx].x = here();
            compile_node(*alts[i]);
            const int jmp_idx = emit_jmp();
            jmp_patches.push_back(jmp_idx);
            m_program->instructions[split_idx].y = here();
        }
        compile_node(*alts.back());
        const int end = here();
        for (const int idx : jmp_patches)
        {
            m_program->instructions[idx].x = end;
        }
    }

    void compile_star(const RegexNode &child)
    {
        const int split_idx = emit_split();
        m_program->instructions[split_idx].x = here();
        compile_node(child);
        const int jmp_idx = emit_jmp();
        m_program->instructions[jmp_idx].x = split_idx;
        m_program->instructions[split_idx].y = here();
    }

    void compile_plus(const RegexNode &child)
    {
        const int body_start = here();
        compile_node(child);
        const int split_idx = emit_split();
        m_program->instructions[split_idx].x = body_start;
        m_program->instructions[split_idx].y = here();
    }

    void compile_opt(const RegexNode &child)
    {
        const int split_idx = emit_split();
        m_program->instructions[split_idx].x = here();
        compile_node(child);
        m_program->instructions[split_idx].y = here();
    }

    // e{m,n}: m mandatory copies, then (n-m) further optional copies whose
    // "skip" branch all target the same shared exit -- correct because
    // skipping the first optional copy also makes every later one
    // unreachable, which is exactly e{m,n}'s semantics.
    void compile_repeat(const RegexNode &node)
    {
        const RegexNode &child = *node.child;
        for (int i = 0; i < node.repeat_min; ++i)
        {
            compile_node(child);
        }
        if (node.repeat_max < 0)
        {
            compile_star(child);
            return;
        }
        std::vector<int> patches;
        for (int i = 0; i < node.repeat_max - node.repeat_min; ++i)
        {
            const int split_idx = emit_split();
            m_program->instructions[split_idx].x = here();
            compile_node(child);
            patches.push_back(split_idx);
        }
        const int end = here();
        for (const int idx : patches)
        {
            m_program->instructions[idx].y = end;
        }
    }
};

// ---------------------------------------------------------------------------
// Matching (Pike's VM: linear time, thread priority gives leftmost-greedy
// semantics, per-step pc dedup gives both termination on epsilon loops and
// the linear time bound)
// ---------------------------------------------------------------------------

struct Thread
{
    int pc;
    std::vector<int> saved;
};

struct RegexMatchResult
{
    std::size_t start;
    std::size_t end;
    std::vector<int> saved;
};

class RegexMatcher
{
public:
    RegexMatcher(const RegexProgram &program, const std::vector<char32_t> &scalars)
        : m_program(program), m_scalars(scalars), m_visited(program.instructions.size(), -1)
    {
    }

    // `gen` must survive across run() calls on the same matcher (trouver_tous
    // and remplacer/remplacer_tout call run() repeatedly on one instance to
    // scan forward through the text). If it reset to 0 each call, the
    // second call's early gen values would collide with m_visited marks the
    // FIRST call already left behind, making add_thread() wrongly think
    // pc 0 was "already visited this step" and silently find nothing.

    // Runs the program. `unanchored` injects a fresh attempt at every
    // position from `start_pos` onward (lowest priority) until a match is
    // found, giving "leftmost match anywhere"; without it, the only attempt
    // is the one at `start_pos`, giving an anchored match.
    std::optional<RegexMatchResult> run(std::size_t start_pos, bool unanchored)
    {
        const std::size_t n = m_scalars.size();
        std::vector<Thread> clist;
        std::vector<Thread> nlist;
        std::optional<RegexMatchResult> matched;

        for (std::size_t sp = start_pos; sp <= n; ++sp)
        {
            if (!matched.has_value() && (unanchored || sp == start_pos))
            {
                std::vector<int> fresh(std::size_t(2 * (m_program.group_count + 1)), -1);
                add_thread(clist, m_gen, 0, sp, std::move(fresh));
            }
            if (clist.empty())
            {
                break;
            }

            ++m_gen;
            nlist.clear();
            const bool has_char = sp < n;
            const char32_t ch = has_char ? m_scalars[sp] : 0;

            for (const Thread &thread : clist)
            {
                const Inst &inst = m_program.instructions[thread.pc];
                switch (inst.op)
                {
                case Inst::Op::CHAR:
                    if (has_char && ch == inst.ch)
                    {
                        add_thread(nlist, m_gen, thread.pc + 1, sp + 1, thread.saved);
                    }
                    break;
                case Inst::Op::ANY:
                    if (has_char)
                    {
                        add_thread(nlist, m_gen, thread.pc + 1, sp + 1, thread.saved);
                    }
                    break;
                case Inst::Op::CLASS:
                    if (has_char && class_matches(inst.class_index, ch))
                    {
                        add_thread(nlist, m_gen, thread.pc + 1, sp + 1, thread.saved);
                    }
                    break;
                case Inst::Op::MATCH:
                {
                    RegexMatchResult result;
                    result.start = std::size_t(thread.saved[0] >= 0 ? thread.saved[0] : sp);
                    result.end = sp;
                    result.saved = thread.saved;
                    matched = std::move(result);
                    // Lower-priority threads in this step cannot beat a
                    // match a higher-priority thread already reached.
                    goto step_done;
                }
                default:
                    // BOL/EOL/SAVE/JMP/SPLIT are epsilon transitions that
                    // add_thread() already resolved; they never sit in a
                    // thread list directly.
                    break;
                }
            }
        step_done:
            clist.swap(nlist);
        }
        return matched;
    }

private:
    const RegexProgram &m_program;
    const std::vector<char32_t> &m_scalars;
    std::vector<int> m_visited;
    int m_gen = 0;

    bool class_matches(int class_index, char32_t ch) const
    {
        bool in_range = false;
        for (const auto &range : m_program.classes[class_index])
        {
            if (ch >= range.lo && ch <= range.hi)
            {
                in_range = true;
                break;
            }
        }
        return m_program.class_negated[class_index] ? !in_range : in_range;
    }

    void add_thread(std::vector<Thread> &list, int gen, int pc, std::size_t sp, std::vector<int> saved)
    {
        if (m_visited[pc] == gen)
        {
            return;
        }
        m_visited[pc] = gen;

        const Inst &inst = m_program.instructions[pc];
        switch (inst.op)
        {
        case Inst::Op::JMP:
            add_thread(list, gen, inst.x, sp, std::move(saved));
            return;
        case Inst::Op::SPLIT:
            add_thread(list, gen, inst.x, sp, saved);
            add_thread(list, gen, inst.y, sp, std::move(saved));
            return;
        case Inst::Op::SAVE:
            saved[std::size_t(inst.slot)] = int(sp);
            add_thread(list, gen, pc + 1, sp, std::move(saved));
            return;
        case Inst::Op::BOL:
            if (sp == 0)
            {
                add_thread(list, gen, pc + 1, sp, std::move(saved));
            }
            return;
        case Inst::Op::EOL:
            if (sp == m_scalars.size())
            {
                add_thread(list, gen, pc + 1, sp, std::move(saved));
            }
            return;
        default:
            // CHAR, ANY, CLASS, MATCH: real, input-consuming (or terminal)
            // instructions -- these are what actually populate a step.
            list.push_back(Thread{pc, std::move(saved)});
            return;
        }
    }
};

// ---------------------------------------------------------------------------
// Text <-> Unicode scalar bridging
// ---------------------------------------------------------------------------

struct DecodedText
{
    std::vector<char32_t> scalars;
    std::vector<std::size_t> byte_offsets; // size() == scalars.size() + 1
};

DecodedText decode_text(IRuntime &runtime, const std::string &text, const std::string &context, const RuntimeSite &site)
{
    DecodedText result;
    result.byte_offsets.push_back(0);
    std::size_t offset = 0;
    while (offset < text.size())
    {
        char32_t ch = 0;
        const auto next = utf8::decode_one(text, offset, ch);
        if (!next)
        {
            runtime.raise_runtime_error(site, context + " : texte UTF-8 invalide");
        }
        result.scalars.push_back(ch);
        offset = *next;
        result.byte_offsets.push_back(offset);
    }
    return result;
}

std::string slice_by_scalars(const std::string &text, const std::vector<std::size_t> &byte_offsets, std::size_t start, std::size_t end)
{
    return text.substr(byte_offsets[start], byte_offsets[end] - byte_offsets[start]);
}

// ---------------------------------------------------------------------------
// Motif (compiled pattern) runtime value
// ---------------------------------------------------------------------------

struct MotifState : NativeState
{
    RegexProgram program;

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

Value make_motif_value(RegexProgram program)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "Regex.Motif";
    klass->type_identity = native_nominal_type_identity(klass->name);
    object->klass = std::move(klass);
    auto state = make_ref<MotifState>();
    state->program = std::move(program);
    object->native_state = std::move(state);
    return Value::objet(std::move(object));
}

const RegexProgram &expect_motif(IRuntime &runtime,
                                 const Value &value,
                                 const std::string &context,
                                 const RuntimeSite &site)
{
    if (!value.is_objet() || value.as_objet() == nullptr || value.as_objet()->klass == nullptr ||
        value.as_objet()->klass->name != "Regex.Motif")
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type Motif");
    }
    auto *state = dynamic_cast<MotifState *>(value.as_objet()->native_state.get());
    if (state == nullptr)
    {
        runtime.raise_runtime_error(site, context + " attend une valeur Motif valide");
    }
    return state->program;
}

// ---------------------------------------------------------------------------
// Correspondance (match) runtime value
// ---------------------------------------------------------------------------

Value make_correspondance_value(const std::string &whole_text,
                                const std::vector<std::size_t> &byte_offsets,
                                const RegexMatchResult &match,
                                int group_count,
                                const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "Regex.Correspondance";
    klass->type_identity = native_nominal_type_identity(klass->name);
    object->klass = std::move(klass);

    const std::string matched_text = slice_by_scalars(whole_text, byte_offsets, match.start, match.end);
    const std::size_t start = match.start;
    const std::size_t end = match.end;

    object->fields["texte"] = Value::fonction(make_native_function(
        [matched_text](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Correspondance.texte", native_args.site);
            return Value::texte(matched_text);
        }));
    object->fields["début"] = Value::fonction(make_native_function(
        [start](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Correspondance.début", native_args.site);
            return Value::entier(int64_t(start));
        }));
    object->fields["fin"] = Value::fonction(make_native_function(
        [end](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Correspondance.fin", native_args.site);
            return Value::entier(int64_t(end));
        }));

    // Captured once so texte()/groupe()/groupes() work even after the
    // Motif and original match state have gone out of scope.
    std::vector<std::optional<std::string>> group_texts;
    group_texts.reserve(std::size_t(group_count));
    for (int i = 1; i <= group_count; ++i)
    {
        const int gs = match.saved[std::size_t(2 * i)];
        const int ge = match.saved[std::size_t(2 * i + 1)];
        if (gs < 0 || ge < 0)
        {
            group_texts.emplace_back(std::nullopt);
        }
        else
        {
            group_texts.push_back(slice_by_scalars(whole_text, byte_offsets, std::size_t(gs), std::size_t(ge)));
        }
    }

    object->fields["groupe"] = Value::fonction(make_native_function(
        [matched_text, group_texts](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Correspondance.groupe", call_site);
            const int64_t index = stdlib_expect_integer(runtime, args[0].value, "Correspondance.groupe", call_site);
            if (index == 0)
            {
                return Value::texte(matched_text);
            }
            if (index < 0 || index > int64_t(group_texts.size()))
            {
                runtime.raise_runtime_error(call_site, "Correspondance.groupe: numéro de groupe invalide");
            }
            const auto &group = group_texts[std::size_t(index - 1)];
            return group.has_value() ? Value::texte(*group) : Value::rien();
        }));

    object->fields["groupes"] = Value::fonction(make_native_function(
        [group_texts](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Correspondance.groupes", native_args.site);
            auto data = make_ref<ListeData>();
            data->elements.reserve(group_texts.size());
            for (const auto &group : group_texts)
            {
                data->elements.push_back(group.has_value() ? Value::texte(*group) : Value::rien());
            }
            return Value::liste(std::move(data));
        }));

    return Value::objet(std::move(object));
}

// ---------------------------------------------------------------------------
// Replacement template expansion ($0, $1-$99, $$)
// ---------------------------------------------------------------------------

struct ReplacementError
{
    std::string cause;
};

std::string expand_replacement(const std::string &tmpl,
                               const std::string &whole_text,
                               const std::vector<std::size_t> &byte_offsets,
                               const RegexMatchResult &match,
                               int group_count)
{
    std::string out;
    std::size_t i = 0;
    while (i < tmpl.size())
    {
        if (tmpl[i] == '$' && i + 1 < tmpl.size())
        {
            if (tmpl[i + 1] == '$')
            {
                out += '$';
                i += 2;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(tmpl[i + 1])))
            {
                std::size_t j = i + 1;
                int number = 0;
                int digits = 0;
                while (j < tmpl.size() && digits < 2 && std::isdigit(static_cast<unsigned char>(tmpl[j])))
                {
                    number = number * 10 + (tmpl[j] - '0');
                    ++j;
                    ++digits;
                }
                if (number > group_count)
                {
                    throw ReplacementError{"numéro de groupe invalide dans le remplacement: $" + std::to_string(number)};
                }
                if (number == 0)
                {
                    out += slice_by_scalars(whole_text, byte_offsets, match.start, match.end);
                }
                else
                {
                    const int gs = match.saved[std::size_t(2 * number)];
                    const int ge = match.saved[std::size_t(2 * number + 1)];
                    if (gs >= 0 && ge >= 0)
                    {
                        out += slice_by_scalars(whole_text, byte_offsets, std::size_t(gs), std::size_t(ge));
                    }
                    // An unavailable (non-participating) capture is an empty
                    // replacement, per docs/stdlib-foundations.md.
                }
                i = j;
                continue;
            }
        }
        out += tmpl[i++];
    }
    return out;
}

}

void register_regex_module(Module &module)
{
    const auto &make_native_function = native_function_factory();

    stdlib_bind_public_type(module, "Motif");
    stdlib_bind_public_type(module, "Correspondance");
    auto error_class = make_ref<LumiereClass>();
    error_class->name = "Regex.ErreurRegex";
    stdlib_bind_public_value(module, "ErreurRegex", Value::classe(std::move(error_class)));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "analyser",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Regex.analyser", call_site);
            const std::string source = stdlib_expect_text(runtime, args[0].value, "Regex.analyser", call_site);
            try
            {
                PatternParser parser(source);
                RegexNodePtr ast = parser.parse();
                RegexCompiler compiler;
                RegexProgram program = compiler.compile(*ast, parser.group_count());
                return stdlib_success(make_motif_value(std::move(program)));
            }
            catch (const RegexParseError &error)
            {
                return stdlib_failure(
                    stdlib_error_value("Regex.ErreurRegex", "analyser", error.cause), call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "correspond",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Regex.correspond", call_site);
            const RegexProgram &program = expect_motif(runtime, args[0].value, "Regex.correspond", call_site);
            const std::string text = stdlib_expect_text(runtime, args[1].value, "Regex.correspond", call_site);
            const DecodedText decoded = decode_text(runtime, text, "Regex.correspond", call_site);
            RegexMatcher matcher(program, decoded.scalars);
            const auto result = matcher.run(0, false);
            return Value::logique(result.has_value() && result->start == 0 && result->end == decoded.scalars.size());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "chercher",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Regex.chercher", call_site);
            const RegexProgram &program = expect_motif(runtime, args[0].value, "Regex.chercher", call_site);
            const std::string text = stdlib_expect_text(runtime, args[1].value, "Regex.chercher", call_site);
            const DecodedText decoded = decode_text(runtime, text, "Regex.chercher", call_site);
            RegexMatcher matcher(program, decoded.scalars);
            const auto result = matcher.run(0, true);
            if (!result)
            {
                return Value::rien();
            }
            return make_correspondance_value(
                text, decoded.byte_offsets, *result, program.group_count, make_native_function);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "trouver_tous",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Regex.trouver_tous", call_site);
            const RegexProgram &program = expect_motif(runtime, args[0].value, "Regex.trouver_tous", call_site);
            const std::string text = stdlib_expect_text(runtime, args[1].value, "Regex.trouver_tous", call_site);
            const DecodedText decoded = decode_text(runtime, text, "Regex.trouver_tous", call_site);
            RegexMatcher matcher(program, decoded.scalars);

            auto data = make_ref<ListeData>();
            std::size_t pos = 0;
            while (pos <= decoded.scalars.size())
            {
                const auto result = matcher.run(pos, true);
                if (!result)
                {
                    break;
                }
                data->elements.push_back(make_correspondance_value(
                    text, decoded.byte_offsets, *result, program.group_count, make_native_function));
                // Advancing by one Unicode scalar after an empty match
                // guarantees this loop always terminates, per
                // docs/stdlib-foundations.md.
                pos = result->end > result->start ? result->end : result->end + 1;
            }
            return Value::liste(std::move(data));
        });

    const auto bind_replace = [&](const std::string &name, bool all) {
        stdlib_bind_public_function(
            module,
            make_native_function,
            name,
            [name, all](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                const auto &args = *native_args.arguments;
                const auto &call_site = native_args.site;
                stdlib_expect_positional(runtime, args, 3, "Regex." + name, call_site);
                const RegexProgram &program = expect_motif(runtime, args[0].value, "Regex." + name, call_site);
                const std::string text = stdlib_expect_text(runtime, args[1].value, "Regex." + name, call_site);
                const std::string replacement =
                    stdlib_expect_text(runtime, args[2].value, "Regex." + name, call_site);
                const DecodedText decoded = decode_text(runtime, text, "Regex." + name, call_site);
                RegexMatcher matcher(program, decoded.scalars);

                std::string out;
                std::size_t pos = 0;
                std::size_t last_copied_byte = 0;
                bool replaced_once = false;
                while (pos <= decoded.scalars.size())
                {
                    const auto result = matcher.run(pos, true);
                    if (!result)
                    {
                        break;
                    }
                    const std::size_t match_start_byte = decoded.byte_offsets[result->start];
                    out += text.substr(last_copied_byte, match_start_byte - last_copied_byte);
                    try
                    {
                        out += expand_replacement(
                            replacement, text, decoded.byte_offsets, *result, program.group_count);
                    }
                    catch (const ReplacementError &error)
                    {
                        runtime.raise_runtime_error(call_site, "Regex." + name + ": " + error.cause);
                    }
                    last_copied_byte = decoded.byte_offsets[result->end];
                    replaced_once = true;
                    pos = result->end > result->start ? result->end : result->end + 1;
                    if (false == all)
                    {
                        break;
                    }
                }
                (void)replaced_once;
                out += text.substr(last_copied_byte);
                return Value::texte(out);
            });
    };
    bind_replace("remplacer", false);
    bind_replace("remplacer_tout", true);
}

} // namespace lumiere
