"""Convert the startup preview to attenuated, frame-aligned optical PCM."""

import sys
import wave
from pathlib import Path


def convert(source, output):
    with wave.open(str(source), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (2, 3, 48000):
            raise ValueError("Expected 48 kHz stereo 24-bit PCM")
        # Leave time for the DMA tail before the end of the artwork.
        frames = 6 * 48000
        data = wav.readframes(frames)
    if len(data) != frames * 6:
        raise ValueError("Startup preview is too short")
    fade = 7200
    with Path(output).open("w", encoding="ascii") as header:
        header.write("static const int16_t startup_pcm[] = {\n")
        for start in range(0, frames, 8):
            samples = []
            for frame in range(start, min(start + 8, frames)):
                scale = min(frames - 1 - frame, fade)
                for ch in range(2):
                    i = frame * 6 + ch * 3
                    sample = int.from_bytes(data[i:i + 3], "little", signed=True)
                    samples.append(str(sample * scale // (1024 * fade)))
            header.write("  " + ", ".join(samples) + ",\n")
        header.write("};\n")


if __name__ == "__main__":
    convert(*sys.argv[1:])
