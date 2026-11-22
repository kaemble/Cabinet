#!/usr/bin/env python3
"""Run one graphics test on an X server with an atomically allocated display."""
import os
import select
import signal
import subprocess
import sys
import tempfile


def terminate(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    # Let CTest cancellation run the same cleanup as a normal test failure.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    server = test = None
    with tempfile.TemporaryDirectory(prefix="cabinet-xvfb-") as directory:
        with open(os.path.join(directory, "server.log"), "w+b") as log:
            reader, writer = os.pipe()
            try:
                # No TCP listener; this local server contains only test frames.
                # -displayfd chooses/locks its display inside Xvfb, avoiding the
                # check-then-start race in xvfb-run's free-number search.
                server = subprocess.Popen(
                    [sys.argv[1], "-displayfd", str(writer), "-screen", "0", "640x480x24", "-nolisten", "tcp", "-ac"],
                    pass_fds=(writer,), stdout=subprocess.DEVNULL, stderr=log,
                )
                os.close(writer)
                writer = -1
                if not select.select([reader], [], [], 10)[0]:
                    raise RuntimeError("Xvfb did not publish a display within 10 seconds")
                display = os.read(reader, 64).decode().strip()
                if not display.isdigit():
                    raise RuntimeError("Xvfb failed before publishing a display")
                env = dict(os.environ, DISPLAY=":" + display)
                env.pop("XAUTHORITY", None)
                test = subprocess.Popen(sys.argv[2:], env=env)
                return test.wait(timeout=50)
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                print(error, file=sys.stderr)
                log.seek(0)
                print(log.read().decode(errors="replace"), file=sys.stderr)
                return 1
            finally:
                os.close(reader)
                if writer >= 0:
                    os.close(writer)
                terminate(test)
                terminate(server)


if __name__ == "__main__":
    sys.exit(main())
