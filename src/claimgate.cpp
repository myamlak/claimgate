#include "claimgate/claimgate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ostream>

#if defined(__GNUC__) || defined(__clang__)
#define CLAIMGATE_PRINTF_LIKE(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define CLAIMGATE_PRINTF_LIKE(format_index, first_argument)
#endif

namespace claimgate {
namespace {

// Formats one report line. The wording of every line this builds is part of
// the report's contract: a consumer reads the RESULT line and the fraction
// beside it, so both are written once, here, in the form the report prints.
// The format attribute is what keeps a later edit to those strings from
// printing a number through the wrong conversion.
CLAIMGATE_PRINTF_LIKE(1, 2) std::string Fmt(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (n < 0) {
        return std::string();
    }

    return std::string(buffer, static_cast<std::size_t>(n) < sizeof(buffer)
                                  ? static_cast<std::size_t>(n)
                                  : sizeof(buffer) - 1);
}

std::string Percent(std::size_t part, std::size_t whole) {
    if (whole == 0) {
        return "0.0";
    }

    return Fmt("%.1f", 100.0 * static_cast<double>(part) / static_cast<double>(whole));
}

// The measured evidence for one row, in the row's own counters.
std::string Evidence(const Row& a) {
    if (a.points == 0) {
        return "no cell was measured for this row, so this revision cannot check it";
    }

    std::string s = Fmt("%zu comparison cells, %zu of them (%s%%) with a bound at least as "
                        "large as the reference itself",
                        a.points,
                        a.vacuous,
                        Percent(a.vacuous, a.points).c_str());

    if (a.vacuousZero > 0) {
        s += Fmt(", %zu of those returning a value that cannot hold the reference "
                 "(the largest reference so discarded is %.4g at index=%d, x=%.6g)",
                 a.vacuousZero,
                 a.lostSignal,
                 a.lostSignalIndex,
                 a.lostSignalX);
    }

    if (a.worstIndex >= 0) {
        s += Fmt("; worst %.3g of the bound at (index=%d, x=%.6g): delivered %.4g against %.4g, "
                 "%zu cells over the bound",
                 a.worstRatio,
                 a.worstIndex,
                 a.worstX,
                 a.worstErr,
                 a.worstBound,
                 a.failures);
    }

    if (a.domainPoints > 0) {
        s += Fmt("; the bound binds on %zu cells: %zu returned a subnormal, %zu returned zero "
                 "(the largest reference a zero return discarded is %.4g), %zu were outside "
                 "the bound",
                 a.domainPoints,
                 a.domainSubnormal,
                 a.domainZero,
                 a.domainZeroRef,
                 a.domainOutside);
    }

    return s;
}

} // namespace

bool IsMet(Verdict v) noexcept {
    return v == Verdict::Verified || v == Verdict::MetOverDomain;
}

const char* VerdictName(Verdict v) noexcept {
    switch (v) {
    case Verdict::Verified:
        return "verified at this revision";
    case Verdict::MetOverDomain:
        return "met over the stated domain";
    case Verdict::Exceeded:
        return "EXCEEDED";
    case Verdict::Vacuous:
        return "vacuous only";
    case Verdict::EvidenceAbsent:
        return "evidence absent from the tree";
    }

    return "?";
}

const char* ClassFalsifier(Verdict v) noexcept {
    switch (v) {
    case Verdict::Verified:
    case Verdict::MetOverDomain:
        return "a measured row: the sweep would have to deliver a cell over its bound (the row "
               "prints its cells, its worst cell and its failure count, so a non-zero failure "
               "count is what to look for), or cover none of the domain it names";
    case Verdict::Exceeded:
        return "none needed - the claim is already exceeded, and the numbers that do it are "
               "printed here";
    case Verdict::Vacuous:
        return "this row would have to become a bound that binds: it passes only where no cell "
               "is over budget, and here every counted cell is one where the bound cannot be "
               "reached";
    case Verdict::EvidenceAbsent:
        return "the artifact this row names would have to enter the tree and be measured";
    }

    return "?";
}

const char* PropertyTypeName(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::Accuracy:
        return "accuracy bound";
    case PropertyType::DomainOfValidity:
        return "domain of validity";
    case PropertyType::Ordering:
        return "ordering";
    case PropertyType::Continuity:
        return "continuity";
    case PropertyType::Reproducibility:
        return "reproducibility";
    case PropertyType::ErrorFloor:
        return "error floor";
    case PropertyType::Unclassified:
        return "unclassified";
    }

    return "?";
}

Verdict FromAccum(const Row& row) noexcept {
    if (row.failures > 0) {
        return Verdict::Exceeded;
    }

    // No cell was measured against this row, so there is nothing for a verdict
    // to rest on. Reading it as verified would report a claim as met with no
    // measurement under it - the one outcome this framework exists to catch.
    if (row.points == 0) {
        return Verdict::EvidenceAbsent;
    }

    if (row.points == row.vacuous) {
        return Verdict::Vacuous;
    }

    return Verdict::Verified;
}

Verdict CombineVerdicts(std::initializer_list<Verdict> verdicts) noexcept {
    // An unmeasured row reaches here as EvidenceAbsent, which is neither
    // exceeded, verified nor vacuous, so the rule below already reports evidence
    // absent when that is all the rows are: the two paths agree by construction.
    bool anyExceeded = false;
    bool anyVerified = false;
    bool anyVacuous = false;

    for (const Verdict v : verdicts) {
        anyExceeded = anyExceeded || v == Verdict::Exceeded;
        anyVerified = anyVerified || v == Verdict::Verified;
        anyVacuous = anyVacuous || v == Verdict::Vacuous;
    }

    if (anyExceeded) {
        return Verdict::Exceeded;
    }

    return anyVerified ? Verdict::Verified : (anyVacuous ? Verdict::Vacuous : Verdict::EvidenceAbsent);
}

Verdict ReportedVerdict(const Row& row) noexcept {
    if (row.evidenceAbsent) {
        return Verdict::EvidenceAbsent;
    }

    const Verdict v = FromAccum(row);

    // A row with no cells is evidence absent and cannot take the domain reading:
    // a claim nobody measured is not met over any domain.
    if (v == Verdict::Verified && !row.domain.empty()) {
        return Verdict::MetOverDomain;
    }

    return v;
}

Book::Book(int maxIndex) : maxIndex_(maxIndex) {
    if (maxIndex < 0) {
        throw std::invalid_argument("claimgate::Book: maxIndex must not be negative");
    }
}

Claim Book::AddClaim(PropertyType property,
                     std::string_view lane,
                     std::string_view region,
                     double bound,
                     bool judged) {
    Row a;
    a.lane = std::string(lane);
    a.region = std::string(region);
    a.property = property;
    a.bound = bound;
    a.judged = judged;
    a.byIndex.resize(static_cast<std::size_t>(maxIndex_) + 1);
    rows_.push_back(std::move(a));

    return Claim(static_cast<int>(rows_.size()) - 1, this);
}

int Book::Slot(Claim claim) const {
    if (!claim.Valid() || claim.book_ != this) {
        throw std::out_of_range("claimgate::Book: the claim handle is not one this book issued "
                                "(it is default-constructed, or it belongs to another book)");
    }

    return claim.slot_;
}

Row& Book::At(int claim) {
    if (claim < 0 || claim >= static_cast<int>(rows_.size())) {
        throw std::out_of_range(Fmt("claimgate::Book: claim %d does not exist in this book "
                                    "(%zu claims registered)",
                                    claim,
                                    rows_.size()));
    }

    return rows_[static_cast<std::size_t>(claim)];
}

const Row& Book::At(int claim) const {
    return const_cast<Book*>(this)->At(claim);
}

double Book::Bound(Claim claim) const {
    return At(Slot(claim)).bound;
}

double Book::Bound(int claim) const {
    return At(claim).bound;
}

PropertyType Book::Property(Claim claim) const {
    return At(Slot(claim)).property;
}

PropertyType Book::Property(int claim) const {
    return At(claim).property;
}

void Book::SetDomain(Claim claim, std::string_view domain) {
    At(Slot(claim)).domain = std::string(domain);
}

void Book::SetEvidenceAbsent(Claim claim) {
    At(Slot(claim)).evidenceAbsent = true;
}

std::size_t Book::CheckedIndex(const Cell& cell) const {
    // The index selects the row's per-index counters, so an index outside the
    // range the book was built for would write outside them.
    if (cell.index < 0 || cell.index > maxIndex_) {
        throw std::out_of_range(Fmt("claimgate::Book::Measure: cell index %d is outside 0..%d "
                                    "(the book was constructed with maxIndex = %d)",
                                    cell.index,
                                    maxIndex_,
                                    maxIndex_));
    }

    return static_cast<std::size_t>(cell.index);
}

void Book::Measure(int claim, const Cell& cell, double bound) {
    Row& a = At(claim);
    const std::size_t i = CheckedIndex(cell);

    ++a.points;

    const double err = std::abs(cell.got - cell.ref);
    const bool boundAboveValue = bound >= std::abs(cell.ref);
    IndexAccum& o = a.byIndex[i];
    ++o.points;

    if (boundAboveValue) {
        ++a.vacuous;
        ++o.vacuous;

        if (cell.gotUnrepresentable) {
            ++a.vacuousZero;

            if (std::abs(cell.ref) > a.lostSignal) {
                a.lostSignal = std::abs(cell.ref);
                a.lostSignalIndex = cell.index;
                a.lostSignalX = cell.x;
            }
        }
    }

    const double ratio = err / bound;

    if (!boundAboveValue) {
        ++a.domainPoints;
        ++o.domainPoints;

        if (cell.gotUnrepresentable) {
            ++a.domainSubnormal;
        }

        if (cell.got == 0.0) {
            ++a.domainZero;

            if (std::abs(cell.ref) > a.domainZeroRef) {
                a.domainZeroRef = std::abs(cell.ref);
            }
        }

        if (ratio > 1.0) {
            ++a.domainOutside;
        }
    }

    if (ratio > o.worstRatio) {
        o.worstRatio = ratio;
        o.worstErr = err;
        o.worstBound = bound;
        o.worstX = cell.x;
    }

    if (ratio > a.worstRatio) {
        a.worstRatio = ratio;
        a.worstErr = err;
        a.worstBound = bound;
        a.worstIndex = cell.index;
        a.worstX = cell.x;
    }

    if (ratio > 1.0) {
        ++a.failures;
        ++o.failures;

        if (a.failures <= kMaxReportedExceeded) {
            ExceededCell e;
            e.sequence = exceededSequence_++;
            e.index = cell.index;
            e.x = cell.x;
            e.ref = cell.ref;
            e.err = err;
            e.bound = bound;
            e.ratio = ratio;
            a.exceeded.push_back(e);
        }
    }
}

void Book::Measure(Claim claim, const Cell& cell, double bound) {
    Measure(Slot(claim), cell, bound);
}

Report Book::BuildReport() const {
    Report r;
    r.rows = rows_;

    for (const Row& a : rows_) {
        r.cells += a.points;
        r.nonDiscriminating += a.vacuous;
    }

    r.discriminating = r.cells - r.nonDiscriminating;

    if (r.cells > 0) {
        r.discriminatingFraction =
            static_cast<double>(r.discriminating) / static_cast<double>(r.cells);
    }

    // The same counts again, keyed by the property type: the totals say how much
    // was measured, and these say what kinds of question were asked. A type with
    // no rows is a defect class nothing in this book can refute.
    for (const Row& a : rows_) {
        TypeTally& t = r.byProperty[static_cast<std::size_t>(a.property)];
        t.cells += a.points;
        t.nonDiscriminating += a.vacuous;
    }

    for (TypeTally& t : r.byProperty) {
        t.discriminating = t.cells - t.nonDiscriminating;

        if (t.cells > 0) {
            t.discriminatingFraction =
                static_cast<double>(t.discriminating) / static_cast<double>(t.cells);
        }
    }

    // A row registered as a record carries no verdict: its cells are counted
    // above, where every cell belongs, but no count of met claims moves for it.
    for (const Row& a : rows_) {
        TypeTally& t = r.byProperty[static_cast<std::size_t>(a.property)];

        if (!a.judged) {
            ++t.records;
            continue;
        }

        ++r.claims;
        ++t.claims;

        switch (ReportedVerdict(a)) {
        case Verdict::Verified:
            ++r.verified;
            ++t.met;
            break;
        case Verdict::MetOverDomain:
            ++r.metOverDomain;
            ++t.met;
            break;
        case Verdict::Exceeded:
            ++r.exceeded;
            ++t.exceeded;
            break;
        case Verdict::Vacuous:
            ++r.vacuousOnly;
            ++t.vacuousOnly;
            break;
        case Verdict::EvidenceAbsent:
            ++r.absent;
            ++t.absent;
            break;
        }
    }

    r.met = r.verified + r.metOverDomain;
    return r;
}

int Book::PrintReport(std::ostream& out) const {
    const Report rep = BuildReport();

    // ---- the cells that failed, in the order they were measured ------------
    // Bounded by kMaxReportedExceeded per row; a sweep that fails everywhere
    // would otherwise bury the report.
    struct Failure {
        const Row* row;
        const ExceededCell* cell;
    };

    std::vector<Failure> failed;

    for (const Row& a : rows_) {
        for (const ExceededCell& e : a.exceeded) {
            failed.push_back(Failure{&a, &e});
        }
    }

    std::sort(failed.begin(), failed.end(), [](const Failure& a, const Failure& b) {
        return a.cell->sequence < b.cell->sequence;
    });

    for (const Failure& f : failed) {
        out << Fmt("  EXCEEDED%s %s / %s  index=%d x=%.17g  err=%.6g  bound=%.6g  ratio=%.4g  "
                   "ref=%.6g\n",
                   f.row->judged ? " " : " (record, not judged)",
                   f.row->lane.c_str(),
                   f.row->region.c_str(),
                   f.cell->index,
                   f.cell->x,
                   f.cell->err,
                   f.cell->bound,
                   f.cell->ratio,
                   f.cell->ref);
    }

    // ---- every row and its cells -------------------------------------------
    out << "\nper lane, per region: delivered error / documented bound\n";
    out << Fmt("  %-24s %-9s %8s %8s  %-24s %-24s %8s %8s\n",
               "lane",
               "region",
               "points",
               "real",
               "delivered / claimed",
               "max ratio (index, x)",
               "vacuous",
               "no value");
    out << "  " << std::string(126, '-') << "\n";

    for (const Row& a : rows_) {
        const std::size_t real = a.points - a.vacuous;
        char location[96] = "-";
        char delivered[64] = "-";

        if (a.worstIndex >= 0) {
            std::snprintf(location,
                          sizeof(location),
                          "%.3g (index=%d, x=%.6g)",
                          a.worstRatio,
                          a.worstIndex,
                          a.worstX);
            std::snprintf(delivered, sizeof(delivered), "%.3g / %.3g", a.worstErr, a.worstBound);
        }

        // The trailing field is the one thing a summary row cannot show:
        // whether the row has a verdict at all. A row kept for the record has
        // none, and its ratio can be above 1.0 in a green book, so the row says
        // so rather than leaving it to be inferred from the RESULT line not
        // counting it.
        out << Fmt("  %-24s %-9s %8zu %8zu  %-24s %-24s %8zu %8zu%s\n",
                   a.lane.c_str(),
                   a.region.c_str(),
                   a.points,
                   real,
                   delivered,
                   location,
                   a.vacuous,
                   a.vacuousZero,
                   a.judged ? "" : "  record, not judged");
    }

    out << "\n  delivered / claimed = |got - ref| and the bound it was judged against, at\n"
           "             the worst cell of the sweep; a row whose bound is per-cell shows the\n"
           "             pair at that cell, not the registered base bound\n"
           "  vacuous  = points where the documented bound is at least as large as the\n"
           "             reference value itself, so any returned value in range passes\n"
           "  no value = of those, points where the lane returned zero or a subnormal\n";

    // ---- one verdict per judged claim --------------------------------------
    out << "\nclaims, each with one verdict:\n"
           "  a claim the document itself scopes - to a region, a set of indices, a\n"
           "  range of arguments - is met over that stated domain and prints the domain\n"
           "  with its verdict; a restriction the document states is not the same finding\n"
           "  as evidence missing from the tree, and never shares its verdict.\n"
           "  every row ends with what would have to be true for its check to fail, so a\n"
           "  green row says which reading of it is still open, not only that it passed.\n"
           "  a row registered as a record carries no verdict at all.\n";

    for (const Row& a : rows_) {
        const std::string identity =
            a.lane + " / " + a.region + " - " + PropertyTypeName(a.property);
        const char* falsifier = ClassFalsifier(ReportedVerdict(a));

        if (!a.judged) {
            out << Fmt("  [%-16s] %s\n      %s\n      %s\n",
                       "record, not judged",
                       identity.c_str(),
                       Evidence(a).c_str(),
                       "this row's cells are counted in the table above and in the cell totals, "
                       "but no verdict rests on it");
            continue;
        }

        if (a.domain.empty()) {
            out << Fmt("  [%-16s] %s\n      %s\n      falsified by: %s\n",
                       VerdictName(ReportedVerdict(a)),
                       identity.c_str(),
                       Evidence(a).c_str(),
                       falsifier);
        } else {
            out << Fmt("  [%-16s] domain: %s\n      %s\n      %s\n      falsified by: %s\n",
                       VerdictName(ReportedVerdict(a)),
                       a.domain.c_str(),
                       identity.c_str(),
                       Evidence(a).c_str(),
                       falsifier);
        }
    }

    // ---- what the verdicts rest on -----------------------------------------
    if (rep.cells > 0) {
        out << Fmt("\n  cells: %zu comparison cells across every lane, region and index, "
                   "of which %zu (%s%%) carry a bound at least as large as the value "
                   "itself, so any return in range passes there and the cell cannot "
                   "discriminate; the no-cell-over-budget statement above is carried by "
                   "the remaining %zu (%s%%). This is the vacuous column aggregated, not "
                   "a separate finding, and it is the number to read before reading the "
                   "green rows\n",
                   rep.cells,
                   rep.nonDiscriminating,
                   Percent(rep.nonDiscriminating, rep.cells).c_str(),
                   rep.discriminating,
                   Percent(rep.discriminating, rep.cells).c_str());
    }

    out << Fmt("\n  RESULT: %d of %zu claims met at this revision (%d verified outright, "
               "%d met over a stated domain, %d exceeded, %d vacuous only, %d evidence "
               "absent; neither of the last two counted as met)\n",
               rep.met,
               static_cast<std::size_t>(rep.claims),
               rep.verified,
               rep.metOverDomain,
               rep.exceeded,
               rep.vacuousOnly,
               rep.absent);

    // The count in the RESULT line is only as wide as the cells that can
    // discriminate, so the qualification prints inside the same line rather
    // than beside it: a reader who quotes the verdict gets the fraction the
    // verdict rests on, and a reader who quotes only the first line has dropped
    // something the report did not drop.
    if (rep.cells > 0) {
        out << Fmt("          carried by the %zu of %zu comparison cells (%s%%) that can "
                   "discriminate: the other %zu carry a bound at least as large as the value "
                   "itself, so read the two numbers together\n",
                   rep.discriminating,
                   rep.cells,
                   Percent(rep.discriminating, rep.cells).c_str(),
                   rep.nonDiscriminating);
    }

    // ---- what kinds of question were asked ---------------------------------
    // The totals above say how much was measured; these say what each kind of
    // row can see. The tally is keyed by the property type a row was registered
    // under, so a defect class nothing in this book asks about shows up as a
    // type with no rows rather than as a silence between the other numbers.
    out << "\nrows by property type: a row refutes only what its own kind of question\n"
           "reaches\n";
    out << Fmt("  %-22s %8s %8s %8s %9s %11s  %s\n",
               "type",
               "claims",
               "met",
               "not met",
               "records",
               "cells",
               "cannot discriminate");
    out << "  " << std::string(94, '-') << "\n";

    for (std::size_t i = 0; i < kPropertyTypeCount; ++i) {
        const TypeTally& t = rep.byProperty[i];
        const int notMet = t.exceeded + t.vacuousOnly + t.absent;

        out << Fmt("  %-22s %8d %8d %8d %9d %11zu  %zu (%s%%)\n",
                   PropertyTypeName(static_cast<PropertyType>(i)),
                   t.claims,
                   t.met,
                   notMet,
                   t.records,
                   t.cells,
                   t.nonDiscriminating,
                   Percent(t.nonDiscriminating, t.cells).c_str());
    }

    if (rep.cells > 0) {
        out << "  a type with no rows is a defect class nothing in this book asks about,\n"
               "  and a type whose cells cannot discriminate can only see a return out of\n"
               "  range: the counts are what each kind of row saw, not what there was to see\n";
    }

    // The verdict is strict by construction: a claim that is not met - whether
    // it was measured exceeded, measured vacuous only, or rests on evidence
    // this revision cannot re-run - leaves the book red and names itself below.
    if (!rep.AllMet()) {
        out << "  NOT MET at this revision:";

        for (const Row& a : rows_) {
            if (a.judged && !IsMet(ReportedVerdict(a))) {
                out << ' ' << a.lane << '/' << a.region;
            }
        }

        out << "\n  FAIL (exit status 1)\n";
        return 1;
    }

    out << "  PASS: every documented claim met at this revision\n";
    return 0;
}

} // namespace claimgate
