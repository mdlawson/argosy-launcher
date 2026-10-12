import pathlib
import struct
import subprocess
import sys
import tempfile
import wave


def verify(path, rate, frames):
    with wave.open(str(path), "rb") as audio:
        assert audio.getnchannels() == 2
        assert audio.getsampwidth() == 2
        assert audio.getframerate() == rate
        assert audio.getnframes() == frames
        payload = audio.readframes(frames)
    decoded = struct.unpack(f"<{frames * 2}h", payload)
    expected = tuple(value for i in range(frames)
                     for value in ((i * 17) % 60000 - 30000,
                                   30000 - (i * 31) % 60000))
    assert decoded == expected, path


with tempfile.TemporaryDirectory(prefix="argosy-capture-") as folder:
    root = pathlib.Path(folder)
    subprocess.run([sys.argv[1], folder], check=True)
    verify(root / "normal/audio-core.wav", 32040, 32040)
    verify(root / "normal/audio-output.wav", 48000, 48000)
    assert "capture saved success=1" in (root / "normal/audio-diagnostics.txt").read_text()
    verify(root / "aborted/audio-core.wav", 10, 5)
    verify(root / "aborted/audio-output.wav", 10, 5)
    assert "capture saved success=0" in (root / "aborted/audio-diagnostics.txt").read_text()
    verify(root / "overflow/audio-core.wav", 10, 30)
    verify(root / "overflow/audio-output.wav", 10, 10)
    report = (root / "overflow/audio-diagnostics.txt").read_text()
    assert "capture saved success=0" in report
    assert "dropped_core_frames=10" in report
    assert not list((root / "idle").iterdir())
print("PASS: WAV decoding, stereo payloads, two producers, exact duration, abort, overflow, idle")
