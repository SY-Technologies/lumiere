#pragma once

#include "lumiere/diagnostics/diagnostic.hpp"
#include "lumiere/lexer/token.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumiere
{

/**
 * @brief An opaque, snapshot-local handle for one source document.
 *
 * Interned from the document's logical path by a `SourceRegistry`. Valid
 * only for the registry that issued it -- like `SemanticTypeRef` is only
 * meaningful within the `TypeInterner` that produced it (see
 * `TypeInterner::adopt`'s own comment on that), a `SourceId` from one
 * `SourceRegistry` is never compared against another's. `SourceId{0}` is
 * always the buffer `analyze_source` was called on; every source pulled in
 * afterwards (an imported module, read from disk by
 * `build_import_environment`) gets the next id in the order it was first
 * registered.
 */
enum class SourceId : std::uint32_t
{
};

/**
 * @brief A byte range within one document, identified by that document's
 * `SourceId`.
 *
 * This is `SourceRange` (diagnostics/diagnostic.hpp) plus the `SourceId`
 * needed to tell which document `start`/`end` are offsets into --
 * `SourceRange` itself stays exactly as it is, since diagnostics already
 * carry their document identity separately, as a plain `source_path`
 * string, via `Diagnostic::source_path`. `SourceSpan` is for data that
 * moves between documents in the same snapshot, such as a hover occurrence
 * for a symbol declared in an imported module.
 */
struct SourceSpan
{
    SourceId source{0};
    /** Zero-based UTF-8 byte offset of the first covered byte. */
    std::size_t start = 0;
    /** Zero-based exclusive UTF-8 byte offset; may equal start at EOF. */
    std::size_t end = 0;
};

[[nodiscard]] inline bool operator==(const SourceSpan &left, const SourceSpan &right) noexcept
{
    return left.source == right.source && left.start == right.start && left.end == right.end;
}

/** Builds the `SourceSpan` a token covers within the given document. */
[[nodiscard]] inline SourceSpan span_of(const Token &token, const SourceId source) noexcept
{
    return SourceSpan{source, token.start_offset, token.end_offset};
}

/**
 * @brief Interns document paths to `SourceId`s for one analysis snapshot.
 *
 * One `SourceRegistry` belongs to one `SemanticModel`. `intern` is
 * idempotent: registering the same path twice returns the same id, so a
 * module imported from two different files can be interned once each time
 * it is reached during `build_import_environment`'s walk without minting
 * duplicate ids for it.
 */
class SourceRegistry final
{
public:
    /** Interns `path`, returning its existing id or minting the next one. */
    [[nodiscard]] SourceId intern(const std::string &path)
    {
        if (const auto found = m_ids.find(path); found != m_ids.end())
        {
            return found->second;
        }
        const auto id = static_cast<SourceId>(m_paths.size());
        m_paths.push_back(path);
        m_ids.emplace(path, id);
        return id;
    }

    /** The path a previously interned `SourceId` was registered under. */
    [[nodiscard]] const std::string &path_of(const SourceId id) const
    {
        return m_paths.at(static_cast<std::size_t>(id));
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_paths.size();
    }

private:
    std::vector<std::string> m_paths;
    std::unordered_map<std::string, SourceId> m_ids;
};

} // namespace lumiere
