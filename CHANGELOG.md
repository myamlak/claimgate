# Changelog

## 0.1.0

First release.

- `Book` registers claims and accumulates measured cells; any number of books
  coexist in one process, each with its own claims, counters and report.
- Every row is registered under a `PropertyType` - what the row is a claim
  about - which is the record of the defect class its cells can see. The set is
  `Accuracy`, `DomainOfValidity`, `Ordering`, `Continuity`, `Reproducibility`,
  `ErrorFloor` and `Unclassified`, the last for a row the consumer cannot place.
  `Book::Property` reads the type back; the report counts the rows and cells of
  each type apart from the others, so a type with no rows is visible as a defect
  class the book does not reach.
- A judged row with no measured cells reports `EvidenceAbsent`, not `Verified`:
  a claim that reads as met with nothing measured under it is the outcome the
  framework exists to catch, and a stated domain does not rescue such a row. A
  book holding one is not met.
- The counters and the verdict rule: a cell whose bound is at least the
  reference's own magnitude is counted as vacuous and kept out of the domain
  where the bound binds; a row is exceeded if any cell failed, vacuous only if
  every cell is vacuous, verified otherwise.
- The report prints the per-lane table, one verdict per judged row with its
  evidence and its falsifier, and the discriminating fraction inside the
  RESULT line: the count of met claims is qualified by the cells that could
  have failed.
- `Claim` is an opaque registration handle; `Book::Bound` reads a registered
  bound back; a cell index outside the book's range is reported, not written.
- `examples/toy_accuracy.cpp`: a complete consumer, and the same book failing
  against a bound its lane does not meet.
- Test suite covering the counter rules, the verdict boundaries, the
  discriminating fraction on a hand-computed fixture, rows of different
  property types separated by the report, an unmeasured row holding its book
  back, two books in one process, out-of-range indices, and the report's
  wording.
