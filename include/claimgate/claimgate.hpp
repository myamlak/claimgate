// claimgate - turn a library's documented accuracy claims into machine-checkable
// rows.
//
// A claim is registered once, with the lane and region it is made over and the
// bound it states. Every comparison of the library's value against an
// independent reference is handed to the same book, which counts what each
// comparison could and could not decide. The report says which claims hold,
// over what domain, and what fraction of the comparison cells could tell a
// correct implementation from a broken one.
//
// Standard library only.
//
//   claimgate::Book book(32);
//   const claimgate::Claim c =
//       book.AddClaim(claimgate::PropertyType::Accuracy, "fp32", "small x", 1.5e-7);
//   book.Measure(c, {0, 0.5, got, ref, false}, book.Bound(c));
//   return book.PrintReport(std::cout);
//
// A row is registered under a property type as well as a lane, a region and a
// bound. The type is what the row claims to know about, and it decides which
// defect classes the row can see: a cell can only refute a defect that the
// question put to it reaches. A book whose rows are all of one type is silent
// about every defect of the other kinds, so the report counts the rows and the
// cells of each type rather than only their total.

#ifndef CLAIMGATE_CLAIMGATE_HPP
#define CLAIMGATE_CLAIMGATE_HPP

#include <array>
#include <cstddef>
#include <initializer_list>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace claimgate {

// How many failed cells a row prints before the row's worst-cell line is the
// only record of it; a sweep that fails everywhere would otherwise bury the
// report.
inline constexpr std::size_t kMaxReportedExceeded = 5;

// The verdict a registered row ends with:
//
//   Verified        measured here, and met at a point that carries signal;
//   MetOverDomain   met over the domain the claim itself states;
//   Exceeded        measured here, and the delivered error is outside the bound;
//   Vacuous         measured here, and every cell that meets the bound is one
//                   where the bound is at least the reference's own magnitude -
//                   the pass is the number format's floor, not the library's,
//                   so it is counted apart from the verified rows;
//   EvidenceAbsent  this revision cannot measure the row. Counted apart again,
//                   and never as a pass.
enum class Verdict { Verified, MetOverDomain, Exceeded, Vacuous, EvidenceAbsent };

// A claim the document itself scopes - to a region, a set of indices, a range
// of arguments - is met over that stated domain and is a pass. A restriction
// the document states is not the same finding as evidence missing from the
// revision, and never shares its verdict.
bool IsMet(Verdict v) noexcept;

// The verdict as the report prints it.
const char* VerdictName(Verdict v) noexcept;

// What would have to be true for the check behind a verdict to fail. A verdict
// that cannot say this is not a check, so every row prints one.
const char* ClassFalsifier(Verdict v) noexcept;

// The kind of property a row is a claim about.
//
// The type is not decoration: it fixes the defect class the row can see. A cell
// refutes only what the question put to it reaches, so a value that is wrong in
// a way no row of any registered type asks about is a value this book passes -
// and that is a property of the type, not of how many cells were measured. Each
// member below states the defect it detects and the one it cannot.
enum class PropertyType {
    // The error against an independent reference is inside a stated bound.
    // Sees a value wrong by more than the bound; blind to a wrong value that is
    // inside it, and to a bound met by the value's own smallness.
    Accuracy,
    // The arguments, lanes, indices or modes the other rows hold over: where the
    // arithmetic is claimed to be in range at all. Sees a row applied outside
    // the region it was stated for; blind to everything inside that region.
    DomainOfValidity,
    // A relation between two cells - a total order, a sign, a symmetry, a
    // bracketing. Sees a transposed argument, a wrong branch, a table applied in
    // the wrong direction; blind to a value wrong the same way at both cells.
    Ordering,
    // The size of the step between neighbouring arguments. Sees a threshold that
    // jumps, a series truncated where its remainder is not yet small, a dispatch
    // boundary the documentation does not admit; blind to a smooth wrong value.
    Continuity,
    // Two paths to the same value agree to the last bit: a lane against itself,
    // a vector path against the scalar one, one run against the next. Sees
    // non-determinism and a reassociation; blind to a value wrong identically on
    // both paths.
    Reproducibility,
    // The returned value can hold the answer at all, in the format it is
    // returned in. Sees a lane that returns zero or a subnormal and passes on
    // the number format's floor rather than on its arithmetic; blind to a
    // representable value that is simply wrong.
    ErrorFloor,
};

// The number of PropertyType members, for a tally over all of them. The members
// are dense from zero and in the order declared, so a type's value indexes an
// array of that many tallies.
inline constexpr std::size_t kPropertyTypeCount = 6;

// The property type as the report prints it.
const char* PropertyTypeName(PropertyType type) noexcept;

// One measured comparison: the library's value and an independent reference
// value at the same argument.
struct Cell {
    int    index;              // the row's index variable (order, mode, tier...)
    double x;                  // the argument
    double got;                // the library's value
    double ref;                // an independent reference value
    bool   gotUnrepresentable; // the return is zero or subnormal in its own format
};

// The same counters keyed by the row's index variable, so a worst cell can be
// reported with the index it falls at.
struct IndexAccum {
    std::size_t points = 0;
    std::size_t vacuous = 0;
    std::size_t domainPoints = 0; // the bound is tighter than the reference here
    std::size_t failures = 0;
    double worstRatio = 0.0;
    double worstErr = 0.0;
    double worstBound = 0.0;
    double worstX = 0.0;
};

// A cell whose error was outside the bound, kept so the report can print the
// numbers that did it rather than only their count.
struct ExceededCell {
    std::size_t sequence = 0; // the book's measurement order, for printing
    int    index = -1;
    double x = 0.0;
    double ref = 0.0;
    double err = 0.0;
    double bound = 0.0;
    double ratio = 0.0;
};

// One registered claim and everything measured against it.
struct Row {
    std::string  lane;            // the implementation lane or mode
    std::string  region;          // the region of the domain the bound is over
    PropertyType property = PropertyType::Accuracy; // what the row claims to know about
    double      bound = 0.0;      // the base bound the row was registered with
    bool        judged = true;    // whether any report verdict rests on it
    std::string domain;           // the stated domain, when the claim is scoped
    bool        evidenceAbsent = false; // this revision cannot measure the row

    std::size_t points = 0;
    std::size_t vacuous = 0;     // the bound alone exceeds |ref|
    std::size_t vacuousZero = 0; // ... and the return cannot hold it
    std::size_t failures = 0;
    double worstRatio = 0.0;
    double worstErr = 0.0;
    double worstBound = 0.0;
    int    worstIndex = -1;
    double worstX = 0.0;
    // The largest reference magnitude a zero or subnormal return discarded.
    double lostSignal = 0.0;
    int    lostSignalIndex = -1;
    double lostSignalX = 0.0;
    // The domain the bound binds over: the cells where |ref| exceeds the bound,
    // so no floor and no value's own smallness can meet it. Inside it the lane
    // has to return a value inside the bound; outside it the bound is met by
    // the format's floor rather than by the arithmetic, and no accuracy is
    // claimed. Counted per row, so the two sides are never confused.
    std::size_t domainPoints = 0;
    std::size_t domainSubnormal = 0; // ... and the return is subnormal
    std::size_t domainZero = 0;      // ... and the return is the format's zero
    std::size_t domainOutside = 0;   // ... and the return is outside the bound
    double domainZeroRef = 0.0;      // largest |ref| a zero return discarded

    std::vector<IndexAccum>   byIndex;
    std::vector<ExceededCell> exceeded;
};

// The verdict a measured row earns, from its counters alone.
Verdict FromAccum(const Row& row) noexcept;

// The verdict a claim covering several measured rows earns: exceeded if any row
// failed, else verified if any row was, else vacuous only if any row was, else
// evidence absent - no row carried a cell.
Verdict CombineVerdicts(std::initializer_list<Verdict> verdicts) noexcept;

// The verdict a row is reported with: FromAccum, and a row the consumer scoped
// to a stated domain reads as met over that domain rather than verified
// outright. A row marked evidence absent reports that and nothing else.
Verdict ReportedVerdict(const Row& row) noexcept;

// What the rows of one property type amount to. Counted apart from the other
// types so the report can show which kinds of question were asked at all: a
// type with no rows saw nothing, whatever the other types found.
struct TypeTally {
    int claims = 0;        // judged rows of this type
    int met = 0;
    int exceeded = 0;
    int vacuousOnly = 0;
    int absent = 0;
    int records = 0;       // rows of this type registered unjudged
    std::size_t cells = 0;             // comparison cells over every row of this type
    std::size_t nonDiscriminating = 0; // ... whose bound is at least |ref|
    std::size_t discriminating = 0;
    double discriminatingFraction = 0.0; // 0 when this type has no cells
};

// Everything the report says, without printing it.
struct Report {
    std::vector<Row> rows;
    std::size_t cells = 0;             // every comparison cell, all rows
    std::size_t nonDiscriminating = 0; // ... whose bound is at least |ref|
    std::size_t discriminating = 0;    // ... and the rest
    double discriminatingFraction = 0.0; // 0 when no cell was measured
    int claims = 0;                    // judged rows
    int met = 0;
    int verified = 0;
    int metOverDomain = 0;
    int exceeded = 0;
    int vacuousOnly = 0;
    int absent = 0;

    // The same counts per property type, indexed by the underlying value of the
    // type. A type with no rows is all zeros rather than absent.
    std::array<TypeTally, kPropertyTypeCount> byProperty{};

    // Whether every judged claim is met; an empty book is met by construction.
    bool AllMet() const noexcept { return met >= claims; }
};

// An opaque handle to a registered claim. It carries no arithmetic, no
// ordering and no conversion to an integer: the only things it can be used for
// are the Book calls that take it.
//
// This is deliberate. A registration that returns a bare index invites a
// consumer to do arithmetic on it and to assume the range is dense and
// contiguous, which is exactly the assumption that breaks when a lane is
// registered conditionally.
class Claim {
public:
    Claim() = default; // an invalid handle; Valid() is false

    bool Valid() const noexcept { return slot_ >= 0 && book_ != nullptr; }

    friend bool operator==(Claim a, Claim b) noexcept {
        return a.slot_ == b.slot_ && a.book_ == b.book_;
    }
    friend bool operator!=(Claim a, Claim b) noexcept { return !(a == b); }

private:
    friend class Book;
    Claim(int slot, const void* book) noexcept : slot_(slot), book_(book) {}

    int         slot_ = -1;
    const void* book_ = nullptr;

    // A handle carries the book that issued it, so a handle used against
    // another book is reported rather than measuring a row at the same slot.
};

// One book of claims. A book is an ordinary object: any number of them can
// coexist in one process, each with its own claims, counters and report.
class Book {
public:
    // `maxIndex` is the largest value the index variable of a cell may take;
    // every row carries counters for 0..maxIndex inclusive.
    explicit Book(int maxIndex);

    // Registers a claim and returns a handle to it. The property type is what
    // the row is a claim about and is required: it is the record of which defect
    // class this row's cells can see. `judged` false registers the row as a
    // record: its cells are counted where they belong but no verdict rests on
    // it, and it can be over its bound without failing a book.
    Claim AddClaim(PropertyType property,
                   std::string_view lane,
                   std::string_view region,
                   double bound,
                   bool judged = true);

    // The bound a registered claim carries, so a caller never has to keep its
    // own copy to judge a cell against it.
    double Bound(Claim claim) const;
    double Bound(int claim) const;

    // The property type a registered claim carries, so a caller never has to
    // keep its own copy to say what the row can see.
    PropertyType Property(Claim claim) const;
    PropertyType Property(int claim) const;

    // Declares the domain the document itself scopes this claim to. A scoped
    // row that measures clean reads as met over that domain.
    void SetDomain(Claim claim, std::string_view domain);

    // Marks the row as one this revision cannot measure; it reports evidence
    // absent and never counts as met.
    void SetEvidenceAbsent(Claim claim);

    // Records one comparison against a claim. `bound` is the bound that cell is
    // judged against, which need not be the registered base bound - a lane
    // whose budget is per-region or per-value passes the bound that applies at
    // this cell.
    //
    // Throws std::out_of_range if the claim does not exist in this book, or if
    // cell.index is outside 0..maxIndex.
    void Measure(Claim claim, const Cell& cell, double bound);
    void Measure(int claim, const Cell& cell, double bound);

    int ClaimCount() const noexcept { return static_cast<int>(rows_.size()); }
    int MaxIndex() const noexcept { return maxIndex_; }

    // The rows, in registration order.
    const std::vector<Row>& Rows() const noexcept { return rows_; }

    // Totals every row and classifies it; prints nothing.
    Report BuildReport() const;

    // Prints the report and returns the exit status a driver should use: 0 when
    // every judged claim is met, 1 otherwise.
    int PrintReport(std::ostream& out) const;

private:
    Row&       At(int claim);
    const Row& At(int claim) const;
    int         Slot(Claim claim) const;
    std::size_t CheckedIndex(const Cell& cell) const;

    int               maxIndex_;
    std::vector<Row>  rows_;
    std::size_t       exceededSequence_ = 0;
};

} // namespace claimgate

#endif // CLAIMGATE_CLAIMGATE_HPP
