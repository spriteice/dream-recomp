# ADR 17: A function's origin is the best way it was found, not the first

**Status.** Accepted 2026-11-05, for this fork (`spriteice/dream-recomp`), by the maintainer of Jet Set
Recomp. It refines ADR 8's discovery; it supersedes nothing.

**Context.** Discovery ranks the ways a function is found (ADR 8: configured seeds and call targets,
then literal-pool pointers, then the prologue sweep, then branch-derived entries), and
`analyse_function` trims a function at the next entry of at least its own rank, so a guess inside a
called function never cuts it. The rank was fixed by the first way a function was reached. The
pointer pass runs before the calls that name a function are walked, so a function it reached first
kept rank `pointer` even when a call to it came later, and any pointer into its middle (a switch
case label, or a word that merely looks like an address) cut it there. In Jet Grind Radio
0x0c063004 is a call target the pointer pass found first; a pointer entry inside it cut it from
0x0c063eb8 to 0x0c0633c0 and two switch tables went with it, and 0x0c036b6c lost one the same way.
2,882 functions in that image were reached by a pointer before their call.

**Decision.** A function's origin is the most trustworthy way it was found. `enqueue` records the
best rank queued or walked for each entry, and `drain` walks a function again when a better origin
reaches it, replacing the earlier walk's entry. Ranks only rise, so each function is walked at most
once per rank, and a function's own range no longer depends on which pass reached it first.

**Consequences.**

- Discovery does a little more work: in Jet Grind Radio 1,142 functions are walked twice, and the
  whole emit takes 5.0 s against 5.4 s before, which is noise.
- Functions already walked are not re-trimmed when a neighbour is upgraded later. A `call`-origin
  function that contains an entry upgraded afterwards keeps its range, the two overlap, and each
  emits its own copy, as overlapping functions always have (`docs/emitter-design.md`). What still
  depends on walk order is which overlaps exist, not any function's own extent.
- The report prints `functions walked again for a better origin N`.
- Test: `translator/tests/test_discover.cpp`, "a call found after the pointer pass keeps the callee
  whole": the pointer pass reaches F through a pointer into its middle first; the call that follows
  must make F whole, with origin `call`, while the pointer target stays a function of its own.
