# base-07 step 1: local execution evidence

On 2026-10-19, the working tree based on
`fe73f98e2bf9aef877b4fdea5f8f73a724d6fd7c` with this step's CI runner changes
was tested on Debian using QEMU 10.0.13 with TCG, four cores and 512 MiB RAM.
No hardware or GitHub Actions run is claimed.

- `make -j4`: exit 0; log `build/logs/base-07-build.log`.
- `bash tools/ci-boot.sh bios`: exit 0; 37 boot checks and 17 RAM checks
  passed, zero failed. Logs: `build/logs/boot-bios/{serial.log,results.log,provenance.txt}`.
- `bash tools/ci-boot.sh uefi`: exit 0; the same 54 checks passed, zero
  failed. Logs: `build/logs/boot-uefi/{serial.log,results.log,provenance.txt}`.
  Used Debian's OVMF_CODE_4M.fd and a copied OVMF_VARS_4M.fd with Secure Boot off.

Both runs used the freshly built ISO with SHA-256
`0286c4cc4b64f3942f26edbbfc7ebabb15ed9866a542e3d89e95719ca40ce84e`.
The local provenance records the base HEAD above because the runner was tested
before committing. CI checks out the run's SHA and archives provenance with it.
Logs remain in the worker's ignored build directory; generated binaries are
excluded from the proposed source changes and the two tracked build outputs
were restored from HEAD after testing.

The negative check uses the same BIOS QEMU arguments:

```sh
python3 tools/serialtest.py --log build/logs/base-07-negative-serial.log \
  --cmd 'selftest boot ## INTENTIONAL_MISSING_RESULT' -- \
  -accel tcg -cdrom Banana_OS.iso -boot d -m 512M -smp 4 -nic none
```

Expected and observed: exit 1 for the missing expected result even though the
37 boot selftests pass. Output is saved in
`build/logs/base-07-negative-results.log`. This verifies the existing serial
result driver; it does not demonstrate stable publication blocking (step 3).

`bash -n tools/ci-boot.sh`, parsing the workflow with PyYAML and the checklist
with Python's JSON parser, and `git diff --check` passed. GitHub artifact
upload/download and Ubuntu's packaged QEMU have not been executed locally.

Antigravity delegation was attempted once and rejected with
"Configuration must be root-owned and not group/world writable". No proposal
was applied and no server configuration was changed. Implementation and
validation were performed locally.

Finalization is blocked by the execution environment: `git add` cannot create
`/var/lib/yhello/repo/.git/worktrees/base-07-s1/index.lock` (read-only filesystem).
The tested source changes remain uncommitted for controller recovery; no Git
metadata permissions or integration checkout were modified.
