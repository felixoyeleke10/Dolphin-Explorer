# Stage 08 Slice 120 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-120 — transactional NodeGraph v2 persistence
- primary goal: preserve the complete executable graph across save/open and reject unsupported or internally inconsistent graphs without mutating live state

## What Changed

- Introduced NodeGraph persistence format version 2 and now serializes every edge destination port plus node groups and their membership, in addition to nodes, parameters, and layout.
- `NodeGraph::fromJson` reconstructs into a temporary graph and swaps it into the receiver only after every object and cross-reference passes validation. A failed load leaves the existing graph byte-for-byte equivalent when reserialized.
- Graph validation covers document keys/version, collection shapes, registered node types, non-empty unique instance IDs, schema-known parameter names, exact parameter types/ranges/options, existing edge endpoints, valid and singly occupied input ports, valid finite layout coordinates, unique layout entries, unique group IDs, and valid unique group members.
- Legacy documents remain readable when the version and optional collections are absent; legacy edges without `to_port` continue to target input port zero.
- Project loading now fails closed when an embedded project or layer graph is not an object, contains an unknown node type, or otherwise fails graph validation. It no longer silently opens a project after discarding an unsupported processing graph.

## Files Touched

- `src/pipeline/NodeGraph.Serialization.cpp`
- `src/app/project/Project.Serialization.Read.cpp`
- `src/app/project/Project.Serialization.Layers.Read.cpp`
- `tests/test_node_graph.cpp`
- `tests/test_project_storage.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- Added the `NodeGraph` CTest target with a v2 round trip for non-zero `to_port`, parameters, layout, and groups; a legacy-port compatibility case; and rejection cases for unknown types, malformed containers/parameters, duplicate IDs, dangling references, invalid ports, invalid layout, and invalid groups.
- Every rejection case starts from a populated stable graph and asserts that failed deserialization does not mutate it. `ProjectStorage` also covers fail-closed project-level and layer-level embedded graphs.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `NodeGraph`, `PerfBaseline`, and `GlSmoke`.

## Gate Status

- gate items completed: graph persistence carries the topology data required for multi-input execution and grouping; deserialization is transactional; project open cannot silently erase an unsupported graph.
- gate items still open: aggregate build/test verification remains part of the final QC gate.

## Risks / Follow-Ups

- Unknown future node types and graph versions are intentionally rejected by older builds. If forward migration is added later, it needs an explicit compatibility policy rather than a best-effort partial load.
- Legacy edges without a port are unambiguously assigned to port zero; manifests that historically depended on another implicit input cannot be inferred after the fact.

## What The Next Stage May Assume

- A successful NodeGraph v2 round trip preserves node parameters, edge input ports, layout, and groups.
- A `false` result from `NodeGraph::fromJson` leaves the receiver unchanged, and project loading treats an invalid embedded graph as a project-load failure.
