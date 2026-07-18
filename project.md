# PROJECT

## Goal

Rebuild Mango session restore around the standard Wayland session protocol instead of relying on heuristics as the primary design.

## Objectives

- implement `xdg-session-management-v1` in Mango
- persist compositor-side session IDs and per-toplevel identities
- restore protocol-aware Wayland clients through the protocol
- preserve a useful fallback path for non-participating clients and Xwayland
- move session-related code out of `src/mango.c` into dedicated modules

## Design Direction

Primary path:
- protocol-native restore

Fallback path:
- Mango-managed snapshot and relaunch heuristics

## Non-Goals

- do not require every application to be Mango-launched
- do not treat heuristic relaunch as the long-term core architecture
- do not keep expanding session logic directly inside `src/mango.c`

## Current Branch

- `feature/xdg-session-management-scaffold`

This branch exists to scaffold protocol support first, then layer actual restore behavior on top of it cleanly.
