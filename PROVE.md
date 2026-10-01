| id | claim | task | form | command | rc | result | evidence | commit | at |
|---|---|---|---|---|---|---|---|---|---|
| P7.1 | C7.1 | T7 | local | `test $(wc -l < docs/lessons-prototype1.md) -ge 60 && grep -q 'use_glib' docs/lessons-prototype1.md && grep -qi 'compiled' docs/lessons-prototype1.md` | 0 | pass | docs/lessons-prototype1.md has 124 lines (>=60 required); 4 lines matching 'use_glib'; 8 lines matching 'compiled' (case-insensitive); all three conditions satisfied | b99378e | 2026-10-01T17:56Z |
