# Stage 02 - XTF Compatibility Expansion

This report defines the next major priority after foundation hardening:

- expand XTF support from a solid subset into broad real-world compatibility
- handle more vendor and recorder variants safely
- move toward "bring us the XTF and it usually works or fails clearly" behavior

This stage is high priority because it directly affects product credibility.

If the software cannot open a wide range of XTF files reliably, everything built on top of it becomes less valuable no matter how good the UI or workflow is.

## Why This Is Stage 02

This should come before import-policy refinement.

Reason:

- broad XTF compatibility is a core product capability
- import-once policy is important, but it is downstream of successfully understanding the source data in the first place
- if the parser only handles a narrow slice of XTF, workflow polish will not close the most important capability gap

## Current State

The current XTF reader is not weak. It already handles an important subset well:

- sidescan pings
- sub-bottom traces
- magnetometer samples
- common packet-level navigation fallback
- inferred sample width for imperfect headers
- split-packet and ambiguous port/starboard cases
- projected-vs-geographic heuristics for common navigation cases

That is good progress.

But it is still a targeted XTF parser, not an "all major XTF flavors" parser.

## Entry Criteria

Stage 02 should begin only after the Stage 01 foundation is stable enough that compatibility work is not building on moving contracts.

In practice that means:

- artifact-store/index consistency is already defined
- metadata and coordinate propagation rules are already defined
- there is at least a minimal automated test harness for parser fixtures

## What Is Already Covered

Based on the current reader implementation, the strongest supported area is:

- sidescan-centric XTF with common ping layouts

Capabilities already present:

- `PACKET_PING` indexing and decode
- `PACKET_NAV` backfill support
- sub-bottom classification from channel type
- sidescan port/starboard classification from file header and packet sub-channel
- 8/16/32-bit sample handling with geometry-based fallback
- magnetometer extraction from packet header fields

This means the software is already useful for the XTF families closest to your current work.

## What Is Not Broadly Handled Yet

These are the main reasons it is not "all XTF formats" yet.

### 1. Packet coverage is narrow

The reader really only acts on:

- `PACKET_PING`
- `PACKET_NAV`

Other packet types may be declared, but they are not treated as first-class parse paths yet.

Examples of missing or weak areas:

- attitude packets
- notes/events
- vendor-specific auxiliary packet layouts
- optional packet extensions

### 2. Bathymetry / multibeam-style XTF is not truly implemented

The header reads bathymetry channel counts, but the channel classifier only accepts:

- sub-bottom
- port sidescan
- starboard sidescan

So "XTF support" currently does not mean general bathymetry-capable XTF support.

### 3. Variant coverage depends on heuristics more than fixtures

There is a lot of careful fallback logic, which is good.

But broad compatibility needs:

- a representative corpus of real XTF files
- fixture tests per vendor/variant
- expected-output baselines

Without that, compatibility is still largely inferred rather than proven.

### 4. Metadata coverage is still too thin

Even where payload decode works, broader compatibility also depends on reliably capturing:

- survey metadata
- vessel metadata
- time span
- navigation semantics
- channel meaning
- coordinate reference interpretation

Those contracts still need strengthening.

## Desired Outcome

After Stage 02, the product should be able to say something much stronger:

- common sidescan XTF works
- common sub-bottom XTF works
- common magnetometer-in-XTF works
- a broader range of vendor and recorder variants works
- unsupported variants fail clearly instead of silently mis-parsing

The practical goal is not "literally every XTF file ever produced".

The goal is:

- strong coverage of the formats users actually bring
- explicit detection and graceful failure for the rest

That is what gets you toward a SonarWiz-level compatibility reputation.

## Scope For This Stage

This stage should focus on compatibility breadth, not UI polish.

Priority areas:

- broader packet and channel coverage
- broader vendor-variant handling
- more reliable coordinate and navigation interpretation
- clearer unsupported-format detection
- strong fixture-based verification

## Recommended Workstreams

## 1. Define an explicit XTF compatibility matrix

Create a compatibility document or fixture inventory with rows like:

- vendor / recorder family
- sidescan layout style
- sub-bottom layout style
- bathymetry presence
- sample width mode
- nav units behavior
- split-packet vs dual-channel style
- known quirks
- supported / partial / unsupported

Without this matrix, "support all XTF" stays too vague to execute.

## 2. Expand packet-type handling deliberately

Review and decide what to do with additional packet types, especially:

- attitude
- notes/events
- auxiliary/nav-related variants
- any packet families needed by real customer files

Not every packet type must become a user-facing artifact.
But the parser should at least:

- understand whether the packet matters
- ignore it safely if irrelevant
- use it if it improves navigation or interpretation

## 3. Expand channel classification beyond the current subset

The current classifier is sidescan/sub-bottom-centric.

That needs to grow into a more complete channel interpretation layer that can distinguish:

- sidescan
- sub-bottom
- bathymetry / multibeam-like content
- unsupported but recognized channel types

Even when the product does not yet render every modality, the parser should classify them honestly.

## 4. Add unsupported-format detection instead of silent fallback

Broad compatibility is not only about decoding more.

It is also about failing well.

If a file cannot be parsed confidently, the system should report something like:

- `Unsupported XTF bathymetry variant`
- `Unsupported packet extension layout`
- `Unknown XTF channel mapping`

That is much better than:

- treating unknown data as sidescan
- producing believable but wrong output

## 5. Build a real XTF fixture corpus

This is the most important execution piece.

Collect representative XTF files or reduced fixtures for:

- standard dual-channel sidescan
- split-packet sidescan
- sub-bottom
- magnetometer-carrying packets
- projected navigation
- zero-nav with nav backfill
- missing / wrong `BytesPerSample`
- unusual channel metadata
- truncated records
- oversize/corrupt records
- bathymetry-containing XTF
- vendor-specific variants you care about most

For each fixture, define:

- expected artifact counts
- expected modality mix
- expected coordinate interpretation
- expected failure mode if unsupported

## 6. Separate "recognized" from "fully supported"

A useful compatibility model has at least three states:

- supported
- recognized but partial
- unsupported

That lets the software:

- ingest what it fully understands
- warn clearly on partial support
- fail honestly on unsupported variants

This is much healthier than a binary "XTF yes/no" claim.

## Acceptance Criteria

Stage 02 should be considered successful when all of these are true:

- the project has an explicit XTF compatibility matrix
- the parser has fixture coverage for the main XTF families you expect users to bring
- unsupported XTF variants fail clearly instead of silently mis-parsing
- sidescan/sub-bottom/magnetometer coverage is verified across multiple real-world variants
- bathymetry-containing XTF is either supported or explicitly classified as partial/unsupported
- compatibility claims are based on fixtures, not only code inspection

## Minimum First-Pass Bar

Until a human explicitly revises the target, Stage 02 must at least produce:

- at least `10` documented fixture entries total
- at least `8` runnable validation cases that cover:
  - dual-channel sidescan
  - split-packet sidescan
  - sub-bottom
  - projected navigation
  - zero-nav backfill
  - malformed or truncated records
  - bathymetry-containing XTF
  - unsupported or unknown packet/channel behavior
- at least `2` prioritized vendor or recorder families beyond the current primary sample set, if such files are available to the team

If the required vendor-family coverage is not available, the stage closure note must say that explicitly instead of silently claiming broad support.

## Concrete Deliverables

By the end of Stage 02, implementation should have produced all of these:

- a checked-in XTF compatibility matrix
- a fixture corpus or fixture manifest for representative XTF families
- explicit support states:
  - supported
  - recognized but partial
  - unsupported
- parser behavior that reports unsupported or partial variants clearly
- a stage closure note listing which XTF families are now:
  - supported
  - partial
  - still deferred
- a fixture-to-support-state mapping so each collected fixture has an explicit expected outcome

## Test Categories

- dual-channel sidescan XTF
- split-packet sidescan XTF
- sub-bottom XTF
- XTF with mag fields populated
- projected-nav XTF
- XTF with zero ping coordinates but valid nav packets
- incorrect `BytesPerSample`
- missing `NumSamples`
- vendor-specific channel-name quirks
- bathymetry channel presence
- unknown packet types
- truncated/corrupt record sizes

## Suggested Delivery Order Inside Stage 02

### 2.1

- write the compatibility matrix
- collect fixture corpus
- define support categories

### 2.2

- harden classification and unsupported-format detection
- add packet and channel coverage where risk is highest

### 2.3

- expand modality coverage for bathymetry-related XTF where product value justifies it
- close the biggest vendor-specific gaps

## Relationship To Other Stages

- Stage 01 provides the parser/cache/UI foundation that makes compatibility work safer to implement.
- Stage 02 expands actual XTF capability breadth.
- Stage 03 turns broader XTF support into responsive, scalable workstation behavior.
- Stage 04 then refines import/reuse policy so already-supported XTF data is registered and reused cleanly.

## Out Of Scope For Stage 02

To keep this stage implementable, it should not absorb unrelated work:

- broad UI shell redesign belongs to Stage 01
- large-scale performance tuning belongs to Stage 03
- final import-once workflow policy belongs to Stage 04

## Bottom Line

No, the software does not already handle "all XTF formats".

It handles an important and promising subset.

Stage 02 should be the dedicated push that turns that subset into broad, tested, defensible XTF compatibility.
