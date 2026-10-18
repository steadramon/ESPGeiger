# Patches to vendored code

Each library under `vendor/` is an upstream release with these patches
already applied; the build uses the files as they are. A patch is kept here
so the library can be updated without losing it.

| Library   | Upstream                                        | Patch                       |
|-----------|-------------------------------------------------|-----------------------------|
| micro-ecc | [v1.2](https://github.com/kmackay/micro-ecc/releases/tag/v1.2) | `micro-ecc-branchless-carry.patch`: carry and borrow in `uECC_vli_add` / `uECC_vli_sub` by bit operations, so signing does not branch on key data |

## Updating a library

1. Replace `vendor/<library>/` with the new upstream release.
2. `git apply --directory=vendor/<library> vendor/patches/<patch>`
3. Run the host suites (`test_uecc` pins micro-ecc's keys and signatures).
4. Update the table above.
