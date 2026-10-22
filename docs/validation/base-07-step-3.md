# base-07 step 3: local execution evidence and stable release gating verification

On 2026-10-20, base-07 step 3 was implemented and verified locally on Debian.
The working tree builds upon commit `c4da22dc9575f94020eaff5f5601e03c79abd0a3`.

## 1. CI Workflow Specification

In `.github/workflows/build-iso.yml`, the dedicated stable release publication job `publish-stable` was introduced:
- `needs: [ build, boot ]`: explicitly depends on both the ISO build and the complete `boot` validation matrix.
- `if: github.ref == 'refs/heads/main' && success()`: only executes when all matrix legs in `boot` succeed cleanly on `main`.
- Publishes tag `v${{ github.run_number }}-stable` with release flag `prerelease: false`.
- The `boot` matrix executes:
  1. `sh tools/ext4test/run.sh`: Host ext4 and journal replay tests (cuts before/after commit, e2fsck verification).
  2. `bash tools/ci-boot.sh "${{ matrix.firmware }}"`: QEMU boot reaching shell prompt, verifying `selftest boot` (37 checks) and `selftest ram` (17 checks) reporting `0 failed`.
  3. `bash tools/ci-install-test.sh "${{ matrix.firmware }}"`: Automated installation to a fresh disposable disk image (`disks`, `install 1 --minimal`, confirmation `y`), standalone disk boot without CD-ROM (`selftest boot`, `selftest ram` reporting `0 failed`), and ISO update (`update 1`).

Any failure, crash, timeout, or unexpected skipped test in either BIOS or UEFI causes `boot` to exit non-zero, automatically blocking `publish-stable`.

## 2. Local Execution Evidence

All gating test suites were executed and verified locally with QEMU 10.0.13 (TCG, 4 cores, 512 MiB RAM):

1. **Host ext4 filesystem & journal replay test**:
   - Command: `sh tools/ext4test/run.sh`
   - Result: Exit 0 (`ext4test: all passed`), verified journal replay and e2fsck clean passes across cut scenarios.

2. **Firmware Boot Smoke Checks (BIOS)**:
   - Command: `bash tools/ci-boot.sh bios`
   - Result: Exit 0. 37 boot menu checks passed, 17 RAM reporting checks passed, 0 failed.
   - Logs: `build/logs/boot-bios/serial.log`, `build/logs/boot-bios/results.log`, `build/logs/boot-bios/provenance.txt`.

3. **Firmware Boot Smoke Checks (UEFI)**:
   - Command: `bash tools/ci-boot.sh uefi`
   - Result: Exit 0. 37 boot menu checks passed, 17 RAM reporting checks passed, 0 failed.
   - Logs: `build/logs/boot-uefi/serial.log`, `build/logs/boot-uefi/results.log`, `build/logs/boot-uefi/provenance.txt`.

4. **Disposable Image Installation, Disk Boot & Update Checks (BIOS)**:
   - Command: `bash tools/ci-install-test.sh bios`
   - Result: Exit 0. `disks`, `install 1 --minimal` ('y'), standalone QEMU disk boot (37 boot checks, 17 RAM checks passed, 0 failed), and `update 1` passed cleanly.
   - Logs: `build/logs/install-bios/results-install.log`, `build/logs/install-bios/results-diskboot.log`, `build/logs/install-bios/results-update.log`.

5. **Disposable Image Installation, Disk Boot & Update Checks (UEFI)**:
   - Command: `bash tools/ci-install-test.sh uefi`
   - Result: Exit 0. Cleanly completed installation, standalone disk boot (54 passed checks, 0 failed), and ISO update.
   - Logs: `build/logs/install-uefi/results-install.log`, `build/logs/install-uefi/results-diskboot.log`, `build/logs/install-uefi/results-update.log`.

## 3. Negative Intentional Failure Test

To demonstrate that an unexpected failure or regression strictly fails validation and halts the pipeline before stable release:
- Command:
  ```sh
  python3 tools/serialtest.py --log build/logs/negative-failure-test.log \
    --boot-timeout 180 --timeout 60 \
    --cmd 'selftest boot ## INTENTIONAL_MISSING_SUBSTRING' -- \
    -accel tcg -cdrom Banana_OS.iso -boot d -m 512M -smp 4 -nic none
  ```
- Result: Exit code 1 (`FAIL selftest boot <- expected 'INTENTIONAL_MISSING_SUBSTRING'`). Log archived at `build/logs/negative-failure-test.log`.
- In GitHub Actions, exit code 1 causes the `boot` job to fail immediately, preventing downstream job `publish-stable` from running (`needs: [ build, boot ]`).
