import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class UartProtocolTests(unittest.TestCase):
    def test_fragmentation_noise_corruption_and_resynchronization(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "uart"
            subprocess.run(
                ["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-I", str(ROOT / "main"), str(ROOT / "tests/test_sony_uart.c"),
                 str(ROOT / "main/sony_uart_protocol.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
