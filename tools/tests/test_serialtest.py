# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("serialtest", Path(__file__).resolve().parents[1] / "serialtest.py")
serialtest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serialtest)


class CommandFailureTests(unittest.TestCase):
    def test_passing_nested_suite(self):
        self.assertFalse(serialtest.command_failure("all tests passed (0 failed)\nselftest: 230 passed, 0 failed (45000 ms)\n"))

    def test_nested_success_does_not_hide_outer_failure(self):
        self.assertTrue(serialtest.command_failure("all tests passed (0 failed)\nselftest: 229 passed, 1 failed (45000 ms)\n"))

    def test_failed_check_without_crash(self):
        self.assertTrue(serialtest.command_failure("  FAIL  clock moved backwards\n"))

    def test_expected_recovered_signal(self):
        self.assertFalse(serialtest.command_failure("  ok    stack overflow recovered\n  ok    segmentation fault recovered\n"))

    def test_actual_crash_after_recovered_signal(self):
        self.assertTrue(serialtest.command_failure("  ok    segmentation fault recovered\napp crashed: systest\n"))

    def test_panic_stays_fatal(self):
        self.assertIn("PANIC", serialtest.command_failure("PANIC: kernel fault\n"))

    def test_zero_loss_requires_zero_loss(self):
        self.assertFalse(serialtest.expected_output_present("100% packet loss", "0% packet loss"))
        self.assertTrue(serialtest.expected_output_present("1 received, 0% packet loss", "0% packet loss"))

    def test_numeric_expectation_does_not_match_another_count(self):
        self.assertFalse(serialtest.expected_output_present("117 passed", "17 passed"))
        self.assertTrue(serialtest.expected_output_present("selftest: 17 passed", "17 passed"))


if __name__ == "__main__":
    unittest.main()
