#!/usr/bin/env python3
"""Start the compiled native C server and run the real Minecraft client test."""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_e2e.py /path/to/mcserver_native", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1]).resolve()
    client = Path(__file__).resolve().parent / "e2e_client.py"
    server = subprocess.Popen(
        [str(binary)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        # Native startup includes world initialization. Give it time without
        # opening a stray client connection that could perturb the protocol.
        time.sleep(3)
        if server.poll() is not None:
            raise RuntimeError(f"Server quit during startup ({server.returncode})")
        test = subprocess.run(
            [sys.executable, str(client)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=110,
            check=False,
        )
        print(test.stdout, flush=True)
        if test.returncode:
            raise RuntimeError(f"End-to-end client failed (exit {test.returncode})")
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"E2E failure: {exc}", file=sys.stderr)
        return 1
    finally:
        if server.poll() is None:
            server.terminate()
        try:
            output, _ = server.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            server.kill()
            output, _ = server.communicate(timeout=8)
        if output and server.returncode not in (0, -15):
            print("Server log:\n" + output[-20000:], file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
