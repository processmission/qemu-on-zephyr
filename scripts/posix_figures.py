# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
import pandas as pd
from lxml import etree
from docutils import nodes
from docutils.core import publish_doctree
from docutils.parsers.rst import roles


ROOT = Path(__file__).resolve().parent.parent
REVISION = "ba25413e5b6b2661a71db24888f6b89274f1480d"
IMPLEMENTATION = "2b1161806afe14fe57dec97234f37aac1ea84e5e"
CATALOG = ROOT / "upstream/zephyr/doc/services/portability/posix/option_groups/index.rst"
AUDIT = ROOT / "docs/data/qemu-posix-audit.csv"


@dataclass(frozen=True)
class Category:
    key: str
    label: str
    color: str
    hatch: str


DOCUMENTATION = (
    Category("unqualified_yes", "Unqualified yes", "#0072B2", ""),
    Category("qualified", "Qualified / undefined", "#E69F00", "//"),
    Category("unmarked", "Unmarked", "#999999", ".."),
)
PORT = (
    Category("reused", "Reusable Zephyr POSIX", "#0072B2", ""),
    Category("adapted", "Zephyr-side adaptation", "#009E73", "//"),
    Category("limited", "stat/fstat metadata limits", "#E69F00", ".."),
    Category("unavailable", "Process/signal interfaces unavailable", "#D55E00", "xx"),
)
PORT_FIGURE = (
    Category("reused", "Reusable Zephyr POSIX", "#0072B2", ""),
    Category("adapted", "Zephyr-side adaptation", "#009E73", "//"),
    Category("limited_or_unavailable", "Limited or unavailable", "#D55E00", "xx"),
)


def inline_role(name, rawtext, text, lineno, inliner, options=None, content=None):
    return [nodes.inline(rawtext, text)], []


def verify_pinned_source(path: Path) -> None:
    zephyr = ROOT / "upstream/zephyr"
    repository, revision = (zephyr, REVISION) if path.is_relative_to(zephyr) else (ROOT, IMPLEMENTATION)
    expected = subprocess.run(
        ["git", "-C", str(repository), "show", f"{revision}:{path.relative_to(repository)}"],
        check=True, capture_output=True,
    ).stdout
    if path.read_bytes() != expected:
        raise ValueError(f"Source changed since the reviewed baseline; review the ledger: {path}")


def documentation_inventory() -> tuple[pd.DataFrame, list[dict[str, str]], int]:
    actual_revision = subprocess.run(
        ["git", "-C", str(ROOT / "upstream/zephyr"), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if actual_revision != REVISION:
        raise ValueError(f"Expected Zephyr {REVISION}; found {actual_revision}")
    verify_pinned_source(CATALOG)
    for role in ("ref", "kconfig:option"):
        roles.register_local_role(role, inline_role)
    document = publish_doctree(
        CATALOG.read_text(encoding="utf-8"),
        source_path=str(CATALOG),
        settings_overrides={"halt_level": 2, "report_level": 2},
    )
    rows = []
    excluded = []
    tables = list(document.findall(nodes.table))
    for table in tables:
        group = next(table.findall(nodes.title)).astext()
        for body in table.findall(nodes.tbody):
            for row in body.findall(nodes.row):
                cells = [entry.astext().strip() for entry in row.findall(nodes.entry)]
                if len(cells) != 2:
                    raise ValueError(f"Expected API and Supported cells: {cells}")
                api, status = cells
                record = {"api": api, "group": group, "support_cell": status}
                if api.endswith("()"):
                    record["api"] = api[:-2]
                    rows.append(record)
                else:
                    excluded.append(record)
    frame = pd.DataFrame(rows)
    unique = []
    for api, group in frame.groupby("api", sort=True):
        statuses = set(group["support_cell"])
        allowed = {"", "yes", "yes (UTC timezone only)"}
        if any(status not in allowed and "†" not in status for status in statuses):
            raise ValueError(f"Unrecognized status for {api}: {statuses}")
        if any("†" in status or "(" in status for status in statuses):
            category = "qualified"
        elif statuses == {"yes"}:
            category = "unqualified_yes"
        elif statuses == {""}:
            category = "unmarked"
        else:
            raise ValueError(f"Conflicting status for {api}: {statuses}")
        unique.append({
            "api": api,
            "category": category,
            "occurrences": len(group),
            "groups": "; ".join(group["group"]),
            "support_cells": json.dumps(list(group["support_cell"]), ensure_ascii=False),
        })
    return pd.DataFrame(unique), excluded, len(tables)


def port_inventory() -> pd.DataFrame:
    frame = pd.read_csv(AUDIT, keep_default_na=False)
    if frame["api"].duplicated().any():
        raise ValueError("Duplicate API in the host-interface audit")
    if not set(frame["category"]).issubset({category.key for category in PORT}):
        raise ValueError("Unknown host-interface category")
    if set(frame["scope"]) != {"selected", "extension"}:
        raise ValueError("The audit must identify selected interfaces and extensions")
    for row in frame.itertuples(index=False):
        if not (ROOT / row.reference).is_file():
            raise ValueError(f"Missing source for {row.api}: {row.reference}")
        if row.scope == "extension" and row.category != "unavailable":
            raise ValueError(f"Review the extension classification: {row.api}")
    for reference in set(frame["reference"]):
        verify_pinned_source(ROOT / reference)
    return frame


def counts(frame: pd.DataFrame, categories: tuple[Category, ...]) -> list[int]:
    totals = frame["category"].value_counts()
    return [int(totals.get(category.key, 0)) for category in categories]


def draw_pie(ax, values: list[int], categories: tuple[Category, ...], title: str):
    total = sum(values)
    labels = [f"{value}\n{value / total:.1%}" for value in values]
    wedges, _ = ax.pie(
        values,
        startangle=90,
        counterclock=False,
        colors=[category.color for category in categories],
        labels=labels,
        labeldistance=0.66,
        textprops={"fontsize": 9, "ha": "center", "va": "center", "color": "black",
                   "bbox": {"facecolor": "white", "edgecolor": "none", "pad": 1.8}},
        wedgeprops={"linewidth": 0.6, "edgecolor": "#222222"},
    )
    for wedge, category in zip(wedges, categories, strict=True):
        wedge.set_hatch(category.hatch)
    ax.set_title(title, fontsize=10, fontweight="bold", pad=9)
    ax.legend(wedges, [category.label for category in categories], loc="upper center",
              bbox_to_anchor=(0.5, -0.06), frameon=False, fontsize=8, handlelength=1.7)
    ax.set_aspect("equal")


def render_figures(output: Path, catalog: pd.DataFrame, audit: pd.DataFrame) -> list[str]:
    style = {
        "font.family": "DejaVu Sans", "font.size": 9,
        "svg.fonttype": "none", "pdf.fonttype": 42,
        "figure.facecolor": "white", "savefig.facecolor": "white",
        "hatch.linewidth": 0.5,
    }
    paths = []
    with mpl.rc_context(style):
        fig, axes = plt.subplots(1, 2, figsize=(180 / 25.4, 112 / 25.4))
        fig.subplots_adjust(left=0.035, right=0.965, top=0.87, bottom=0.26, wspace=0.3)
        draw_pie(axes[0], counts(catalog, DOCUMENTATION), DOCUMENTATION,
                 f"A  Zephyr documentation (n={len(catalog)})")
        audit_plot = audit.copy()
        audit_plot["category"] = audit_plot["category"].replace(
            {"limited": "limited_or_unavailable", "unavailable": "limited_or_unavailable"})
        draw_pie(axes[1], counts(audit_plot, PORT_FIGURE), PORT_FIGURE,
                 f"B  QEMU host POSIX interfaces (n={len(audit)})")
        fig.text(0.5, 0.98, "POSIX interface inventories", ha="center", va="top",
                 fontsize=12, fontweight="bold")
        fig.text(0.5, 0.01, "Interface counts; each panel has its own denominator. No conformance score.",
                 ha="center", va="bottom", fontsize=7.5)
        for suffix in ("svg", "png"):
            path = output / f"posix-support.{suffix}"
            fig.savefig(path, dpi=300)
            if suffix == "svg":
                document = etree.parse(path, etree.XMLParser(remove_blank_text=True, resolve_entities=False))
                document.write(path, encoding="utf-8", xml_declaration=True, pretty_print=True)
            paths.append(str(path.relative_to(ROOT)))
        plt.close(fig)
    return paths


def main() -> None:
    parser = argparse.ArgumentParser(description="Reproduce the manuscript's POSIX inventories and plots")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/figures")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    catalog, excluded, table_count = documentation_inventory()
    audit = port_inventory()
    data_dir = ROOT / "docs/data"
    catalog.to_csv(data_dir / "zephyr-posix-catalog.csv", index=False)
    outputs = render_figures(args.output, catalog, audit)
    manifest = {
        "zephyr_revision": REVISION,
        "implementation_revision": IMPLEMENTATION,
        "source": str(CATALOG.relative_to(ROOT)),
        "source_sha256": hashlib.sha256(CATALOG.read_bytes()).hexdigest(),
        "audit_sha256": hashlib.sha256(AUDIT.read_bytes()).hexdigest(),
        "audit_source_sha256": {
            reference: hashlib.sha256((ROOT / reference).read_bytes()).hexdigest()
            for reference in sorted(set(audit["reference"]))
        },
        "table_count": table_count,
        "callable_rows": int(catalog["occurrences"].sum()),
        "unique_catalog_count": len(catalog),
        "excluded_rows": excluded,
        "documentation_counts": dict(zip((c.key for c in DOCUMENTATION), counts(catalog, DOCUMENTATION))),
        "audit_counts": dict(zip((c.key for c in PORT), counts(audit, PORT))),
        "audit_scope_counts": {str(key): int(value) for key, value in audit["scope"].value_counts().items()},
        "percentage_rule": "100 * category interface count / panel interface count; one decimal place",
        "physical_size_mm": [180, 112],
        "png_dpi": 300,
        "svg_serialization": "XML attribute whitespace normalization and indentation; geometry and labels retained",
        "packages": {name: importlib.metadata.version(name) for name in ("matplotlib", "pandas", "docutils", "lxml")},
        "outputs": outputs,
    }
    (data_dir / "posix-inventory.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"documentation": manifest["documentation_counts"],
                      "host_audit": manifest["audit_counts"],
                      "scope": manifest["audit_scope_counts"]}, indent=2))


if __name__ == "__main__":
    main()
