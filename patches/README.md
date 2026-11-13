# Framework patches

Yakumo is vendored under `work/tools/Yakumo` (ignored by Git). The framework changes this project
needs live here so a clean clone can reproduce the bring-up with `scripts/bringup.ps1`, which
applies every `*.patch` in this directory after cloning and before building.

| Patch | Why it exists |
|---|---|
| `0001-framework-bringup.patch` | Interpreter and decoder coverage the bring-up corpus needs: VFPU families `vfpu1`/`vfpu4` lowering gaps, `vi2uc`/`vi2c`/`vi2us`/`vi2s`, retired-instruction accounting so the interpreter does not spin, and the codegen switches the profile relies on. |

Regenerate a patch from the vendored tree with:

```powershell
git -C work/tools/Yakumo diff -- <files> | Set-Content -Encoding utf8 patches/<name>.patch
```
