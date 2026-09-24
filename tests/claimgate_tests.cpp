// The claimgate test suite. No third-party framework: a test is a function, a
// case is one checked expression, and a failing case prints what it saw.
//
// Run:  cmake --build <build> && ctest --test-dir <build> --output-on-failure

#include "claimgate/claimgate.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

int g_tests = 0;
int g_cases = 0;
int g_failures = 0;
const char* g_current = "";

struct Test {
    const char* name;
    void (*fn)();
};

std::vector<Test>& Registry() {
    static std::vector<Test> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back(Test{name, fn}); }
};

// So a failing case that compares verdicts prints them by name.
std::ostream& operator<<(std::ostream& os, claimgate::Verdict v) {
    return os << claimgate::VerdictName(v);
}

// So a failing case that compares property types prints them by name.
std::ostream& operator<<(std::ostream& os, claimgate::PropertyType type) {
    return os << claimgate::PropertyTypeName(type);
}

void Case(bool ok, const char* expr, int line) {
    ++g_cases;

    if (!ok) {
        ++g_failures;
        std::printf("    FAIL %s line %d: %s\n", g_current, line, expr);
    }
}

template <typename A, typename B>
void CaseEq(const A& got, const B& want, const char* expr, int line) {
    ++g_cases;

    if (!(got == want)) {
        ++g_failures;
        std::ostringstream os;
        os << got;
        std::ostringstream ws;
        ws << want;
        std::printf("    FAIL %s line %d: %s\n      got  %s\n      want %s\n",
                    g_current,
                    line,
                    expr,
                    os.str().c_str(),
                    ws.str().c_str());
    }
}

claimgate::Cell Make(int index, double x, double got, double ref, bool unrepresentable = false) {
    return claimgate::Cell{index, x, got, ref, unrepresentable};
}

claimgate::Report Build(const claimgate::Book& book) {
    return book.BuildReport();
}

std::string Print(const claimgate::Book& book) {
    std::ostringstream os;
    book.PrintReport(os);
    return os.str();
}

int PrintStatus(const claimgate::Book& book) {
    std::ostringstream os;
    return book.PrintReport(os);
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t CountOf(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;

    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }

    return n;
}

bool Close(double got, double want, double tolerance = 1e-9) {
    return std::abs(got - want) <= tolerance;
}

// The whole line of `text` that contains `needle`, so a printed field can be
// checked where it is printed rather than anywhere in the report.
std::string LineWith(const std::string& text, const std::string& needle) {
    const std::size_t at = text.find(needle);

    if (at == std::string::npos) {
        return std::string();
    }

    const std::size_t start = text.rfind('\n', at) + 1;
    const std::size_t end = text.find('\n', at);
    return text.substr(start, end - start);
}

// The tally of one property type, for the many checks that read one of them.
const claimgate::TypeTally& Tally(const claimgate::Report& rep, claimgate::PropertyType type) {
    return rep.byProperty[static_cast<std::size_t>(type)];
}

} // namespace

#define TEST(name)                                         \
    static void name();                                    \
    static const Registrar registrar_##name(#name, &name); \
    static void name()

#define CHECK(cond) Case((cond), #cond, __LINE__)
#define CHECK_EQ(got, want) CaseEq((got), (want), #got " == " #want, __LINE__)

using claimgate::Book;
using claimgate::Claim;
using claimgate::Report;
using claimgate::Row;
using claimgate::Verdict;
using claimgate::PropertyType;

// ---------------------------------------------------------------------------
// The verdict a measured row earns, from its counters alone.
// ---------------------------------------------------------------------------

TEST(FromAccumFollowsFailuresThenVacuousOnlyThenVerified) {
    Row row;
    row.points = 10;
    row.vacuous = 3;
    CHECK_EQ(claimgate::FromAccum(row), Verdict::Verified);

    row.vacuous = 10;
    CHECK_EQ(claimgate::FromAccum(row), Verdict::Vacuous);

    row.vacuous = 0;
    row.failures = 1;
    CHECK_EQ(claimgate::FromAccum(row), Verdict::Exceeded);

    // A failure outranks the vacuous-only reading: a row can be over its bound
    // and still have nearly every cell unable to discriminate.
    row.vacuous = 9;
    row.failures = 1;
    CHECK_EQ(claimgate::FromAccum(row), Verdict::Exceeded);
}

TEST(AnUnmeasuredRowIsVerifiedByTheCounterRuleAndNotByEvidence) {
    // Preserved from the source rule, and the one place it is surprising: a row
    // with no cells at all satisfies the vacuous-only test's guard (points > 0)
    // and falls through to Verified. A consumer that registers a row this
    // revision cannot measure must say so with SetEvidenceAbsent; the counter
    // rule alone will not.
    Row row;
    CHECK_EQ(row.points, 0u);
    CHECK_EQ(claimgate::FromAccum(row), Verdict::Verified);

    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-9);
    CHECK_EQ(claimgate::FromAccum(book.Rows()[0]), Verdict::Verified);
    CHECK_EQ(Build(book).verified, 1);

    book.SetEvidenceAbsent(c);
    CHECK_EQ(Build(book).verified, 0);
    CHECK_EQ(Build(book).absent, 1);
    CHECK_EQ(claimgate::ReportedVerdict(book.Rows()[0]), Verdict::EvidenceAbsent);
}

TEST(CombineVerdictsFollowsTheGroupingRule) {
    using claimgate::CombineVerdicts;
    CHECK_EQ(CombineVerdicts({Verdict::Verified, Verdict::Verified}), Verdict::Verified);
    CHECK_EQ(CombineVerdicts({Verdict::Vacuous, Verdict::Verified}), Verdict::Verified);
    CHECK_EQ(CombineVerdicts({Verdict::Vacuous, Verdict::Vacuous}), Verdict::Vacuous);
    CHECK_EQ(CombineVerdicts({Verdict::Exceeded, Verdict::Verified}), Verdict::Exceeded);
    CHECK_EQ(CombineVerdicts({Verdict::EvidenceAbsent, Verdict::Exceeded}), Verdict::Exceeded);
    CHECK_EQ(CombineVerdicts({Verdict::EvidenceAbsent, Verdict::Vacuous}), Verdict::Vacuous);
    CHECK_EQ(CombineVerdicts({}), Verdict::EvidenceAbsent);
}

TEST(VerdictNamesAndFalsifiersAreReportText) {
    CHECK_EQ(std::string(claimgate::VerdictName(Verdict::Verified)), "verified at this revision");
    CHECK_EQ(std::string(claimgate::VerdictName(Verdict::MetOverDomain)),
             "met over the stated domain");
    CHECK_EQ(std::string(claimgate::VerdictName(Verdict::Exceeded)), "EXCEEDED");
    CHECK_EQ(std::string(claimgate::VerdictName(Verdict::Vacuous)), "vacuous only");
    CHECK_EQ(std::string(claimgate::VerdictName(Verdict::EvidenceAbsent)),
             "evidence absent from the tree");
    CHECK(claimgate::IsMet(Verdict::Verified));
    CHECK(claimgate::IsMet(Verdict::MetOverDomain));
    CHECK(!claimgate::IsMet(Verdict::Exceeded));
    CHECK(!claimgate::IsMet(Verdict::Vacuous));
    CHECK(!claimgate::IsMet(Verdict::EvidenceAbsent));
    CHECK(Contains(claimgate::ClassFalsifier(Verdict::Vacuous), "bound that binds"));
}

// ---------------------------------------------------------------------------
// What one measured cell counts.
// ---------------------------------------------------------------------------

TEST(TheVacuousTestIsTheBoundAgainstTheReference) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-9);
    const double bound = book.Bound(c);

    // bound >= |ref|: vacuous, and it does not enter the domain counters.
    book.Measure(c, Make(0, 1.0, 1e-12, 1e-12), bound);
    CHECK_EQ(book.Rows()[0].points, 1u);
    CHECK_EQ(book.Rows()[0].vacuous, 1u);
    CHECK_EQ(book.Rows()[0].domainPoints, 0u);

    // Equality counts as vacuous: the bound is at least as large as the value.
    book.Measure(c, Make(0, 2.0, 1e-9, 1e-9), bound);
    CHECK_EQ(book.Rows()[0].vacuous, 2u);
    CHECK_EQ(book.Rows()[0].domainPoints, 0u);

    // |ref| above the bound: the domain where the bound has to be met.
    book.Measure(c, Make(0, 3.0, 1.0, 1.0), bound);
    CHECK_EQ(book.Rows()[0].points, 3u);
    CHECK_EQ(book.Rows()[0].vacuous, 2u);
    CHECK_EQ(book.Rows()[0].domainPoints, 1u);

    // A return that cannot hold the reference is what makes a vacuous pass
    // uninformative, and it is counted only on the vacuous side.
    book.Measure(c, Make(0, 4.0, 0.0, 1e-12, true), bound);
    CHECK_EQ(book.Rows()[0].vacuous, 3u);
    CHECK_EQ(book.Rows()[0].vacuousZero, 1u);
    CHECK_EQ(book.Rows()[0].lostSignal, 1e-12);
    CHECK_EQ(book.Rows()[0].lostSignalIndex, 0);
    CHECK_EQ(book.Rows()[0].lostSignalX, 4.0);
}

TEST(TheDomainCountersCountOnlyCellsTheBoundBindsOn) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    const double bound = book.Bound(c);

    book.Measure(c, Make(0, 1.0, 1.0 + 5e-7, 1.0), bound);     // inside the bound
    book.Measure(c, Make(0, 2.0, 2.5e-308, 1.0, true), bound); // subnormal, far outside
    book.Measure(c, Make(0, 3.0, 0.0, 1.0), bound);            // zero, far outside
    book.Measure(c, Make(0, 4.0, 1.0 + 1e-3, 1.0), bound);     // outside the bound

    const Row& a = book.Rows()[0];
    CHECK_EQ(a.domainPoints, 4u);
    CHECK_EQ(a.domainSubnormal, 1u);
    CHECK_EQ(a.domainZero, 1u);
    CHECK_EQ(a.domainZeroRef, 1.0);
    CHECK_EQ(a.domainOutside, 3u);
    CHECK_EQ(a.failures, 3u);
    CHECK_EQ(claimgate::FromAccum(a), Verdict::Exceeded);
}

TEST(TheWorstCellIsTrackedPerRowAndPerIndex) {
    Book book(3);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    const double bound = book.Bound(c);

    book.Measure(c, Make(0, 1.0, 1.0, 1.0), bound);
    book.Measure(c, Make(2, 5.0, 1.0 + 5e-7, 1.0), bound);
    book.Measure(c, Make(2, 6.0, 1.0 + 9e-7, 1.0), bound);

    const Row& a = book.Rows()[0];
    CHECK_EQ(a.worstIndex, 2);
    CHECK_EQ(a.worstX, 6.0);
    CHECK(Close(a.worstRatio, 0.9, 1e-9));
    CHECK(Close(a.worstErr, 9e-7, 1e-15));
    CHECK_EQ(a.worstBound, bound);

    CHECK_EQ(a.byIndex.size(), 4u);
    CHECK_EQ(a.byIndex[0].points, 1u);
    CHECK_EQ(a.byIndex[1].points, 0u);
    CHECK_EQ(a.byIndex[2].points, 2u);
    CHECK_EQ(a.byIndex[2].worstX, 6.0);
    CHECK(Close(a.byIndex[2].worstRatio, 0.9, 1e-9));
    CHECK_EQ(a.byIndex[2].failures, 0u);
}

TEST(ACellOverItsBoundIsAFailure) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", std::ldexp(1.0, -20));
    const double bound = book.Bound(c);

    // Error exactly equal to the bound: not over it. Both the bound and the
    // error are powers of two here, so the comparison is exact.
    book.Measure(c, Make(0, 1.0, 1.0 + bound, 1.0), bound);
    CHECK_EQ(book.Rows()[0].points, 1u);
    CHECK_EQ(book.Rows()[0].domainPoints, 1u);
    CHECK_EQ(book.Rows()[0].failures, 0u);
    CHECK_EQ(book.Rows()[0].worstRatio, 1.0);

    book.Measure(c, Make(0, 2.0, 1.0 + 2.0 * bound, 1.0), bound);
    CHECK_EQ(book.Rows()[0].failures, 1u);
    CHECK_EQ(claimgate::FromAccum(book.Rows()[0]), Verdict::Exceeded);
    CHECK_EQ(PrintStatus(book), 1);
}

TEST(OnlyTheFirstFailedCellsAreKept) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);

    for (int k = 0; k < 7; ++k) {
        book.Measure(c, Make(0, static_cast<double>(k), 2.0 + k, 1.0), book.Bound(c));
    }

    const Row& a = book.Rows()[0];
    CHECK_EQ(a.failures, 7u);
    CHECK_EQ(a.exceeded.size(), claimgate::kMaxReportedExceeded);
    CHECK_EQ(claimgate::kMaxReportedExceeded, 5u);
    CHECK_EQ(a.exceeded[0].x, 0.0);
    CHECK_EQ(a.exceeded[4].x, 4.0);
    CHECK_EQ(a.exceeded[0].sequence, 0u);
    CHECK_EQ(a.exceeded[4].sequence, 4u);
    CHECK_EQ(a.exceeded[0].ratio, 1.0 / 1e-6);

    const std::string text = Print(book);
    CHECK_EQ(CountOf(text, "EXCEEDED  lane / region"), 5u);
    CHECK(Contains(text, "ref=1"));
}

TEST(PerCellBoundsAreJudgedAgainstTheBoundPassedIn) {
    // The registered bound is a base; a lane whose budget is per-cell judges
    // each cell against the bound that applies there.
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-3);

    book.Measure(c, Make(0, 1.0, 1.0 + 1e-8, 1.0), 1e-6);
    CHECK_EQ(book.Rows()[0].vacuous, 0u);
    CHECK_EQ(book.Rows()[0].failures, 0u);

    book.Measure(c, Make(0, 2.0, 1.0 + 1e-5, 1.0), 1e-6);
    CHECK_EQ(book.Rows()[0].failures, 1u);
    CHECK_EQ(book.Rows()[0].worstBound, 1e-6);
    CHECK_EQ(book.Bound(c), 1e-3);
}

TEST(AZeroBoundIsNotGuarded) {
    // Preserved: the counter rule divides by the bound with no guard. A zero
    // bound with an error above it is an immediate failure, and a zero bound
    // with an exact hit gives a NaN ratio, which fails nothing.
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 0.0);

    book.Measure(c, Make(0, 1.0, 0.0, 0.0), 0.0);
    CHECK_EQ(book.Rows()[0].vacuous, 1u);
    CHECK_EQ(book.Rows()[0].failures, 0u);

    book.Measure(c, Make(0, 2.0, 2.0, 1.0), 0.0);
    CHECK_EQ(book.Rows()[0].domainPoints, 1u);
    CHECK_EQ(book.Rows()[0].domainOutside, 1u);
    CHECK_EQ(book.Rows()[0].failures, 1u);
}

// ---------------------------------------------------------------------------
// The book as an object: handles, bounds, several books in one process.
// ---------------------------------------------------------------------------

TEST(TwoBooksInOneProcessDoNotInterfere) {
    Book first(2);
    Book second(5);

    const Claim a = first.AddClaim(PropertyType::Accuracy, "first", "region", 1e-6);
    const Claim b = second.AddClaim(PropertyType::Reproducibility, "second", "region", 1e-3);
    CHECK_EQ(first.ClaimCount(), 1);
    CHECK_EQ(second.ClaimCount(), 1);

    // The property type is per claim, and it is readable back off the book that
    // issued it rather than off a store the caller reaches into.
    CHECK_EQ(first.Property(a), PropertyType::Accuracy);
    CHECK_EQ(first.Property(0), PropertyType::Accuracy);
    CHECK_EQ(second.Property(b), PropertyType::Reproducibility);
    CHECK_EQ(first.Rows()[0].property, PropertyType::Accuracy);
    CHECK_EQ(second.Rows()[0].property, PropertyType::Reproducibility);

    first.Measure(a, Make(1, 1.0, 1.0 + 1e-7, 1.0), first.Bound(a));
    first.Measure(a, Make(2, 2.0, 1.0 + 1e-9, 1.0), first.Bound(a));

    second.Measure(b, Make(0, 1.0, 1e-12, 1e-12), second.Bound(b));
    second.Measure(b, Make(4, 2.0, 1.0, 1.0), second.Bound(b));
    second.Measure(b, Make(4, 3.0, 1.0 + 1e-2, 1.0), second.Bound(b));

    CHECK_EQ(first.Rows()[0].points, 2u);
    CHECK_EQ(first.Rows()[0].failures, 0u);
    CHECK_EQ(first.Rows()[0].byIndex.size(), 3u);
    CHECK_EQ(second.Rows()[0].points, 3u);
    CHECK_EQ(second.Rows()[0].vacuous, 1u);
    CHECK_EQ(second.Rows()[0].failures, 1u);
    CHECK_EQ(second.Rows()[0].byIndex.size(), 6u);

    CHECK_EQ(Build(first).cells, 2u);
    CHECK_EQ(Build(second).cells, 3u);
    CHECK_EQ(Build(first).claims, 1);
    CHECK_EQ(PrintStatus(first), 0);
    CHECK_EQ(PrintStatus(second), 1);

    {
        // A third book, registered and destroyed in between, moves neither.
        Book third(0);
        const Claim c = third.AddClaim(PropertyType::Accuracy, "third", "region", 1e-9);
        third.Measure(c, Make(0, 1.0, 1.0, 1.0), third.Bound(c));
        CHECK_EQ(Build(third).cells, 1u);
    }

    CHECK_EQ(Build(first).cells, 2u);
    CHECK_EQ(Build(second).cells, 3u);
    CHECK_EQ(first.Rows()[0].lane, "first");
    CHECK_EQ(second.Rows()[0].lane, "second");
    CHECK_EQ(first.Rows()[0].byIndex.size(), 3u);
    CHECK_EQ(second.Rows()[0].byIndex.size(), 6u);
}

TEST(AnIndexOutsideTheBooksRangeIsReported) {
    Book book(3);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    CHECK_EQ(book.MaxIndex(), 3);

    bool threw = false;

    try {
        book.Measure(c, Make(4, 1.0, 1.0, 1.0), book.Bound(c));
    } catch (const std::out_of_range& e) {
        threw = true;
        CHECK(Contains(e.what(), "index 4"));
        CHECK(Contains(e.what(), "0..3"));
    }

    CHECK(threw);
    CHECK_EQ(book.Rows()[0].points, 0u); // nothing was counted before the throw

    threw = false;

    try {
        book.Measure(c, Make(-1, 1.0, 1.0, 1.0), book.Bound(c));
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
    CHECK_EQ(book.Rows()[0].points, 0u);
}

TEST(AClaimThatDoesNotExistIsReported) {
    Book book(0);
    book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    bool threw = false;

    try {
        book.Measure(7, Make(0, 1.0, 1.0, 1.0), 1e-6);
    } catch (const std::out_of_range& e) {
        threw = true;
        CHECK(Contains(e.what(), "claim 7"));
    }

    CHECK(threw);

    threw = false;

    try {
        book.Bound(4);
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
}

TEST(RegistrationReturnsAnOpaqueHandle) {
    // The registration result carries no arithmetic, no ordering and no
    // conversion to an integer: a consumer can neither use it as a slot number
    // nor assume a registered range is dense and contiguous.
    static_assert(!std::is_convertible_v<Claim, int>);
    static_assert(!std::is_constructible_v<int, Claim>);
    static_assert(!std::is_arithmetic_v<Claim>);
    static_assert(!std::is_convertible_v<Claim, bool>);
    static_assert(std::is_default_constructible_v<Claim>);
    static_assert(std::is_copy_constructible_v<Claim>);

    Book book(0);
    const Claim a = book.AddClaim(PropertyType::Accuracy, "first", "region", 1e-6);
    const Claim b = book.AddClaim(PropertyType::Accuracy, "second", "region", 1e-6);
    const Claim copy = a;
    const Claim none;

    CHECK(a.Valid());
    CHECK(b.Valid());
    CHECK(!none.Valid());
    CHECK(a == copy);
    CHECK(a != b);
    CHECK(a != none);

    bool threw = false;

    try {
        book.Measure(none, Make(0, 1.0, 1.0, 1.0), 1e-6);
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);

    // A handle carries the book that issued it: using one against another book
    // is reported rather than measuring whatever row sits at the same slot.
    Book other(0);
    other.AddClaim(PropertyType::DomainOfValidity, "other", "region", 1e-6);
    threw = false;

    try {
        other.Measure(a, Make(0, 1.0, 1.0, 1.0), 1e-6);
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
    CHECK_EQ(other.Rows()[0].points, 0u);
    CHECK_EQ(book.Rows()[0].points, 0u);

    // The same rule for reading a row's property type back: `other` has a row at
    // slot 0, and it is not the row this handle refers to.
    threw = false;

    try {
        other.Property(a);
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
    CHECK_EQ(other.Property(0), PropertyType::DomainOfValidity);

    threw = false;

    try {
        book.Property(none);
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
}

TEST(ARegisteredBoundCanBeReadBack) {
    Book book(0);
    const Claim tight = book.AddClaim(PropertyType::Accuracy, "lane", "tight", 1e-9);
    const Claim loose = book.AddClaim(PropertyType::Accuracy, "lane", "loose", 2.5e-7, false);

    CHECK_EQ(book.Bound(tight), 1e-9);
    CHECK_EQ(book.Bound(loose), 2.5e-7);
    CHECK_EQ(book.Bound(0), 1e-9);
    CHECK_EQ(book.Bound(1), 2.5e-7);

    book.Measure(tight, Make(0, 1.0, 1.0 + 1e-11, 1.0), book.Bound(tight));
    CHECK_EQ(book.Rows()[0].bound, 1e-9);
    CHECK_EQ(book.Rows()[0].failures, 0u);

    bool threw = false;

    try {
        book.Bound(Claim());
    } catch (const std::out_of_range&) {
        threw = true;
    }

    CHECK(threw);
}

TEST(ABookNeedsANonNegativeMaxIndex) {
    bool threw = false;

    try {
        const Book negative(-1);
        (void)negative;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);

    const Book zero(0);
    CHECK_EQ(zero.MaxIndex(), 0);
    CHECK_EQ(zero.ClaimCount(), 0);
    CHECK(Contains(Print(zero), "PASS: every documented claim met at this revision"));
}

// ---------------------------------------------------------------------------
// The discriminating fraction, on a fixture computed by hand.
// ---------------------------------------------------------------------------

TEST(TheDiscriminatingFractionIsTheCellsThatCanDiscriminate) {
    // Ten cells, each of the three rows judged against a bound of 1e-6. The
    // first row's four references are all above the bound, the second row's are
    // one above and two below, and the third row's are two above and one below:
    // three of the ten cells have a bound at least as large as the reference,
    // so seven can tell a correct implementation from a broken one.
    Book book(1);
    const Claim a = book.AddClaim(PropertyType::Accuracy, "toy", "wide", 1e-6);
    const Claim b = book.AddClaim(PropertyType::Accuracy, "toy", "mixed", 1e-6);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "toy", "narrowing", 1e-6);

    for (const double ref : {1.0, 0.5, 0.25, 0.125}) {
        book.Measure(a, Make(0, 1.0, ref, ref), book.Bound(a));
    }

    for (const double ref : {1.0, 1e-8, 1e-9}) {
        book.Measure(b, Make(0, 30.0, ref, ref), book.Bound(b));
    }

    for (const double ref : {1.0, 0.5, 1e-9}) {
        book.Measure(c, Make(0, 40.0, ref, ref), book.Bound(c));
    }

    const Report rep = Build(book);
    CHECK_EQ(rep.cells, 10u);
    CHECK_EQ(rep.nonDiscriminating, 3u);
    CHECK_EQ(rep.discriminating, 7u);
    CHECK(Close(rep.discriminatingFraction, 0.7));
    CHECK_EQ(rep.claims, 3);
    CHECK_EQ(rep.met, 3);
    CHECK(rep.AllMet());

    const std::string text = Print(book);
    CHECK(Contains(text, "of which 3 (30.0%) carry a bound at least as large as the value"));
    CHECK(Contains(text, "carried by the 7 of 10 comparison cells (70.0%) that can discriminate"));
    CHECK(Contains(text, "the other 3 carry a bound at least as large as the value itself"));
    CHECK(Contains(text, "RESULT: 3 of 3 claims met at this revision (3 verified outright, "
                         "0 met over a stated domain, 0 exceeded, 0 vacuous only, 0 evidence "
                         "absent; neither of the last two counted as met)"));
}

TEST(TheTotalsCountEveryRowIncludingRecords) {
    // The fraction is the vacuous column aggregated over every row, judged or
    // not, and a record row's cells are part of it.
    Book book(0);
    const Claim judged = book.AddClaim(PropertyType::Accuracy, "judged", "region", 1e-6);
    const Claim record = book.AddClaim(PropertyType::Accuracy, "record", "region", 0.5, false);
    const Claim vacuous = book.AddClaim(PropertyType::Accuracy, "vacuous", "region", 1e-3);

    book.Measure(judged, Make(0, 1.0, 1.0, 1.0), book.Bound(judged));
    book.Measure(record, Make(0, 1.0, 1e-9, 1e-9), book.Bound(record));
    book.Measure(vacuous, Make(0, 30.0, 1e-12, 1e-12), book.Bound(vacuous));

    const Report rep = Build(book);
    CHECK_EQ(rep.cells, 3u);
    CHECK_EQ(rep.nonDiscriminating, 2u);
    CHECK_EQ(rep.discriminating, 1u);
    CHECK(Close(rep.discriminatingFraction, 1.0 / 3.0));
    CHECK_EQ(rep.claims, 2);      // the record carries no verdict
    CHECK_EQ(rep.met, 1);         // the all-vacuous row is not a met claim
    CHECK_EQ(rep.vacuousOnly, 1);
    CHECK(!rep.AllMet());

    const std::string text = Print(book);
    CHECK(Contains(text, "record, not judged"));
    CHECK(Contains(text, "RESULT: 1 of 2 claims met at this revision"));
}

TEST(ABookWithNoCellsPrintsNoFraction) {
    Book book(0);
    book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);

    const Report rep = Build(book);
    CHECK_EQ(rep.cells, 0u);
    CHECK_EQ(rep.discriminating, 0u);
    CHECK_EQ(rep.discriminatingFraction, 0.0);

    const std::string text = Print(book);
    CHECK(!Contains(text, "carried by the"));
    CHECK(Contains(text, "RESULT: 1 of 1 claims met at this revision"));
}

// ---------------------------------------------------------------------------
// The report.
// ---------------------------------------------------------------------------

TEST(AnAllVacuousRowIsNotMet) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-3);

    book.Measure(c, Make(0, 1.0, 1e-9, 1e-9), book.Bound(c));
    book.Measure(c, Make(0, 2.0, 0.0, 1e-9, true), book.Bound(c));

    const Report rep = Build(book);
    CHECK_EQ(rep.vacuousOnly, 1);
    CHECK_EQ(rep.met, 0);
    CHECK(!rep.AllMet());
    CHECK_EQ(PrintStatus(book), 1);

    const std::string text = Print(book);
    CHECK(Contains(text, "[vacuous only    ]"));
    CHECK(Contains(text, "NOT MET at this revision: lane/region"));
    CHECK(Contains(text, "FAIL (exit status 1)"));
}

TEST(ADomainScopedRowIsMetOverItsDomain) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    book.SetDomain(c, "the arguments where the reference exceeds its bound");
    book.Measure(c, Make(0, 1.0, 1.0, 1.0), book.Bound(c));

    const Report rep = Build(book);
    CHECK_EQ(rep.verified, 0);
    CHECK_EQ(rep.metOverDomain, 1);
    CHECK_EQ(rep.met, 1);
    CHECK(rep.AllMet());
    CHECK_EQ(claimgate::ReportedVerdict(book.Rows()[0]), Verdict::MetOverDomain);

    const std::string text = Print(book);
    CHECK(Contains(text, "[met over the stated domain] domain: the arguments where the "
                         "reference exceeds its bound"));
    CHECK(Contains(text, "1 met over a stated domain"));
    CHECK_EQ(PrintStatus(book), 0);
}

TEST(AnExceededRowNamesItselfAndFails) {
    Book book(0);
    const Claim c = book.AddClaim(PropertyType::Accuracy, "lane", "region", 1e-6);
    book.Measure(c, Make(0, 1.0, 1.0 + 1e-3, 1.0), book.Bound(c));

    const std::string text = Print(book);
    CHECK(Contains(text, "EXCEEDED  lane / region  index=0"));
    CHECK(Contains(text, "[EXCEEDED        ]"));
    CHECK(Contains(text, "NOT MET at this revision: lane/region"));
    CHECK(Contains(text, "1 exceeded"));
    CHECK_EQ(PrintStatus(book), 1);
}

TEST(ARecordCanBeOverItsBoundAndTheBookStillPasses) {
    Book book(0);
    const Claim judged = book.AddClaim(PropertyType::Accuracy, "good", "region", 1e-6);
    const Claim record = book.AddClaim(PropertyType::Accuracy, "withdrawn", "region", 1e-6, false);

    book.Measure(judged, Make(0, 1.0, 1.0, 1.0), book.Bound(judged));
    book.Measure(record, Make(0, 1.0, 1.0 + 1e-3, 1.0), book.Bound(record));

    const Report rep = Build(book);
    CHECK_EQ(rep.claims, 1);
    CHECK_EQ(rep.exceeded, 0);
    CHECK_EQ(rep.met, 1);
    CHECK(rep.AllMet());
    CHECK_EQ(PrintStatus(book), 0);

    const std::string text = Print(book);
    CHECK(Contains(text, "EXCEEDED (record, not judged) withdrawn / region"));
    CHECK(Contains(text, "record, not judged"));
    CHECK(Contains(text, "PASS: every documented claim met at this revision"));
}

TEST(AnEvidenceAbsentRowIsNamedAndNotCounted) {
    Book book(0);
    const Claim here = book.AddClaim(PropertyType::Accuracy, "here", "region", 1e-6);
    const Claim gone = book.AddClaim(PropertyType::Accuracy, "gone", "region", 1e-6);

    book.Measure(here, Make(0, 1.0, 1.0, 1.0), book.Bound(here));
    book.SetEvidenceAbsent(gone);

    const Report rep = Build(book);
    CHECK_EQ(rep.claims, 2);
    CHECK_EQ(rep.met, 1);
    CHECK_EQ(rep.absent, 1);
    CHECK(!rep.AllMet());
    CHECK_EQ(PrintStatus(book), 1);

    const std::string text = Print(book);
    CHECK(Contains(text, "[evidence absent from the tree] gone / region"));
    CHECK(Contains(text, "no cell was measured for this row"));
    CHECK(Contains(text, "NOT MET at this revision: gone/region"));
    CHECK(Contains(text, "1 evidence absent"));
}

TEST(TheReportSeparatesRowsByPropertyType) {
    // Four cells over three rows of three different property types, so the type
    // and the verdict cannot be read off one another:
    //
    //   Accuracy   judged, two cells both with a reference above the bound, both
    //              clean                                   -> verified
    //   Ordering   judged, one cell with a reference above the bound, the lane
    //              returning zero against 1.0             -> exceeded
    //   ErrorFloor unjudged, one cell whose bound is 1e-30 against a reference of
    //              1e-32, so the cell cannot discriminate  -> a record
    //
    // and three of the six types have no rows at all.
    Book book(1);
    const Claim acc = book.AddClaim(PropertyType::Accuracy, "fp64", "region", 1e-6);
    const Claim ord = book.AddClaim(PropertyType::Ordering, "fp64", "region", 1e-6);
    const Claim floor = book.AddClaim(PropertyType::ErrorFloor, "fp32", "region", 1e-30, false);

    book.Measure(acc, Make(0, 1.0, 1.0, 1.0), book.Bound(acc));
    book.Measure(acc, Make(1, 2.0, 1.0, 1.0), book.Bound(acc));
    book.Measure(ord, Make(0, 3.0, 0.0, 1.0), book.Bound(ord));
    book.Measure(floor, Make(1, 4.0, 0.0, 1e-32, true), book.Bound(floor));

    const Report rep = Build(book);
    CHECK_EQ(rep.cells, 4u);
    CHECK_EQ(rep.nonDiscriminating, 1u);
    CHECK_EQ(rep.discriminating, 3u);
    CHECK_EQ(rep.claims, 2);
    CHECK_EQ(rep.met, 1);
    CHECK_EQ(rep.exceeded, 1);

    // Accuracy: two cells, both able to discriminate, its one claim met.
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).claims, 1);
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).met, 1);
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).exceeded, 0);
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).cells, 2u);
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).nonDiscriminating, 0u);
    CHECK_EQ(Tally(rep, PropertyType::Accuracy).discriminating, 2u);
    CHECK(Close(Tally(rep, PropertyType::Accuracy).discriminatingFraction, 1.0));

    // Ordering: one cell, and the row is over its bound.
    CHECK_EQ(Tally(rep, PropertyType::Ordering).claims, 1);
    CHECK_EQ(Tally(rep, PropertyType::Ordering).met, 0);
    CHECK_EQ(Tally(rep, PropertyType::Ordering).exceeded, 1);
    CHECK_EQ(Tally(rep, PropertyType::Ordering).cells, 1u);

    // Error floor: no claim at all, one record, and its single cell is one where
    // the bound is the larger number, so the type saw nothing at this revision.
    CHECK_EQ(Tally(rep, PropertyType::ErrorFloor).claims, 0);
    CHECK_EQ(Tally(rep, PropertyType::ErrorFloor).records, 1);
    CHECK_EQ(Tally(rep, PropertyType::ErrorFloor).cells, 1u);
    CHECK_EQ(Tally(rep, PropertyType::ErrorFloor).nonDiscriminating, 1u);
    CHECK_EQ(Tally(rep, PropertyType::ErrorFloor).discriminating, 0u);
    CHECK(Close(Tally(rep, PropertyType::ErrorFloor).discriminatingFraction, 0.0));

    // A type no row was registered under is a zero row, not an absent one: the
    // three types nothing was asked about are the ones this book cannot see.
    for (const PropertyType type : {PropertyType::DomainOfValidity,
                                    PropertyType::Continuity,
                                    PropertyType::Reproducibility}) {
        CHECK_EQ(Tally(rep, type).claims, 0);
        CHECK_EQ(Tally(rep, type).records, 0);
        CHECK_EQ(Tally(rep, type).cells, 0u);
        CHECK(Close(Tally(rep, type).discriminatingFraction, 0.0));
    }

    const std::string text = Print(book);
    CHECK(Contains(text, "rows by property type"));
    CHECK(Contains(text, "a type with no rows is a defect class nothing in this book asks "
                         "about"));

    // A row says its type where it says its verdict and its lane, so the two are
    // read together.
    CHECK(Contains(text, "fp64 / region - accuracy bound"));
    CHECK(Contains(text, "fp64 / region - ordering"));
    CHECK(Contains(text, "fp32 / region - error floor"));

    // All six types print, whether or not a row was registered under them.
    for (const PropertyType type : {PropertyType::Accuracy,
                                    PropertyType::DomainOfValidity,
                                    PropertyType::Ordering,
                                    PropertyType::Continuity,
                                    PropertyType::Reproducibility,
                                    PropertyType::ErrorFloor}) {
        CHECK(Contains(text, std::string("\n  ") + claimgate::PropertyTypeName(type)));
    }

    // The counts the type controls are in that type's own line: the error floor's
    // single cell cannot discriminate, the accuracy row's two can.
    CHECK(Contains(LineWith(text, "\n  error floor"), "1 (100.0%)"));
    CHECK(Contains(LineWith(text, "\n  accuracy bound"), "0 (0.0%)"));

    // The book is not met, and the row that fails it is the ordering row.
    CHECK(!rep.AllMet());
    CHECK_EQ(PrintStatus(book), 1);
    CHECK(Contains(text, "NOT MET at this revision: fp64/region"));
}

TEST(RowsPrintInRegistrationOrder) {
    Book book(0);
    book.AddClaim(PropertyType::Accuracy, "one", "region", 1e-6);
    book.AddClaim(PropertyType::Accuracy, "two", "region", 1e-6);
    book.AddClaim(PropertyType::Accuracy, "three", "region", 1e-6);

    const std::string text = Print(book);
    const std::size_t first = text.find("one / region");
    const std::size_t second = text.find("two / region");
    const std::size_t third = text.find("three / region");
    CHECK(first != std::string::npos);
    CHECK(first < second);
    CHECK(second < third);
    CHECK(Contains(text, "PASS: every documented claim met at this revision"));
}

// ---------------------------------------------------------------------------

int main() {
    for (const Test& t : Registry()) {
        g_current = t.name;
        const int before = g_failures;
        ++g_tests;
        t.fn();
        std::printf("  %-58s %s\n", t.name, g_failures == before ? "ok" : "FAIL");
    }

    std::printf("\n%d tests, %d cases, %d failures\n", g_tests, g_cases, g_failures);
    return g_failures == 0 ? 0 : 1;
}
