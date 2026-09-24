# Cline project rules

Before editing, read the repository root `AGENTS.md` and `docs/code-map.md`.
For code-navigation tasks, use the compact map and inspect only the owning module plus its public header and related tests.
The canonical portable skill is `skills/code-map/SKILL.md`.

Keep changes minimal and run the relevant `make` verification target. Preserve the protocol invariants documented in `docs/PROTOCOL.md` and the map; do not edit generated `src/layout_data.cpp` by hand.
