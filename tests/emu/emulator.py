"""Runs the real ESP32-S3 firmware image inside Espressif's esp-emu emulator.

The emulator speed and timing do NOT match a physical board. These tests find
crashes, hangs, lost saves and bad memory use, not real-world speed.
"""
import atexit
import os
import shutil
import signal
import subprocess
import threading
import time

ESP_EMU = os.environ.get("ESP_EMU", os.path.expanduser("~/.local/bin/esp-emu"))


class Emulator:
    def __init__(self, flash_image, elf=None, port=25565, workdir=None):
        self.flash_image = flash_image
        self.elf = elf
        self.port = port
        self.proc = None
        self.lines = []
        self._lock = threading.Lock()
        self.control_port = port + 1000

    def start(self):
        cmd = [
            ESP_EMU, "--chip", "esp32s3", "--firmware", self.flash_image,
            "--save-state",
            "--wifi-ssid", "your-ssid", "--wifi-password", "your-password",
            "--net", "user,hostfwd=tcp::%d-:25565" % self.port,
            "--control-tcp", "127.0.0.1:%d" % self.control_port,
        ]
        if self.elf:
            cmd += ["--elf", self.elf]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        atexit.register(self.crash)  # never leave an emulator running if a test fails
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        for line in self.proc.stdout:
            with self._lock:
                self.lines.append(line.rstrip("\n"))

    def text(self):
        with self._lock:
            return "\n".join(self.lines)

    def wait_for(self, needle, timeout=60, since=0):
        end = time.time() + timeout
        while time.time() < end:
            with self._lock:
                if any(needle in l for l in self.lines[since:]):
                    return True
            if self.proc.poll() is not None:
                break
            time.sleep(0.1)
        return False

    CRASH_MARKERS = ("Guru Meditation", "abort() was called", "Backtrace:", "Task watchdog got triggered",
                     "Interrupt wdt timeout", "CORRUPT HEAP", "assert failed", "Stack canary", "LoadProhibited",
                     "StoreProhibited", "heap_caps_malloc", "Failed to allocate", "Out of memory")

    def crash_lines(self):
        """Lines of the console log that point to a crash, watchdog reset or memory failure."""
        with self._lock:
            return [l for l in self.lines if any(m in l for m in self.CRASH_MARKERS)]

    def mark(self):
        with self._lock:
            return len(self.lines)

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def stop(self):
        """Graceful stop: the emulator writes the flash contents back to the image."""
        if self.alive():
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def crash(self):
        """Instant stop with no chance to save: like pulling the power."""
        if self.alive():
            self.proc.kill()
            self.proc.wait()

    def read_flash_region(self, offset, length, path, timeout=120):
        """Snapshot part of the emulated flash while the firmware keeps running."""
        import socket
        s = socket.create_connection(("127.0.0.1", self.control_port), timeout=timeout)
        s.sendall(("read-region 0x%x %d %s\n" % (offset, length, path)).encode())
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = s.recv(4096)
            if not chunk:
                break
            reply += chunk
        s.close()
        assert reply.strip() == b"ok", "read-region failed: %r" % reply


def fresh_image(src, dst):
    shutil.copyfile(src, dst)
    return dst
