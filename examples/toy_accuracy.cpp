// A complete consumer of the framework: a toy library with documented accuracy
// claims, a book that measures every one of them against an independent
// reference, and the report it prints.
//
//   cmake --build <build> --target claimgate-example
//   <build>/examples/claimgate-example
//
// Pass --exceeded to judge one lane against a bound it does not meet; that is
// what the report looks like when a documented claim does not hold, and the
// process exits 1.

#include "claimgate/claimgate.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

// ---------------------------------------------------------------------------
// The library under test: three lanes for exp(-x), and the reference.
// ---------------------------------------------------------------------------
namespace toy {

// The series for exp(-x) truncated after `terms` terms.
//
// Documented: 16 terms are within 1e-12 of exp(-x) on [0, 1], and 12 terms are
// within 1e-10 of it on [0, 0.5].
double ExpNegSeries(double x, int terms) {
    double term = 1.0;
    double sum = 1.0;

    for (int k = 1; k < terms; ++k) {
        term *= -x / static_cast<double>(k);
        sum += term;
    }

    return sum;
}

// The same value held in single precision, as the cheaper lane.
//
// Documented: within 1e-6 absolute of exp(-x) wherever exp(-x) is larger than
// that bound. Below it no accuracy is claimed, and the report counts those
// cells rather than passing them.
double ExpNegSingle(double x) {
    return static_cast<double>(static_cast<float>(std::exp(-x)));
}

// The reference every lane is measured against.
//
// A real gate uses a value the code under test cannot have influenced at all -
// a committed table, a second implementation, a higher precision. This one is
// the platform's exp, which the single-precision lane is a rounding of, so the
// two are not fully independent. Say what your reference is and why it is
// trustworthy; the framework cannot check that for you.
double Reference(double x) {
    return std::exp(-x);
}

} // namespace toy

int main(int argc, char** argv) {
    const bool exceeded = (argc > 1 && std::strcmp(argv[1], "--exceeded") == 0);

    // The index variable here is the number of series terms, so the book is
    // sized for the largest one; a cell whose index is outside that range is
    // rejected rather than written past the end of the row's counters.
    constexpr int kMaxTerms = 16;
    claimgate::Book book(kMaxTerms);

    // ---- the documented claims, one row each -------------------------------
    // Each row is registered under the kind of property it is a claim about. The
    // type is what says which defect class the row's cells can see, and the
    // report counts the rows and cells of each type apart from the others.
    using claimgate::PropertyType;

    const claimgate::Claim series16 =
        book.AddClaim(PropertyType::Accuracy, "series 16 terms", "x in [0, 1]", 1e-12);
    const claimgate::Claim series12 =
        book.AddClaim(PropertyType::Accuracy, "series 12 terms", "x in [0, 0.5]", 1e-10);
    const claimgate::Claim single =
        book.AddClaim(PropertyType::Accuracy, "single precision", "x in [0, 105]",
                      exceeded ? 1e-9 : 1e-6);

    // A claim the document itself scopes to a stated domain: below about
    // x = 13.8 the value is smaller than the bound and nothing is claimed
    // there. A row that measures clean over its stated domain is reported as
    // met over that domain rather than verified outright.
    book.SetDomain(single, "the arguments where exp(-x) is larger than the bound, x below about 13.8");

    // A different kind of question about the same lane: not whether the value is
    // accurate, but whether the format can hold the answer at all. Judged
    // against the format's smallest normal over the arguments where the answer
    // is at or below it, this row fails for a lane that returns nothing usable
    // where the answer is still representable; where the answer is below the
    // floor the error is the format's and the cell passes. Most of its cells
    // carry a bound larger than the value, so the report shows this row doing
    // real work on few of them - which is the honest size of such a check.
    const claimgate::Claim floorRow =
        book.AddClaim(PropertyType::ErrorFloor, "single precision",
                      "x in [87, 105] at the floor", std::numeric_limits<float>::min());

    // A reading kept for the record, not a claim: the series is a truncation
    // that only converges inside the region the claim above names, so measuring
    // it at large x is a lane used outside its documented region. The row is
    // registered unjudged, which means its cells are counted and its failures
    // are printed - the report says so on every line - but no verdict rests on
    // it and it cannot make the book fail. Registering it judged would turn it
    // into a claim, and it would be exceeded.
    const claimgate::Claim tiny =
        book.AddClaim(PropertyType::DomainOfValidity, "series 16 terms",
                      "x in [30, 40] outside the claimed region", 1e-12, false);

    // ---- the sweep ---------------------------------------------------------
    // Every cell: the lane's value beside the reference at the same argument,
    // judged against the bound that applies there. `smallestNormal` is the
    // format's own floor, which is what makes a return "unrepresentable" for
    // that lane.
    const auto sweep = [&book](claimgate::Claim claim,
                               int index,
                               double from,
                               double step,
                               int steps,
                               double smallestNormal,
                               double (*lane)(double)) {
        for (int k = 0; k <= steps; ++k) {
            const double x = from + step * static_cast<double>(k);
            const double got = lane(x);
            const bool unrepresentable = std::abs(got) < smallestNormal;

            book.Measure(claim, {index, x, got, toy::Reference(x), unrepresentable}, book.Bound(claim));
        }
    };

    const auto series16Lane = [](double x) { return toy::ExpNegSeries(x, 16); };
    const auto series12Lane = [](double x) { return toy::ExpNegSeries(x, 12); };

    sweep(series16, kMaxTerms, 0.0, 0.01, 100, std::numeric_limits<double>::min(), series16Lane);
    sweep(series12, 12, 0.0, 0.005, 100, std::numeric_limits<double>::min(), series12Lane);

    // The single lane is swept past the point where its format can hold the
    // value at all: from about x = 87 the returns are subnormal, and past
    // x = 104 they are zero. Those cells are counted in the "no value" column
    // rather than passed, which is what keeps a lane that returns nothing
    // usable from hiding inside a green total. The error-floor row is the same
    // lane over its last 73 arguments, where the answer is at or below the
    // format's normal range.
    sweep(single, 0, 0.0, 0.25, 420, std::numeric_limits<float>::min(), toy::ExpNegSingle);
    sweep(floorRow, 0, 87.0, 0.25, 72, std::numeric_limits<float>::min(), toy::ExpNegSingle);
    sweep(tiny, kMaxTerms, 30.0, 0.1, 100, std::numeric_limits<double>::min(), series16Lane);

    // ---- the report, and the status a driver exits with --------------------
    return book.PrintReport(std::cout);
}
