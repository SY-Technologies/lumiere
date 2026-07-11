#define ERROR 0
#include "lumiere/diagnostics/diagnostic.hpp"
#undef ERROR

#include <gtest/gtest.h>

TEST(WindowsHeaderCompatibility, DiagnosticSeverityDoesNotConflictWithErrorMacro)
{
    const lumiere::Diagnostic diagnostic;

    EXPECT_EQ(diagnostic.severity, lumiere::DiagnosticSeverity::ERROR_LEVEL);
}
