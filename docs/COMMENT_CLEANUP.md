# Comment cleanup summary

This pass changes only documentation comments; no settings, logic, hooks or build options have changed.

## Results

- `CameraResponse.ini`: 43 to 14 comment lines (-67%).
- `source/CameraResponse.cpp`: 504 to 181 comment lines (-64%).
- Smaller comments in `CameraResponseAPI.h`, `ResponseCore.h`, `premake5.lua`, and `build.bat` were also condensed.
- At the comment-cleanup stage, all 133 settings and 36 sections stayed identical. The later 4K overlay revision adds one setting.

## Comment policy

- Keep short notes for hidden behavior, safety constraints, important offsets and ABI details.
- Summarize implementation rationale rather than repeating what the code already names.
- Remove outdated version history, duplicate descriptions, and commentary about previous bugs once the current invariant is clear.
- Leave user-facing INI notes only where the setting's semantics, defaults or compatibility implications need explanation.

## Validation

- Non-comment lines match the original optimized project exactly across all six edited files.
- `tools/config_audit.py`: passed (133 settings, 36 sections at comment-cleanup time; now 134 settings).
- `tools/performance_audit.py`: passed.
- `tests/config_contract_tests.py`: passed.
- `tests/response_core_tests.cpp` (G++/Linux build and run): passed.

No Win32 plugin-sdk ASI or in-game regression test was run. The runtime behavior is intended to remain identical because executable code and configuration values were not edited.
