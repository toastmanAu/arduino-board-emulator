#include <gtest/gtest.h>
#include "sim_mirror.h"

#include <string>

using boardghost::mirror::authorized;
using boardghost::mirror::decide_bind;
using boardghost::mirror::token_equal;

// ---------------------------------------------------------------------------
// decide_bind — the loopback-vs-LAN gate. The security invariant of Phase 1:
// we bind 0.0.0.0 ONLY for mode "lan" WITH a token. Everything else stays on
// loopback; asking for LAN without a token must warn (and stay loopback).
// ---------------------------------------------------------------------------

TEST(MirrorBind, DefaultsToLoopbackWhenModeUnset) {
    auto d = decide_bind(nullptr, /*token_present=*/false);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, EmptyModeIsLoopback) {
    auto d = decide_bind("", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, ExplicitOffIsLoopback) {
    auto d = decide_bind("off", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, LanWithTokenBindsLan) {
    auto d = decide_bind("lan", /*token_present=*/true);
    EXPECT_TRUE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, LanWithoutTokenStaysLoopbackAndWarns) {
    auto d = decide_bind("lan", /*token_present=*/false);
    EXPECT_FALSE(d.bind_lan) << "must NOT open the LAN socket without a token";
    EXPECT_TRUE(d.warn_no_token);
}

TEST(MirrorBind, UnknownModeTreatedAsOff) {
    auto d = decide_bind("wan", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

// ---------------------------------------------------------------------------
// token_equal — constant-time compare. We can't directly assert timing in a
// unit test, but we CAN assert the functional contract: exact match only,
// length mismatch is false, empty handling is well-defined.
// ---------------------------------------------------------------------------

TEST(MirrorToken, EqualTokensMatch) {
    EXPECT_TRUE(token_equal("s3cret-abc", "s3cret-abc"));
}

TEST(MirrorToken, DifferentTokensDoNotMatch) {
    EXPECT_FALSE(token_equal("s3cret-abc", "s3cret-xyz"));
}

TEST(MirrorToken, LengthMismatchIsFalse) {
    EXPECT_FALSE(token_equal("short", "short-and-then-some"));
}

TEST(MirrorToken, SingleByteDifferenceIsFalse) {
    EXPECT_FALSE(token_equal("aaaaaaaa", "aaaaaaab"));
}

TEST(MirrorToken, BothEmptyMatch) {
    EXPECT_TRUE(token_equal("", ""));
}

// ---------------------------------------------------------------------------
// authorized — the per-request gate built on token_equal.
// No token configured → loopback dev convenience, always allowed.
// Token configured → must be presented in the X-BoardGhost-Mirror header and
// match exactly. Header-only by design: a ?token= query channel would leak the
// secret into logs/history/proxies (see sim_mirror.h).
// ---------------------------------------------------------------------------

TEST(MirrorAuth, NoTokenConfiguredAllowsEverything) {
    EXPECT_TRUE(authorized(/*configured=*/"", /*header=*/""));
    EXPECT_TRUE(authorized("", "whatever"));
}

TEST(MirrorAuth, ConfiguredTokenRequiresAMatch) {
    EXPECT_FALSE(authorized("tok", /*header=*/""));
    EXPECT_FALSE(authorized("tok", "wrong"));
}

TEST(MirrorAuth, HeaderTokenAuthorizes) {
    EXPECT_TRUE(authorized("tok", /*header=*/"tok"));
}
