"""Reopen an original-template report with two independent PDF text parsers.

Only layout whitespace is ignored. Unicode normalization, compatibility-radical
substitution and a successful visual render must never turn a text failure into
a pass. Inputs are anonymous sn100-render-snapshot-v1 files, not databases.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

import pdfplumber
import pypdf


def compact(text):
    return "".join(character for character in text if not character.isspace())


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expectations(snapshot):
    headers = snapshot["headers"]
    labels = {
        "name": "姓名：", "sex": "性别：", "age": "年龄：",
        "bed_number": "床号：", "department": "科室：",
        "sample_provider": "送检医生：", "sample_sn": "样本序号：",
        "patient_type": "病人类型：", "report_date": "报告日期：",
        "mark": "备注：", "checker": "检验医生：", "reviewer": "审核医生：",
    }
    fields = {key: label + headers[key] for key, label in labels.items()}
    fields["hospital_name"] = headers["hospital_name"] + "检验报告单"
    rows = ["".join(row[key] for key in (
        "project", "project_name", "project_result", "reference_range", "unit"))
        for row in snapshot["rows"]]
    return fields, rows


def check_parser(name, pages, snapshot, expected_pages):
    fields, expected_rows = expectations(snapshot)
    page_text = [compact(page["text"]) for page in pages]
    all_text = "".join(page_text)
    failures = []
    if len(pages) != expected_pages:
        failures.append(f"page_count: expected {expected_pages}, actual {len(pages)}")
    field_checks = {}
    for key, expected in fields.items():
        found = compact(expected) in all_text
        field_checks[key] = {"expected": expected, "found_exact_unicode": found}
        if not found:
            failures.append(f"header_or_master_footer:{key}")

    # These assertions compare whole source rows, not just plausible numeric
    # substrings elsewhere in the document. They also retain task order across
    # page breaks, even when page titles/footers occur between two data rows.
    cursor = 0
    row_checks = []
    for index, row in enumerate(expected_rows):
        found = all_text.find(compact(row), cursor)
        row_checks.append({"row": index + 1, "expected": row,
                           "found_in_order_exact_unicode": found >= 0})
        if found < 0:
            failures.append(f"row:{index + 1}")
        else:
            cursor = found + len(compact(row))
    for row, count in Counter(compact(row) for row in expected_rows).items():
        if all_text.count(row) != count:
            failures.append(f"whole_row_count:{row!r}:expected={count},actual={all_text.count(row)}")

    # Each numeric result must have exactly its source multiplicity, bounded
    # against adjacent digits/dots so 0.00 cannot pass as part of 10.00.
    numeric_checks = []
    for value, count in Counter(row["project_result"] for row in snapshot["rows"]).items():
        actual = len(re.findall(r"(?<![0-9.])" + re.escape(compact(value)) + r"(?![0-9.])", all_text))
        numeric_checks.append({"expected_value": value, "expected_count": count, "actual_count": actual})
        if actual != count:
            failures.append(f"result_count:{value!r}:expected={count},actual={actual}")

    footer_checks = []
    repeated_field_checks = []
    master_footer_keys = ("report_date", "mark", "checker", "reviewer")
    column_labels = ("项目代号", "项目名称", "结果", "参考范围", "单位")
    for index, text in enumerate(page_text):
        expected = f"第{index + 1}页共{expected_pages}页"
        passed = text.count(expected) == 1
        footer_checks.append({"page": index + 1, "expected": expected, "found_once_exact_unicode": passed})
        if not passed:
            failures.append(f"page_footer:{index + 1}")
        checks = {}
        # Original ReportTitle belongs to page 1; MasterHeader/MasterFooter
        # repeat on every page. Enforce those actual template bands rather
        # than incorrectly demanding the patient's title on later pages.
        for key, expected_field in fields.items():
            if index > 0 and key not in master_footer_keys:
                continue
            found = compact(expected_field) in text
            checks[key] = found
            if not found:
                failures.append(f"page_{index + 1}_field:{key}")
        for label in column_labels:
            found = label in text
            checks["column:" + label] = found
            if not found:
                failures.append(f"page_{index + 1}_column:{label}")
        if index == 0:
            checks["category"] = "类别：血凝" in text
            if not checks["category"]:
                failures.append("page_1_category")
        repeated_field_checks.append({"page": index + 1, "checks": checks})
        page = pages[index]
        if abs(page["width_pt"] - 210 * 72 / 25.4) > 1 or abs(page["height_pt"] - 148 * 72 / 25.4) > 1:
            failures.append(f"page_geometry:{index + 1}")
    for label in ("类别：血凝", "项目代号", "项目名称", "结果", "参考范围", "单位"):
        if label not in all_text:
            failures.append(f"static_label:{label}")
    if "\ufffd" in all_text:
        failures.append("unicode_replacement_character")
    return {"parser": name, "pages": pages, "field_checks": field_checks,
            "row_checks": row_checks, "numeric_checks": numeric_checks,
            "footer_checks": footer_checks, "repeated_field_checks": repeated_field_checks, "failures": failures,
            "passed": not failures}


def main():
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--pdf", type=Path, required=True)
    arguments.add_argument("--snapshot", type=Path, required=True)
    arguments.add_argument("--expected-pages", type=int, required=True)
    arguments.add_argument("--output", type=Path, required=True)
    options = arguments.parse_args()
    snapshot = json.loads(options.snapshot.read_text(encoding="utf-8-sig"))
    if snapshot.get("schema") != "sn100-render-snapshot-v1":
        raise ValueError("Unsupported snapshot schema")
    result = {"schema": "sn100-pdf-exact-text-v1", "pdf_sha256": sha256(options.pdf),
              "snapshot_sha256": sha256(options.snapshot), "expected_pages": options.expected_pages,
              "comparison": "exact Unicode; ignore layout whitespace only; no normalization or radical substitution",
              "parsers": []}
    reader = pypdf.PdfReader(options.pdf, strict=True)
    pages = [{"width_pt": float(page.mediabox.width), "height_pt": float(page.mediabox.height),
              "text": page.extract_text() or ""} for page in reader.pages]
    result["parsers"].append(check_parser("pypdf " + pypdf.__version__, pages, snapshot, options.expected_pages))
    with pdfplumber.open(options.pdf, unicode_norm=None) as pdf:
        pages = [{"width_pt": page.width, "height_pt": page.height,
                  # The fixed template naturally wraps report_date. Text-flow
                  # order keeps that field together instead of inserting a
                  # neighbouring doctor's line between its date and time.
                  # pdfplumber otherwise expands actual U+FB03/U+FB01 into
                  # ASCII ffi/fi when building words, despite retaining the
                  # exact code points in page.chars. Disable that substitution
                  # so the independent parser checks the PDF's Unicode values.
                  "text": page.extract_text(x_tolerance=1, y_tolerance=3, use_text_flow=True,
                                            expand_ligatures=False) or "",
                  "text_geometric_order": page.extract_text(x_tolerance=1, y_tolerance=3,
                                                            expand_ligatures=False) or ""}
                 for page in pdf.pages]
    result["parsers"].append(check_parser("pdfplumber " + pdfplumber.__version__, pages, snapshot, options.expected_pages))
    result["passed"] = all(parser["passed"] for parser in result["parsers"])
    # The exact raw parser strings remain readable in this evidence. Printing
    # only ASCII-safe summary avoids a host console codec changing the result.
    options.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "parsers": [
        {"name": parser["parser"], "failures": parser["failures"]} for parser in result["parsers"]]}, ensure_ascii=True))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
