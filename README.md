# claimgate

A library's documentation makes accuracy claims: *this lane is within 1e-12 of
the true value on this region*, *that one is within 1e-6 wherever the value is
larger than the bound*. `claimgate` turns each of those sentences into a row
that a program fills in, and reports which claims hold, over what domain, and
how much of the evidence could actually tell a correct implementation from a
broken one.

That last number is the point. A bound compared against a value it is larger
than passes for any implementation, correct or not, so a measurement sweep is
full of cells that cannot discriminate, and a headline that reports only
"every claim met" hides how many of them there were. This framework counts them
separately and puts the fraction inside the same line as the verdict.

A row is also registered under a **property type** - what the row is a claim
about. The type is what fixes the defect class the row can see: a cell refutes
only what the question put to it reaches. A book whose rows are all of one type
is silent about every defect of the other kinds, whatever else it proves, so the
report counts the rows and the cells of each type apart from the rest.

Standard library only, C++17, no dependencies.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`CLAIMGATE_BUILD_TESTS` and `CLAIMGATE_BUILD_EXAMPLES` are both `ON` by
default. The installed package exports `claimgate::claimgate`, so a consumer
can `find_package(claimgate)` after `cmake --install`.

## The row model

A **cell** is one measured comparison: the library's value and an independent
reference value at the same argument.

```cpp
struct Cell {
    int    index;              // the row's index variable (order, mode, tier...)
    double x;                  // the argument
    double got;                // the library's value
    double ref;                // an independent reference value
    bool   gotUnrepresentable; // the return is zero or subnormal in its own format
};
```

A **row** is one registered claim: a lane, the region of the domain the claim is
made over, the bound it states, and every cell measured against it. The
counters a row keeps are the ones the verdict needs:

| Counter | Meaning |
| --- | --- |
| `points` | cells measured |
| `vacuous` | cells where `bound >= |ref|`, so any value in range passes |
| `vacuousZero` | of those, cells whose return cannot hold the reference at all |
| `failures` | cells whose error is over the bound |
| `domainPoints` | cells where `bound < |ref|`: the bound binds, and the error is what meets it |
| `domainSubnormal`, `domainZero`, `domainOutside` | the state of the return on those cells |
| `worstRatio`, `worstErr`, `worstBound`, `worstIndex`, `worstX` | the worst cell |

Every cell is judged against the bound passed to `Measure`, which need not be
the registered one: a lane whose budget is per-region or per-value passes the
bound that applies at that cell, and reads the registered one back with
`Book::Bound` when the cell is judged against the base. `Book::Property` reads a
registered row's property type back the same way.

A row's **property type** is what it is a claim about, and it is required at
registration:

| `PropertyType` | The defect it detects, and the one it cannot |
| --- | --- |
| `Accuracy` | the error against a reference is over a stated bound; cannot see a wrong value inside the bound, or a bound met by the value's own smallness |
| `DomainOfValidity` | a row applied outside the region, lane or index set it was stated for; cannot see anything inside it |
| `Ordering` | a relation between two cells broken - a transposed argument, a wrong branch, a table applied in the wrong direction; cannot see a value wrong the same way at both cells |
| `Continuity` | a threshold that jumps, a series truncated before its remainder is small, an undocumented dispatch boundary; cannot see a value that is smooth and wrong |
| `Reproducibility` | two paths to the same value that disagree - non-determinism, a reassociation, a vector path that parts company with the scalar one; cannot see a value wrong identically on both paths |
| `ErrorFloor` | a lane that returns zero or a subnormal where the answer is representable, passing on the number format's floor; cannot see a representable value that is simply wrong |

The type is the record of which defect classes a book can reach. It does not
change a cell's arithmetic or a row's verdict: it is what the report counts
apart, so a defect class with no rows under it is visible as such rather than
implied by a green total.

Each judged row ends with one **verdict**:

| Verdict | Meaning |
| --- | --- |
| `Verified` | measured here, and met at a point that carries signal |
| `MetOverDomain` | met over the domain the claim itself states |
| `Exceeded` | measured here, and the delivered error is outside the bound |
| `Vacuous` | every counted cell is one where the bound is at least the reference's own magnitude - the pass is the number format's floor, not the library's |
| `EvidenceAbsent` | this revision cannot measure the row |

`Verified` and `MetOverDomain` are the two that count as met. A row registered
with `judged = false` is a **record**: its cells are counted, but no verdict
rests on it, so it can be over its bound and still leave the book green.

## Worked example

Save this as `claimed.cpp` beside an installed `claimgate`, or build it against
the source tree:

```sh
c++ -std=c++17 -I include claimed.cpp src/claimgate.cpp -o claimed
```

```cpp
#include "claimgate/claimgate.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace toy {

// The single-precision lane.
double ExpNegSingle(double x) {
    return static_cast<double>(std::exp(static_cast<float>(-x)));
}

// The reference. A real gate uses a value the code under test cannot have
// influenced; this one is the platform's exp.
double Reference(double x) {
    return std::exp(-x);
}

} // namespace toy

int main() {
    // The book is sized for the row's index variable, which this lane does not
    // have, so every cell is at index 0.
    claimgate::Book book(0);

    // The documented claim: within 1e-6 absolute of exp(-x) on [0, 20].
    const claimgate::Claim claim = book.AddClaim(
        claimgate::PropertyType::Accuracy, "single", "x in [0, 20]", 1e-6);

    for (int k = 0; k <= 100; ++k) {
        const double x = 0.2 * k;
        const double got = toy::ExpNegSingle(x);
        const bool unrepresentable = std::abs(got) < std::numeric_limits<float>::min();

        book.Measure(claim, {0, x, got, toy::Reference(x), unrepresentable}, book.Bound(claim));
    }

    return book.PrintReport(std::cout);
}
```

Its output, with the two fixed paragraphs of explanation elided (the legend
under the table, and the paragraph under `claims, each with one verdict:`):

```
per lane, per region: delivered error / documented bound
  lane                     region      points     real  delivered / claimed      max ratio (index, x)      vacuous no value
  ------------------------------------------------------------------------------------------------------------------------------
  single                   x in [0, 20]      101       70  2.16e-08 / 1e-06         0.0216 (index=0, x=0.6)        31        0

claims, each with one verdict:
  [... the explanation, then the row's evidence and falsifier ...]
  [verified at this revision] single / x in [0, 20] - accuracy bound
      101 comparison cells, 31 of them (30.7%) with a bound at least as large as the reference itself; worst 0.0216 of the bound at (index=0, x=0.6): delivered 2.158e-08 against 1e-06, 0 cells over the bound; the bound binds on 70 cells: 0 returned a subnormal, 0 returned zero (the largest reference a zero return discarded is 0), 0 were outside the bound
      falsified by: a measured row: the sweep would have to deliver a cell over its bound (the row prints its cells, its worst cell and its failure count, so a non-zero failure count is what to look for), or cover none of the domain it names

  cells: 101 comparison cells across every lane, region and index, of which 31 (30.7%) carry a bound at least as large as the value itself, so any return in range passes there and the cell cannot discriminate; the no-cell-over-budget statement above is carried by the remaining 70 (69.3%). This is the vacuous column aggregated, not a separate finding, and it is the number to read before reading the green rows

  RESULT: 1 of 1 claims met at this revision (1 verified outright, 0 met over a stated domain, 0 exceeded, 0 vacuous only, 0 evidence absent; neither of the last two counted as met)
          carried by the 70 of 101 comparison cells (69.3%) that can discriminate: the other 31 carry a bound at least as large as the value itself, so read the two numbers together

rows by property type: a row refutes only what its own kind of question
reaches
  type                     claims      met  not met   records       cells  cannot discriminate
  ----------------------------------------------------------------------------------------------
  accuracy bound                1        1        0         0         101  31 (30.7%)
  domain of validity            0        0        0         0           0  0 (0.0%)
  ordering                      0        0        0         0           0  0 (0.0%)
  continuity                    0        0        0         0           0  0 (0.0%)
  reproducibility               0        0        0         0           0  0 (0.0%)
  error floor                   0        0        0         0           0  0 (0.0%)
  a type with no rows is a defect class nothing in this book asks about,
  and a type whose cells cannot discriminate can only see a return out of
  range: the counts are what each kind of row saw, not what there was to see
  PASS: every documented claim met at this revision
```

That claim is met, and 31 of its 101 cells could not have failed: past
`x = 13.8` the value is below 1e-6, so the bound is larger than the value and
every cell there passes whatever the lane returns. The verdict rests on 70
cells, and the report says so in the same breath. The last block is the other
half of the same reading: this book asked one kind of question, so four of the
six defect classes are types with no rows, and nothing measured here says
anything about them.

`examples/toy_accuracy.cpp` is a longer run: four judged claims, one of them
scoped to a stated domain, a lane swept past the point where its format can hold
the value (which fills the *no value* column), an error-floor row over the
arguments where the answer is at the format's boundary, and a reading kept for
the record. Run it with `--exceeded` to see the same book fail.

## What a consumer must supply

The framework is not tied to any library: it knows about bounds, cells and
counters, and nothing about what is being measured. Pointing it at your own
library means supplying eight things.

1. **A reference the code under test cannot have influenced** - a committed
   table, a second implementation, a higher precision. Say what it is and why
   it is trustworthy; nothing here can check that for you.
2. **A sweep dense enough over the domain the claim names.** Cells the sweep
   never reaches are evidence the report cannot count. Cover the boundaries: a
   dispatch threshold is exactly where a bound stops holding.
3. **The bound, per claim**, registered with `AddClaim`, and per cell where the
   budget is not constant, passed to `Measure`.
4. **The lane and region names.** Together they are the domain the claim is
   made over, and they are what a failing row prints, so make them identify the
   claim precisely enough to act on.
5. **The property type**, at registration. It says which defect class the row's
   cells can see, and it is the only record of that: choose the type the check
   actually reaches for, not the one that sounds like the claim. A book of
   `Accuracy` rows says nothing about non-determinism however green it is, and
   the report shows that as types with no rows.
6. **The index variable** - the thing each cell is keyed by (order, mode, term
   count, tier). The `Book` is constructed with its maximum, and a cell whose
   index is outside that range is rejected instead of written past the end of
   the row's counters.
7. **`gotUnrepresentable` per cell**: whether the return is zero or subnormal
   *in its own format*. Only the consumer knows what that format is; the flag
   is what separates a pass on the arithmetic from a pass on the floor.
8. **Which rows are records, which claims the documentation scopes to a stated
   domain, and which have no evidence in this revision** - `AddClaim(..., false)`,
   `SetDomain`, and `SetEvidenceAbsent`.

A claim usually covers several rows (one per region, per lane, per tier). Group
them with `CombineVerdicts` over the rows' verdicts, which is exceeded if any
row was, verified if any was, vacuous only if any was, and evidence absent if no
row carried a cell at all.

## Reading the report

- **The cells line and the line under the RESULT line are one statement.** The
  first says how many cells carry a bound at least as large as the value, the
  second says how many are left to carry the verdict. Quote them together.
- **`vacuous` is not a failure and not a pass.** It is a cell that cannot
  discriminate. A row in which every cell is vacuous gets the `Vacuous` verdict
  and is *not* counted as met, because its pass belongs to the number format
  rather than to the library.
- **`no value` is the worst case of the same thing**: the bound was met by a
  return that cannot hold the reference at all. A lane that returns zero
  everywhere can be green; this column is how it is caught.
- **A record is not a claim.** It is a reading worth keeping, printed with
  `record, not judged` and excluded from the counts of met claims.
- **`falsified by`** says what would have to be true for a green row's check to
  fail, so a green row says which reading of it is still open.
- **The rows-by-property-type block is what the book did *not* ask.** A type
  with no rows is a defect class nothing here refutes; a type whose cells cannot
  discriminate can only see a return out of range. Read it before quoting a
  green total as coverage.

`Book::PrintReport(std::ostream&)` returns the exit status a driver should
use: 0 when every judged claim is met, 1 otherwise. `Book::BuildReport()`
returns the same numbers as a `Report` value, for a caller that wants to act on
them rather than print them.

## Layout

```
include/claimgate/claimgate.hpp   the whole interface
src/claimgate.cpp                 the book, the counters, the report
examples/toy_accuracy.cpp         a complete consumer, and its failing run
tests/claimgate_tests.cpp         the test suite
CMakeLists.txt                    build, tests, install
```

## License

BSD 3-Clause. See `LICENSE`.
