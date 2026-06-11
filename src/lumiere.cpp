#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "lumiere/lexer/token.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere//lexer/token.hpp"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: lumiere <fichier.lum>\n";
        return 1;
    }

    const std::string LUM_DIR = "examples";
    const std::string EXT = ".lum";

    std::string file_name(argv[1]);
    std::string file_name_no_ext = std::filesystem::path(file_name).stem().string();
    if (std::filesystem::path(file_name).extension() != EXT)
    {
        file_name = file_name_no_ext + EXT;
    }
    std::filesystem::path current = std::filesystem::current_path();
    std::filesystem::path parent = current.parent_path();
    std::filesystem::path file_path = parent / std::filesystem::path(LUM_DIR) / std::filesystem::path(file_name);
    std::ifstream file(file_path);

    if (!file.is_open())
    {
        std::cerr << "erreur: impossible d'ouvrir '" << argv[1] << "'\n";
        return 1;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    lumiere::Lexer lexer(source);
    std::vector<lumiere::Token> tokens = lexer.tokenise();

    for (const auto &token : tokens)
    {
        std::cout << "[ligne " << token.line
                  << ", col " << token.column
                  << "] " << token.to_string()
                  << '\n';
    }

    return 0;
}