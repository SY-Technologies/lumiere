#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST(LegacyExceptions, ProductionSourcesContainNoRemovedMachinery)
{
    const std::array<std::string, 18> forbidden = {
        "TokenType::ESSAYER",
        "TokenType::ATTRAPER",
        "TokenType::FINALEMENT",
        "TokenType::LANCER",
        "ThrowStmt",
        "TryStmt",
        "CatchClause",
        "ThrownSignal",
        "TRY_BEGIN",
        "TRY_END",
        "IR_OP_TRY_BEGIN",
        "IR_OP_TRY_END",
        "IR_OP_EXCEPTION_VALUE",
        "IR_OP_THROW",
        "PUSH_HANDLER",
        "POP_HANDLER",
        "MATCH_CATCH",
        "route_exception",
    };

    for (const std::string_view directory : {"include", "src"})
    {
        const std::filesystem::path root = std::filesystem::path(LUMIERE_SOURCE_DIR) / directory;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const std::filesystem::path &path = entry.path();
            if (path.extension() != ".hpp" && path.extension() != ".cpp")
            {
                continue;
            }

            const std::string source = read_file(path);
            for (const std::string &symbol : forbidden)
            {
                EXPECT_EQ(source.find(symbol), std::string::npos)
                    << path.string() << " still contains removed symbol " << symbol;
            }
        }
    }
}

} // namespace
