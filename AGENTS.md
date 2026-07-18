# AGENTS

## Scope

This file applies to the whole repository.

## Current Focus

The current branch is focused on protocol-native session restore for Mango.

Primary target:
- implement `xdg-session-management-v1` in Mango

Architecture:
- use protocol-native restore as the primary path for participating Wayland clients
- keep Mango's heuristic relaunch/session restore as a fallback for non-participating clients and Xwayland

## Branch Intent

Branch:
- `feature/xdg-session-management-scaffold`

Purpose:
- scaffold a clean protocol-first session restore architecture from `upstream/main`
- avoid growing the old heuristic implementation directly inside `src/mango.c`

## Implementation Rules

- keep new session-management code in dedicated `src/session/` files
- keep `src/mango.c` changes small and lifecycle-oriented
- do not expose a bogus or half-implemented protocol contract intentionally
- prefer deterministic restore state over launch-command guessing
- treat heuristic relaunch as compatibility behavior, not the core design

## Success Criteria

- protocol-aware Wayland clients can restore through `xdg-session-management-v1`
- non-participating clients still have a fallback restore path
- the session restore code is modular enough to review independently from `src/mango.c`
