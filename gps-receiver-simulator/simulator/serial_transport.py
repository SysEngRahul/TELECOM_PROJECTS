"""Transports that carry NMEA text from the simulator to the C receiver.

  StdoutTransport : writes to stdout (a Unix pipe when piped). A pipe is a
                    byte stream with kernel buffering - NOT a UART. There is no
                    baud rate, framing, parity or electrical layer.
  PtyTransport    : opens a Linux pseudo-terminal. The slave side
                    (/dev/pts/N) is a real tty device that programs open like a
                    serial port, so termios, raw mode and EIO-on-hangup
                    behaviour resemble a serial device. Still no electrical
                    layer and no baud-rate timing.

Both can optionally write in small chunks to exercise the receiver's
fragmented-input handling.
"""
import os
import sys
import time
import tty


class TransportError(RuntimeError):
    pass


class _BaseTransport:
    def __init__(self, chunk_size=0, chunk_delay_s=0.0):
        if chunk_size < 0:
            raise ValueError("chunk_size must be >= 0")
        self.chunk_size = chunk_size
        self.chunk_delay_s = chunk_delay_s

    def _chunks(self, data: bytes):
        if self.chunk_size <= 0:
            yield data
            return
        for i in range(0, len(data), self.chunk_size):
            yield data[i:i + self.chunk_size]

    def write(self, text: str) -> None:
        data = text.encode("ascii", errors="replace")
        for chunk in self._chunks(data):
            self._write_raw(chunk)
            if self.chunk_size and self.chunk_delay_s:
                time.sleep(self.chunk_delay_s)

    def _write_raw(self, data: bytes) -> None:  # pragma: no cover
        raise NotImplementedError

    def close(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class StdoutTransport(_BaseTransport):
    """Only NMEA goes to stdout; diagnostics must use stderr."""

    def __init__(self, stream=None, **kwargs):
        """stream: any binary file-like object (default: sys.stdout.buffer)."""
        super().__init__(**kwargs)
        self._out = stream if stream is not None else sys.stdout.buffer

    def _write_raw(self, data: bytes) -> None:
        try:
            self._out.write(data)
            self._out.flush()
        except BrokenPipeError as exc:  # receiver exited early
            raise TransportError("receiver closed the pipe") from exc


class PtyTransport(_BaseTransport):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.master_fd, self.slave_fd = os.openpty()
        tty.setraw(self.slave_fd)  # no echo, no CR/LF translation, 8-bit clean
        self.slave_path = os.ttyname(self.slave_fd)

    def _write_raw(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            written = os.write(self.master_fd, view)
            view = view[written:]

    def close(self, drain_s: float = 0.0) -> None:
        if drain_s:
            time.sleep(drain_s)  # let a reader drain before hang-up discards data
        for fd in (self.master_fd, self.slave_fd):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master_fd = self.slave_fd = -1
