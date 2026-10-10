# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from queue import Empty, Queue
import threading
import tkinter as tk

from nanojev_demo import MazeSession, save_report

BACKGROUND = "#0c1622"
FOREGROUND = "#e4edf5"
MUTED = "#9bb0bf"
MINT = "#7ff3c9"
CYAN = "#4dcae7"
REPORT = Path("/run/nanojev/report.json")


class DemoWindow:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("NanoJev - QEMU on Zephyr")
        self.root.geometry("984x660+18+34")
        self.root.configure(bg=BACKGROUND)
        self.events: Queue = Queue()
        self.commands: Queue = Queue()
        self.running = threading.Event()
        self.ready = False
        self.frame = None
        self.canvas = tk.Canvas(self.root, width=984, height=590, bg=BACKGROUND, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        controls = tk.Frame(self.root, bg=BACKGROUND)
        controls.pack(fill="x", padx=24, pady=12)
        for label, command in (("Run / Pause", self.toggle), ("One step", self.step), ("Reset", self.reset)):
            tk.Button(controls, text=label, command=command, bg="#193242", fg=FOREGROUND,
                      activebackground=MINT, font=("DejaVu Sans", 11), padx=16, pady=5).pack(side="left", padx=(0, 12))
        self.size = tk.StringVar(value="16")
        selector = tk.OptionMenu(controls, self.size, "5", "8", "16", command=self.change_size)
        selector.configure(bg="#193242", fg=FOREGROUND, highlightthickness=0, font=("DejaVu Sans", 11))
        selector.pack(side="left", padx=12)
        tk.Label(controls, text="Hardware virtualization", bg=BACKGROUND, fg=MUTED,
                 font=("DejaVu Sans", 10)).pack(side="right")
        self.root.bind("<space>", lambda event: self.toggle())
        self.root.bind("<Right>", lambda event: self.step())
        self.root.bind("r", lambda event: self.reset())
        self.root.bind("<F5>", lambda event: self.change_size("5"))
        self.root.bind("<F8>", lambda event: self.change_size("8"))
        self.root.bind("<F9>", lambda event: self.change_size("16"))
        self.draw(None, "Loading Qwen3-0.6B and decision heads...")
        threading.Thread(target=self.worker, daemon=True).start()
        self.root.after(100, self.poll)

    def toggle(self) -> None:
        if self.ready:
            if self.running.is_set():
                self.running.clear()
            else:
                self.running.set()
                self.commands.put("run")

    def step(self) -> None:
        if self.ready and not self.running.is_set():
            self.commands.put("step")

    def reset(self) -> None:
        if self.ready:
            self.running.clear()
            self.commands.put("size:" + self.size.get())

    def change_size(self, size: str) -> None:
        if self.ready:
            self.size.set(size)
            self.reset()

    def worker(self) -> None:
        try:
            session = MazeSession(Path("/opt/nanojev/checkpoint"))
            self.events.put((session.report(), "Ready"))
            while True:
                if self.running.is_set():
                    try:
                        command = self.commands.get_nowait()
                    except Empty:
                        command = "step"
                else:
                    command = self.commands.get()
                if command == "run" and not self.running.is_set():
                    continue
                if command.startswith("size:"):
                    session.reset(int(command.removeprefix("size:")), 17)
                elif not session.report()["finished"]:
                    self.events.put((session.report(), "Computing safety probabilities..."))
                    session.step()
                report = session.report()
                save_report(REPORT, report)
                self.events.put((report, report["status"]))
                if command.startswith("size:"):
                    print(f"NANOJEV_MAZE_READY size={report['initial_state']['size']}", flush=True)
                if report["finished"]:
                    self.running.clear()
                    print(f"NANOJEV_FINISHED goal={int(report['goal_reached'])} steps={len(report['steps'])}", flush=True)
        except Exception as error:
            self.running.clear()
            self.events.put((None, f"Inference failed: {error}"))
            print(f"NANOJEV_FAILED {error}", flush=True)
            raise

    def draw(self, report: dict | None, status: str) -> None:
        canvas = self.canvas
        canvas.delete("all")
        canvas.create_text(24, 27, anchor="w", text="NanoJev", fill=FOREGROUND,
                           font=("DejaVu Sans", 26, "bold"))
        canvas.create_text(25, 61, anchor="w", text="LOCAL MODEL INFERENCE  /  LINUX DESKTOP", fill=MUTED,
                           font=("DejaVu Sans", 10))
        canvas.create_text(650, 29, anchor="w", text="QEMU on Zephyr", fill=MINT,
                           font=("DejaVu Sans", 16, "bold"))
        canvas.create_text(650, 58, anchor="w", text="Full-system emulation", fill=MUTED,
                           font=("DejaVu Sans", 11))
        canvas.create_line(24, 83, 956, 83, fill="#294050")
        canvas.create_text(650, 123, anchor="w", text="Live safety probabilities", fill=FOREGROUND,
                           font=("DejaVu Sans", 14, "bold"))
        if report:
            initial = report["initial_state"]
            cell = 440 / initial["size"]
            origin_x, origin_y = 72, 124
            walls = set(map(tuple, initial["walls"]))
            for row in range(initial["size"]):
                for col in range(initial["size"]):
                    x, y = origin_x + col * cell, origin_y + row * cell
                    fill = "#284454" if (row, col) in walls else "#101d29"
                    canvas.create_rectangle(x, y, x + cell, y + cell, fill=fill, outline=BACKGROUND)
            trail = [initial["position"]] + [step["transition"]["next_position"] for step in report["steps"]]
            coordinates = [(origin_x + (col + .5) * cell, origin_y + (row + .5) * cell) for row, col in trail]
            if len(coordinates) > 1:
                canvas.create_line(*[coordinate for point in coordinates for coordinate in point], fill=CYAN, width=4)
            for location, color, radius in ((initial["goal"], "#ff8b80", .18), (report["position"], MINT, .22)):
                row, col = location
                x, y = origin_x + (col + .5) * cell, origin_y + (row + .5) * cell
                canvas.create_oval(x - cell * radius, y - cell * radius, x + cell * radius, y + cell * radius,
                                   fill=color, outline=color)
            canvas.create_text(72, 104, anchor="w", text=f"{initial['size']} x {initial['size']} maze / loops / seed {initial['seed']}",
                               fill=MUTED, font=("DejaVu Sans", 11))
            for index, direction in enumerate(("north", "east", "south", "west")):
                y = 170 + index * 45
                probability = report["probabilities"].get(direction)
                canvas.create_text(650, y, anchor="w", text=direction.title(), fill=MUTED, font=("DejaVu Sans", 11))
                canvas.create_line(718, y, 882, y, fill="#284454", width=7)
                if probability is not None:
                    canvas.create_line(718, y, 718 + 164 * probability, y, fill=MINT, width=7)
                    canvas.create_text(950, y, anchor="e", text=f"{probability:.1%}", fill=MINT, font=("DejaVu Sans", 10))
            facts = [f"Steps                 {len(report['steps'])}", f"Collisions            {report['collisions']}",
                     f"Model calls          {report['inference_calls']}", "Quantization        INT8 + FP32"]
            if report["inferences"]:
                facts.append(f"Last inference      {report['inferences'][-1]['seconds']:.1f} s")
            for index, text in enumerate(facts):
                canvas.create_text(650, 372 + index * 29, anchor="w", text=text, fill=FOREGROUND,
                                   font=("DejaVu Sans", 12))
        canvas.create_text(650, 550, anchor="w", width=305, text=status, fill=MINT, font=("DejaVu Sans", 11))
        canvas.create_text(72, 588, anchor="w", text="Model judgments and verified movement memory", fill=MUTED,
                           font=("DejaVu Sans", 10))

    def poll(self) -> None:
        try:
            while True:
                report, status = self.events.get_nowait()
                became_ready = report is not None and not self.ready
                if report is not None:
                    self.ready = True
                self.draw(report, status)
                if became_ready:
                    self.root.update_idletasks()
                    print(f"NANOJEV_READY device=cpu load_seconds={report['load_seconds']:.3f}", flush=True)
        except Empty:
            pass
        self.root.after(100, self.poll)

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    DemoWindow().run()
