// Pure unit tests for the stage 7/8 pieces that do not need a real clock, a
// real sleep, or a real display: frame-deadline arithmetic and the
// canonical key/button name tables. docs/stdlib-lumidessin.md's "Testing
// contract" calls this out explicitly: "frame-deadline calculation without
// real sleeps". Everything that does need a window (fenêtre() itself,
// présenter(), prochaine_image()'s event pump and close handling) is
// exercised instead as an off-screen or bounded visible-window conformance
// test in test_interpreter_fixtures.cpp -- see LumiDessinWindow* there.

#include "lumidessin/state.hpp"

#include <gtest/gtest.h>

using namespace lumiere;

TEST(LumiDessinFrameWait, FirstCallNeverWaits)
{
    // The first prochaine_image() call reports zero elapsed time and never
    // blocks, regardless of what `next_deadline` happens to hold (it is
    // unset -- 0.0 -- before the first call).
    const FrameWait wait = compute_frame_wait(/*first_call=*/true, /*now=*/100.0, /*next_deadline=*/0.0);
    EXPECT_EQ(wait.wait_seconds, 0.0);
    EXPECT_EQ(wait.frame_now, 100.0);
}

TEST(LumiDessinFrameWait, OnTimeWaitsExactlyToTheDeadline)
{
    // Called well before its deadline: waits the remaining time and is
    // timed from the deadline itself, not from whenever it happened to be
    // called.
    const double interval = 1.0 / 60.0;
    const double next_deadline = 10.0 + interval;
    const FrameWait wait = compute_frame_wait(/*first_call=*/false, /*now=*/10.0, next_deadline);
    // A subtraction-then-comparison round trip (next_deadline - now,
    // against a separately computed interval), so exact/ULP equality is
    // the wrong tool here -- EXPECT_NEAR with a tolerance far finer than
    // anything a frame-pacing decision would ever care about.
    EXPECT_NEAR(wait.wait_seconds, interval, 1e-12);
    EXPECT_EQ(wait.frame_now, next_deadline); // passed through unmodified: exact by construction
}

TEST(LumiDessinFrameWait, ExactlyOnTheDeadlineDoesNotWait)
{
    const double interval = 1.0 / 60.0;
    const FrameWait wait = compute_frame_wait(/*first_call=*/false, /*now=*/5.0, /*next_deadline=*/5.0);
    EXPECT_EQ(wait.wait_seconds, 0.0);
    EXPECT_EQ(wait.frame_now, 5.0);
}

TEST(LumiDessinFrameWait, ALateFrameIsAbandonedNotMadeUp)
{
    // docs/stdlib-lumidessin.md, "Frame lifecycle": "the next deadline
    // advances from the current monotonic time" -- a call arriving after
    // its deadline never waits, and is timed from *now*, not from the
    // missed deadline. This is what keeps a stall (a slow frame, a
    // debugger pause) from turning into a burst of instant catch-up frames
    // afterward.
    const double interval = 1.0 / 60.0;
    const FrameWait wait = compute_frame_wait(/*first_call=*/false, /*now=*/20.5, /*next_deadline=*/20.0);
    EXPECT_EQ(wait.wait_seconds, 0.0);
    EXPECT_EQ(wait.frame_now, 20.5);
}

TEST(LumiDessinFrameWait, ConsecutiveOnTimeFramesNeverDrift)
{
    // Simulate 120 back-to-back on-time frames at 60 fps entirely with pure
    // arithmetic (no sleep): each frame's deadline becomes the next call's
    // `now`, so the recorded elapsed time is exactly the interval every
    // time, and the accumulated deadline after N frames is exactly
    // N * interval with no floating-point creep beyond ordinary rounding.
    const double interval = 1.0 / 60.0;
    bool first_call = true;
    double next_deadline = 0.0;
    double now = 0.0;
    for (int frame = 0; frame < 120; ++frame)
    {
        const FrameWait wait = compute_frame_wait(first_call, now, next_deadline);
        EXPECT_NEAR(wait.wait_seconds, first_call ? 0.0 : interval, 1e-12);
        now = wait.frame_now;
        next_deadline = now + interval;
        first_call = false;
    }
    // The first call is free (no time to account for), so 120 calls
    // advance simulated time by 119 intervals, not 120.
    EXPECT_NEAR(now, 119.0 * interval, 1e-9);
}

TEST(LumiDessinInputNames, CanonicalKeyNamesCoverEveryDocumentedName)
{
    const auto &names = canonical_key_names();
    for (char c = 'a'; c <= 'z'; ++c)
    {
        EXPECT_TRUE(names.count(std::string(1, c))) << c;
    }
    for (char c = '0'; c <= '9'; ++c)
    {
        EXPECT_TRUE(names.count(std::string(1, c))) << c;
    }
    for (const char *name : {"espace", "entrée", "échappement", "tabulation", "retour_arrière", "supprimer",
                             "gauche", "droite", "haut", "bas", "début", "fin", "page_haut", "page_bas", "majuscule",
                             "contrôle", "option", "commande"})
    {
        EXPECT_TRUE(names.count(name)) << name;
    }
    for (int i = 1; i <= 12; ++i)
    {
        EXPECT_TRUE(names.count("f" + std::to_string(i)));
    }
    EXPECT_FALSE(names.count("inconnue"));
    // Exactly 26 + 10 + 14 + 12 + 4 = 66 canonical names -- pins the count
    // against a future entry silently added to one list but not the other.
    EXPECT_EQ(names.size(), 66u);
}

TEST(LumiDessinInputNames, CanonicalButtonNamesAreExactlyFive)
{
    const auto &names = canonical_button_names();
    for (const char *name : {"gauche", "milieu", "droite", "x1", "x2"})
    {
        EXPECT_TRUE(names.count(name)) << name;
    }
    EXPECT_FALSE(names.count("inconnu"));
    EXPECT_EQ(names.size(), 5u);
}
