# Rollback Anchors (C++23 Architecture Refactor)

| Milestone | Commit SHA | Description | Status |
| :--- | :--- | :--- | :--- |
| P0 Baseline | 847926b | Architecture guard v2, refactor plan, baseline lock | Complete |
| P1 Decouple Include Hub | 17312e1 | Decouple globals.h include hub and localize provider headers | Complete |
| P2 Dissolve Audio Engine | 93a00e4 | Dissolve src/audio/engine.cpp into layer-owned files | Complete |
| P3 Decompose Main | 9fade0d | Decompose src/app/main.cpp into high-cohesion units | Complete |
| P4 Decompose Settings | 1f243f5 | Decompose settings.cpp and extract platform text injector | Complete |
| P5 Eliminate Globals | 34bcf3f | Eliminate globals.h and migrate global state to domain owners | Complete |
| P6 Modernize Audio Spans | 357f3cb | Modernize audio slice signatures to std::span and modern C++23 idioms | Complete |
