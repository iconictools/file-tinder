# Split Plan for Three Repositories

Target repositories:

1. `iconic-file-tinder`
2. `iconic-file-filer`
3. `iconic-ai-filer`

## Shared Core

Keep a shared core codebase (or mirrored subtree) containing:

- `app/include/*` and `app/lib/*` shared infrastructure
- `app/resources/*`
- `CMakeLists.txt` with `FILE_TINDER_APP_VARIANT`

## Variant Mapping

- `iconic-file-tinder` → `FILE_TINDER_APP_VARIANT=iconic-file-tinder`
- `iconic-file-filer` → `FILE_TINDER_APP_VARIANT=iconic-file-filer`
- `iconic-ai-filer` → `FILE_TINDER_APP_VARIANT=iconic-ai-filer`

## CI/CD

Use `.github/workflows/build-split-variants.yml` as the reference build pipeline.
For split repos, each repo can keep the same build steps while pinning a single variant.

## Recommended Extraction Order

1. Create three repos with the same baseline source tree.
2. In each repo, set default CMake configure arguments to its variant.
3. Keep CI enabled from day one for each repo.
4. Move shared code to a dedicated shared-core repo/submodule only after the three repos are stable.
