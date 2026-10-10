# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import math
from pathlib import Path
import platform
import resource
import time

from evaluate_model_edges_maze import EdgeExplorer, MazeEnvironment
from predict_toy_decisions import DecisionPredictor
from scaled_maze import DIRECTIONS, make_maze


class MazeSession:
    def __init__(self, checkpoint: Path, size: int = 16, seed: int = 17):
        import torch
        from torch.ao.quantization import quantize_dynamic
        from torch.ao.nn.quantized.dynamic import Linear

        torch.set_num_threads(1)
        torch.backends.quantized.engine = "qnnpack"
        started = time.monotonic()
        print("NANOJEV_LOADING checkpoint", flush=True)
        self.engine = DecisionPredictor(checkpoint, device_name="cpu", precision="fp32")
        print("NANOJEV_LOADING dynamic-int8", flush=True)
        self.engine.model.backbone = quantize_dynamic(
            self.engine.model.backbone, {torch.nn.Linear}, dtype=torch.qint8, inplace=True)
        self.quantized_modules = sum(isinstance(module, Linear) for module in self.engine.model.modules())
        if self.quantized_modules == 0:
            raise RuntimeError("The CPU model contains no quantized linear modules")
        self.load_seconds = time.monotonic() - started
        self.provenance = json.loads((checkpoint / "provenance.json").read_text())
        self.reset(size, seed)

    def reset(self, size: int, seed: int) -> None:
        self.initial = make_maze(size, seed, "loops")
        self.environment = MazeEnvironment(self.initial)
        self.policy = EdgeExplorer(**self.environment.public_coordinates())
        self.steps: list[dict] = []
        self.inferences: list[dict] = []
        self.collisions = 0
        self.probabilities: dict[str, float] = {}
        self.status = "Ready"

    def step(self) -> dict:
        if self.environment.reached_goal() or len(self.steps) >= 2 * self.initial["size"] ** 2:
            return self.report()
        if self.policy.needs_prediction():
            payload = {"states": [{"id": f"position-{self.policy.position}",
                                    **self.environment.observe()}]}
            started = time.monotonic()
            response = self.engine.predict(payload)
            response["execution"].update(parameter_storage="float32+int8", quantization="dynamic-int8",
                                          quantized_linear_modules=self.quantized_modules)
            elapsed = time.monotonic() - started
            answers = response["states"][0]["answers"]
            self.probabilities = {direction: answers["clear_" + direction]["p_true"]
                                  for direction in DIRECTIONS}
            if any(not math.isfinite(value) or not 0 <= value <= 1
                   for value in self.probabilities.values()):
                raise RuntimeError("NanoJev returned invalid safety probabilities")
            self.policy.remember_prediction(self.probabilities)
            self.inferences.append({"request": payload, "response": response, "seconds": elapsed})
            print(f"NANOJEV_INFERENCE call={self.engine.inference_calls} device=cpu seconds={elapsed:.3f}", flush=True)
        else:
            self.probabilities = dict(self.policy.predictions[self.policy.position])
        decision = self.policy.choose()
        if decision is None:
            raise RuntimeError("Maze exploration has no remaining reachable edge")
        transition = self.environment.attempt(decision["action"])
        self.policy.observe_transition(decision["action"], transition["next_position"], transition["collision"])
        self.collisions += int(transition["collision"])
        self.steps.append({"decision": decision, "transition": transition})
        self.status = "Goal reached" if self.environment.reached_goal() else "Exploring"
        if not self.environment.reached_goal() and len(self.steps) >= 2 * self.initial["size"] ** 2:
            self.status = "Step limit reached"
        print(f"NANOJEV_STEP index={len(self.steps)} action={decision['action']} status={self.status}", flush=True)
        return self.report()

    def report(self) -> dict:
        device_tree = Path("/proc/device-tree/model")
        return {
            "model": self.provenance, "device": "cpu", "precision": "fp32",
            "quantization": "dynamic-int8", "quantized_linear_modules": self.quantized_modules,
            "machine": platform.machine(), "kernel": platform.release(),
            "guest_machine": device_tree.read_bytes().rstrip(b"\0").decode() if device_tree.exists() else None,
            "load_seconds": self.load_seconds, "max_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            "initial_state": self.initial, "position": list(self.policy.position),
            "steps": self.steps, "inferences": self.inferences,
            "probabilities": self.probabilities, "collisions": self.collisions,
            "status": self.status, "goal_reached": self.environment.reached_goal(),
            "finished": self.environment.reached_goal() or len(self.steps) >= 2 * self.initial["size"] ** 2,
            "model_loads": 1, "inference_calls": self.engine.inference_calls,
        }


def save_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".part")
    temporary.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run NanoJev CPU maze decisions using local model weights.")
    parser.add_argument("--checkpoint", type=Path, default=Path("/opt/nanojev/checkpoint"))
    parser.add_argument("--size", type=int, default=16)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--steps", type=int, default=1)
    parser.add_argument("--output", type=Path, default=Path("/run/nanojev/report.json"))
    args = parser.parse_args()
    if args.steps < 1 or not 5 <= args.size <= 50:
        parser.error("Use a positive step count and maze size between 5 and 50")
    session = MazeSession(args.checkpoint, args.size, args.seed)
    print(f"NANOJEV_READY device=cpu load_seconds={session.load_seconds:.3f}", flush=True)
    for _ in range(args.steps):
        report = session.step()
        save_report(args.output, report)
        if report["finished"]:
            break


if __name__ == "__main__":
    main()
