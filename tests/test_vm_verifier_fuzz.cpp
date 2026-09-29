#include "lumiere/analysis/analysis.hpp"
#include "lumiere/interpreter/vm/compiler.hpp"
#include "lumiere/interpreter/vm/verifier.hpp"
#include "lumiere/interpreter/vm/vm.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <ctime>
#endif

namespace lumiere
{
namespace
{

/**
 * @brief The claim the verifier exists to support, put under attack.
 *
 * Bytecode is verified once — operands present, table indices in range, jump
 * targets on instruction boundaries, the last instruction unable to fall through
 * — so that the interpreter can then read operands without checking them again.
 * Every unchecked read afterwards rests on that pass being right.
 *
 * Nothing held it to account. These tests corrupt real bytecode at random and
 * assert the one property that matters: **whatever the verifier accepts, running
 * it does not crash.** A rejection is the expected and uninteresting outcome; an
 * acceptance that then dies is a hole in the verifier, and with the sanitizers
 * on it is a precise report rather than a puzzle.
 *
 * Two things the test deliberately does not claim. A mutated module may compute
 * nonsense — that is not a verifier concern. And it may loop forever, because a
 * legitimate Lumière program may too; termination was never part of the
 * guarantee, so a run that does not finish is skipped rather than failed.
 *
 * Each candidate runs in a forked child, which is what makes a crash
 * observable as a signal instead of taking the test process with it.
 */

/** @brief A program with enough shape to give the mutator something to break. */
constexpr const char *kSource = R"(classe Compteur {
    valeur: Entier

    fonction incrementer(pas: Entier) -> Entier {
        ici.valeur = ici.valeur + pas
        retourne ici.valeur
    }
}

fonction somme(valeurs: Liste[Entier]) -> Entier {
    soit total = 0
    pour chaque valeur dans valeurs {
        total = total + valeur
    }
    retourne total
}

fonction principal() {
    soit compteur = Compteur(valeur: 0)
    soit index = 0
    tant que index < 3 {
        compteur.incrementer(index)
        index = index + 1
    }
    soit nombres = [1, 2, 3]
    soit texte = "total:" + somme(nombres)
    soit table = {"a": 1, "b": 2}
    afficher(texte + table["a"])
    afficher(compteur.valeur)
}
)";

ModuleBytecode compile_reference_module()
{
    AnalysisResult analysis = analyze_source(kSource, "fuzz.lum", AnalysisOptions{false, true});
    EXPECT_FALSE(analysis.has_errors()) << "le programme de référence doit compiler";
    Program program{std::move(analysis.statements), "fuzz.lum", kSource};
    VmCompiler compiler;
    return compiler.compile(program);
}

std::size_t environment_number(const char *name, const std::size_t fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr)
    {
        return fallback;
    }
    const long long parsed = std::atoll(raw);
    return parsed > 0 ? static_cast<std::size_t>(parsed) : fallback;
}

/** @brief Corrupts a few bytes of one function's code. */
void mutate(ModuleBytecode &module, std::mt19937_64 &rng)
{
    std::vector<FunctionBytecode *> with_code;
    for (FunctionBytecode &function : module.functions)
    {
        if (!function.chunk.code.empty())
        {
            with_code.push_back(&function);
        }
    }
    ASSERT_FALSE(with_code.empty());

    FunctionBytecode &target =
        *with_code[std::uniform_int_distribution<std::size_t>(0, with_code.size() - 1)(rng)];
    std::vector<std::uint8_t> &code = target.chunk.code;

    const std::size_t edits = std::uniform_int_distribution<std::size_t>(1, 3)(rng);
    for (std::size_t i = 0; i < edits; ++i)
    {
        const std::size_t position =
            std::uniform_int_distribution<std::size_t>(0, code.size() - 1)(rng);
        // A mix of wholly random bytes and small nudges: a nudge is what turns a
        // valid operand into a neighbouring one, which is the interesting case.
        code[position] = std::uniform_int_distribution<int>(0, 3)(rng) == 0
                             ? static_cast<std::uint8_t>(
                                   std::uniform_int_distribution<int>(0, 255)(rng))
                             : static_cast<std::uint8_t>(
                                   code[position] +
                                   std::uniform_int_distribution<int>(-4, 4)(rng));
    }
}

#ifndef _WIN32

enum class Outcome
{
    Finished,  //< Ran to completion or raised a runtime error. Either is fine.
    TimedOut,  //< Still running: a verified module may loop, so this is not a fault.
    Crashed,   //< Died on a signal. This is the finding.
};

/** @brief Runs a module in a child process so a crash is observable. */
Outcome run_in_child(const ModuleBytecode &module, const double seconds)
{
    const pid_t child = ::fork();
    if (child == 0)
    {
        // The child's job is to survive or die; its output is noise either way.
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
        }
        try
        {
            VM vm;
            static_cast<void>(vm.run(module));
        }
        catch (...)
        {
            // A mutated module raising is ordinary: it is the unchecked read
            // this test hunts for, not a diagnostic.
        }
        // _exit, not exit: no atexit handlers, so no leak check and no gtest
        // teardown from a process that was never meant to finish the suite.
        ::_exit(0);
    }
    EXPECT_GE(child, 0) << "fork a échoué";
    if (child < 0)
    {
        return Outcome::TimedOut;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    for (;;)
    {
        int status = 0;
        const pid_t finished = ::waitpid(child, &status, WNOHANG);
        if (finished == child)
        {
            return WIFSIGNALED(status) ? Outcome::Crashed : Outcome::Finished;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            ::kill(child, SIGKILL);
            int discarded = 0;
            ::waitpid(child, &discarded, 0);
            return Outcome::TimedOut;
        }
        const timespec pause{0, 1'000'000};
        ::nanosleep(&pause, nullptr);
    }
}

TEST(VmVerifier, AcceptedBytecodeSurvivesExecution)
{
    const ModuleBytecode reference = compile_reference_module();
    ASSERT_FALSE(verify_module(reference).has_value()) << "le module de référence doit se vérifier";

    // Fixed by default so a failure is reproducible; both are overridable for a
    // longer campaign than a test suite should run on its own.
    const std::size_t seed = environment_number("LUMIERE_FUZZ_SEED", 20260920);
    const std::size_t attempts = environment_number("LUMIERE_FUZZ_ATTEMPTS", 400);

    std::mt19937_64 rng(seed);
    std::size_t accepted = 0;
    std::size_t finished = 0;
    std::size_t timed_out = 0;

    for (std::size_t attempt = 0; attempt < attempts; ++attempt)
    {
        ModuleBytecode mutated = reference;
        mutate(mutated, rng);
        if (::testing::Test::HasFatalFailure())
        {
            return;
        }
        if (verify_module(mutated).has_value())
        {
            continue;  // Rejected, which is the point of the verifier.
        }

        ++accepted;
        switch (run_in_child(mutated, 2.0))
        {
        case Outcome::Finished:
            ++finished;
            break;
        case Outcome::TimedOut:
            ++timed_out;
            break;
        case Outcome::Crashed:
            ADD_FAILURE() << "bytecode accepté par le vérificateur puis planté à l'exécution ; "
                          << "graine " << seed << ", tentative " << attempt;
            return;
        }
    }

    // A run where the verifier rejected everything would pass while testing
    // nothing, so say how much actually reached the interpreter.
    EXPECT_GT(accepted, 0u) << "aucune mutation n'a passé le vérificateur : "
                               "le test ne prouve rien tel quel";
    std::cout << "[fuzz] " << attempts << " mutations, " << accepted
              << " acceptées, " << finished << " exécutées, " << timed_out
              << " sans fin (graine " << seed << ")\n";
}

#else

TEST(VmVerifier, AcceptedBytecodeSurvivesExecution)
{
    GTEST_SKIP() << "le harnais isole chaque tentative par fork(), absent de Windows";
}

#endif

} // namespace
} // namespace lumiere
