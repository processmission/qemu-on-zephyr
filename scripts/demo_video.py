# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path
import shutil
import subprocess

from PIL import Image


class DemoVideo:
    def __init__(self, directory: Path, *, fps: int = 5, size: tuple[int, int] = (1024, 768)):
        self.ffmpeg = shutil.which("ffmpeg")
        self.ffprobe = shutil.which("ffprobe")
        if not self.ffmpeg or not self.ffprobe:
            raise RuntimeError("Video recording requires FFmpeg and FFprobe")
        self.directory = directory
        self.fps = fps
        self.size = size
        self.frames = 0
        self.previous: bytes | None = None
        self.log = (directory / "video.log").open("w")
        self.process = subprocess.Popen([
            self.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y",
            "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", f"{size[0]}x{size[1]}",
            "-framerate", str(fps), "-i", "pipe:0", "-an", "-c:v", "libx264",
            "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p", "-threads", "2",
            "-movflags", "+faststart", str(directory / "nanojev-original.mp4"),
        ], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=self.log)

    def append(self, frame: Image.Image, elapsed: float) -> None:
        data = frame.convert("RGB").resize(self.size, Image.Resampling.LANCZOS).tobytes()
        target = max(self.frames, int(elapsed * self.fps))
        while self.frames < target:
            self.process.stdin.write(self.previous or data)
            self.frames += 1
        self.process.stdin.write(data)
        self.frames += 1
        self.previous = data

    def close(self) -> None:
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            result = self.process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        finally:
            self.log.close()
        if result:
            raise RuntimeError(f"Video encoding failed; see {self.directory / 'video.log'}")

    def edit(self, boot_seconds: float, boot_speed: float) -> dict:
        original = self.directory / "nanojev-original.mp4"
        edited = self.directory / "nanojev.mp4"
        gif = self.directory / "nanojev.gif"
        palette = self.directory / "palette.png"
        graph = (
            f"[0:v]split[boot][maze];[boot]trim=end={boot_seconds:.6f},"
            f"setpts=(PTS-STARTPTS)/{boot_speed}[intro];"
            f"[maze]trim=start={boot_seconds:.6f},setpts=PTS-STARTPTS[play];"
            f"[intro][play]concat=n=2:v=1:a=0,fps={self.fps}[video]"
        )
        with (self.directory / "video-edit.log").open("w") as log:
            subprocess.run([self.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y", "-i", str(original),
                            "-filter_complex", graph, "-map", "[video]", "-an", "-c:v", "libx264",
                            "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p", "-threads", "2",
                            "-movflags", "+faststart", str(edited)], check=True, stdout=log, stderr=log)
            subprocess.run([self.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y", "-i", str(edited),
                            "-vf", "palettegen=max_colors=128:stats_mode=diff", "-frames:v", "1", "-update", "1",
                            str(palette)], check=True, stdout=log, stderr=log)
            subprocess.run([self.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y", "-i", str(edited),
                            "-i", str(palette), "-filter_complex",
                            "[0:v][1:v]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle",
                            "-loop", "0", str(gif)], check=True, stdout=log, stderr=log)
        durations = {}
        for name, file in (("original", original), ("edited", edited), ("gif", gif)):
            data = json.loads(subprocess.check_output([
                self.ffprobe, "-v", "error", "-show_entries", "format=duration", "-of", "json", str(file)]))
            durations[name] = float(data["format"]["duration"])
        expected = boot_seconds / boot_speed + durations["original"] - boot_seconds
        if abs(durations["edited"] - expected) > 2 / self.fps or abs(durations["gif"] - durations["edited"]) > 2 / self.fps:
            raise RuntimeError("The edited recording did not preserve normal maze playback timing")
        return {"fps": self.fps, "frames": self.frames, "boot_seconds": boot_seconds,
                "boot_speed": boot_speed, "maze_speed": 1, "durations": durations}
