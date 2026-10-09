# SPDX-License-Identifier: Apache-2.0

import itertools
import json
from pathlib import Path
import threading
import time

from PIL import Image, ImageDraw, ImageFont
import pyte


class TerminalRecording:
    def __init__(self, directory: Path, name: str, font: Path, *, desktop: bool = False):
        self.rows = 28 if desktop else 24
        self.row_height = 18 if desktop else 21
        self.screen = pyte.Screen(92, self.rows)
        self.stream = pyte.Stream(self.screen)
        self.lock = threading.Lock()
        self.started = time.monotonic()
        self.font = ImageFont.truetype(str(font), 13 if desktop else 16)
        self.cell_width = round(self.font.getlength("M"))
        self.size = (800, 600) if desktop else (self.cell_width * 92 + 40, 24 * 21 + 78)
        self.log = (directory / f"{name}.log").open("w")
        self.cast = (directory / f"{name}.cast").open("w")
        self.cast.write(json.dumps({"version": 2, "width": 92, "height": self.rows}) + "\n")

    def write(self, text: str) -> None:
        with self.lock:
            self.log.write(text)
            self.log.flush()
            self.cast.write(json.dumps([time.monotonic() - self.started, "o", text]) + "\n")
            self.cast.flush()
            self.stream.feed(text)

    def flush(self) -> None:
        self.log.flush()
        self.cast.flush()

    def close(self) -> None:
        self.log.close()
        self.cast.close()

    def frame(self, title: str = "QEMU ON ZEPHYR  /  Linux user mode  /  AArch64 TCG") -> Image.Image:
        colors = {"default": "#dce7f4", "black": "#101a29", "red": "#ff7b86",
                  "green": "#7dd3a8", "yellow": "#f1d18a", "blue": "#79b8ff",
                  "magenta": "#c5a3ff", "cyan": "#7dd3d8", "white": "#f1f5f9"}
        image = Image.new("RGB", self.size, "#101a29")
        draw = ImageDraw.Draw(image)
        draw.rectangle((0, 0, image.width, 47), fill="#243650")
        draw.text((20, 14), title, font=self.font, fill="#7dd3d8")
        with self.lock:
            for row in range(self.rows):
                cells = [self.screen.buffer[row][column] for column in range(92)]
                column = 0
                for attributes, group in itertools.groupby(cells, key=lambda c: (c.fg, c.bg, c.reverse)):
                    characters = "".join(cell.data for cell in group)
                    foreground, background, reverse = attributes
                    fg = colors.get(foreground, "#" + foreground)
                    bg = "#101a29" if background == "default" else colors.get(background, "#" + background)
                    if reverse:
                        fg, bg = bg, fg
                    x, y = 20 + column * self.cell_width, 58 + row * self.row_height
                    if bg != "#101a29":
                        draw.rectangle((x, y, x + len(characters) * self.cell_width,
                                        y + self.row_height), fill=bg)
                    draw.text((x, y), characters, font=self.font, fill=fg)
                    column += len(characters)
        return image
