#pragma once

#include "lumiere/lexer/token.hpp"
#include <memory>
#include <set>
#include <string>
#include <vector>


namespace lumiere
{


//  A single value in a TAC instruction.
//  version is always 0 until SSA renaming runs.
struct Operand
{
    enum class Kind
    {
        TEMP,   // compiler-generated temporary:  %t0, %t1 ...
        VAR,    // user-declared variable:         x, total
        CONST,  // literal constant:               42, "bonjour", vrai
        NONE,   // empty / unused operand slot
    };

    Kind        kind    = Kind::NONE;
    std::string name    = "";   // variable or temp label
    int         version = 0;    // SSA subscript for use when I implement it

    static Operand temp(const std::string &label)
    {
        return {Kind::TEMP, label, 0};
    }

    static Operand var(const std::string &name)
    {
        return {Kind::VAR, name, 0};
    }

    static Operand constant(const std::string &value)
    {
        return {Kind::CONST, value, 0};
    }

    static Operand none()
    {
        return {Kind::NONE, "", 0};
    }

    bool is_none() const { return kind == Kind::NONE; }
};



//  One TAC instruction inside a basic block.
//  operands is variable-length so PHI nodes
//  (added later) fit without changing this type.
struct Instruction
{
    enum class Op
    {
        // data
        ASSIGN,     // dst = src[0]
        BINARY,     // dst = src[0] op src[1]   (op stored in label)
        UNARY,      // dst = op src[0]           (op stored in label)
        CAST,       // dst = src[0] en Type      (type stored in label)

        // calls
        CALL,       // dst = src[0](src[1..n])
        CALL_METHOD,// dst = src[0].label(src[1..n])
        CALL_SUPER, // dst = parent.label(src[0..n])

        // collections
        BUILD_LIST, // dst = [src[0..n]]
        BUILD_DICT, // dst = {src[0]:src[1], src[2]:src[3] ...}
        BUILD_SET,  // dst = {src[0..n]}
        INDEX_GET,  // dst = src[0][src[1]]
        INDEX_SET,  // src[0][src[1]] = src[2]

        // control flow
        JUMP,       // → jump_target unconditionally
        BRANCH,     // src[0] ? → jump_target : → branch_false
        RETURN,     // return src[0]  (NONE operand = bare retourne)

        // exceptions
        THROW,          // throw src[0]
        PUSH_HANDLER,   // push catch handler → jump_target
        POP_HANDLER,    // pop catch handler
        MATCH_CATCH,    // src[0] instanceof label → jump_target else → branch_false

        // objects
        GET_FIELD,  // dst = src[0].label
        SET_FIELD,  // src[0].label = src[1]
        NEW_OBJECT, // dst = label(src[0..n])   label = class name

        // iteration
        GET_ITER,   // dst = iter(src[0])
        ITER_NEXT,  // dst = next(src[0])  →  jump_target if exhausted
        UNPACK_PAIR,// dst, dst2 = src[0]  (for dict (k,v) iteration)

        // module
        IMPORT,     // dst = import label
        LOAD_MODULE_ATTR, // dst = src[0].label

        // SSA — not used yet, but the slot is reserved
        PHI,        // dst = φ(src[0], src[1], ...)

        // misc
        NOP,
    };

    Op                   op;
    Operand              dst;           // NONE if instruction produces no value
    std::vector<Operand> operands;      // sources, variable length
    std::string          label;         // operator string, field name, class name, etc
    int                  src_line = 0;  // source line for diagnostics

    // jump targets — raw pointers, BasicBlock owns nothing here
    struct BasicBlock *jump_target  = nullptr;
    struct BasicBlock *branch_false = nullptr;
};


//  A maximal straight-line sequence of
//  instructions with one entry and one exit.
struct BasicBlock
{
    int         id;
    std::string label;  // e.g. "entry", "if.then", "loop.header"

    std::vector<Instruction> instructions;

    // CFG edges
    std::vector<BasicBlock *> predecessors;
    std::vector<BasicBlock *> successors;

    // ── SSA fields ── populated later, zero/null until then
    BasicBlock *                 idom        = nullptr;
    std::vector<BasicBlock *>    dom_children;
    std::set<BasicBlock *>       dom_frontier;

    // helpers
    bool is_empty()    const { return instructions.empty(); }
    bool is_sealed()   const { return m_sealed; }
    void seal()              { m_sealed = true; }

    // convenience: the last instruction (terminator)
    const Instruction *terminator() const
    {
        if (instructions.empty())
        {
            return nullptr;
        }
        return &instructions.back();
    }

private:
    bool m_sealed = false;  // all predecessors are known, needed for SSA later
};

//  CFG
//  Owns all basic blocks for one function.
struct CFG
{
    std::string name;   // function or script name

    std::vector<std::unique_ptr<BasicBlock>> blocks;
    BasicBlock *entry = nullptr;
    BasicBlock *exit  = nullptr;

    // factory — the only way to make a block so IDs are always unique
    BasicBlock *make_block(const std::string &label)
    {
        auto block      = std::make_unique<BasicBlock>();
        block->id       = static_cast<int>(blocks.size());
        block->label    = label;
        BasicBlock *ptr = block.get();
        blocks.push_back(std::move(block));
        return ptr;
    }

    // add a directed edge between two blocks
    static void add_edge(BasicBlock *from, BasicBlock *to)
    {
        from->successors.push_back(to);
        to->predecessors.push_back(from);
    }
};

} // namespace lumiere::cfg
