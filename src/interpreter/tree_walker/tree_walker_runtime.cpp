#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

#include <sstream>

namespace lumiere
{

TreeWalker::StackFrameGuard::StackFrameGuard(TreeWalker &walker, StackFrame frame)
    : m_walker(&walker)
{
    m_walker->push_stack_frame(std::move(frame));
}

TreeWalker::StackFrameGuard::~StackFrameGuard()
{
    if (m_walker != nullptr)
    {
        m_walker->pop_stack_frame();
    }
}

void TreeWalker::push_stack_frame(StackFrame frame)
{
    m_stack_trace.push_back(std::move(frame));
}

void TreeWalker::pop_stack_frame()
{
    if (!m_stack_trace.empty())
    {
        m_stack_trace.pop_back();
    }
}

[[noreturn]] void TreeWalker::throw_runtime_error(const Token &token, const std::string &message) const
{
    throw RuntimeError(message, m_current_source_path, m_current_source_text, token.line, token.column, m_stack_trace);
}

[[noreturn]] void TreeWalker::throw_runtime_error(const std::string &message) const
{
    throw RuntimeError(message, m_current_source_path, m_current_source_text, 0, 0, m_stack_trace);
}

} // namespace lumiere
