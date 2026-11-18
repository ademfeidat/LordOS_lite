import importlib.util
import os
import tempfile
import unittest

parser_path = os.path.join(os.path.dirname(__file__), "..", "tools", "installer", "parse-grub-write-trace.py")
spec = importlib.util.spec_from_file_location("parse_grub_write_trace", parser_path)
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
parse_trace_files = parser.parse_trace_files
changed_ranges = parser.changed_ranges
covered = parser.covered


class GrubTraceParserTests(unittest.TestCase):
    def test_sequential_and_pwrite_ranges(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = os.path.join(tmp, "trace")
            with open(prefix + ".123", "w") as f:
                f.write('lseek(6</dev/loop0<block 7:0>>, 512, SEEK_SET) = 512\n')
                f.write('write(6</dev/loop0<block 7:0>>, "abc", 512) = 512\n')
                f.write('pwrite64(6</dev/loop0<block 7:0>>, "xyz", 512, 0) = 512\n')
                f.write('ioctl(6</dev/loop0<block 7:0>>, BLKFLSBUF) = 0\n')
            result = parse_trace_files(prefix, "/dev/loop0", 4096)
            self.assertEqual(result["write_operations"], 2)
            self.assertEqual(result["write_ranges"], [(0, 1024)])
            self.assertEqual(result["target_ioctls"], ["BLKFLSBUF"])
            self.assertFalse(result["unclassified_target_writes"])

    def test_reject_non_target_block_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = os.path.join(tmp, "trace")
            with open(prefix + ".1", "w") as f:
                f.write('write(4</dev/sda<block 8:0>>, "x", 1) = 1\n')
            result = parse_trace_files(prefix, "/dev/loop0", 4096)
            self.assertEqual(len(result["non_target_block_device_writes"]), 1)

    def test_reject_unknown_offset(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = os.path.join(tmp, "trace")
            with open(prefix + ".1", "w") as f:
                f.write('write(6</dev/loop0<block 7:0>>, "x", 1) = 1\n')
            result = parse_trace_files(prefix, "/dev/loop0", 4096)
            self.assertTrue(result["unclassified_target_writes"])

    def test_changed_bytes_must_be_covered(self):
        with tempfile.TemporaryDirectory() as tmp:
            before, after = os.path.join(tmp, "before"), os.path.join(tmp, "after")
            open(before, "wb").write(bytes(1024))
            data = bytearray(1024); data[512] = 1
            open(after, "wb").write(data)
            changed = changed_ranges(before, after)
            self.assertEqual(changed, [(512, 1)])
            self.assertTrue(covered(changed, [(512, 512)]))
            self.assertFalse(covered(changed, [(0, 512)]))


if __name__ == "__main__":
    unittest.main()
