"""Re-open original-template UTF-8 HTML independently of Qt's parser.

Only hand-written anonymous snapshots are accepted by the caller. This parser
checks fields and document completion, not Windows/browser visual equivalence.
"""
import argparse
import hashlib
import json
from html.parser import HTMLParser
from pathlib import Path


class Labels(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.labels = []
        self.current = None
        self.meta_utf8 = False
        self.closed = False

    def handle_starttag(self, tag, attrs):
        if tag == "meta":
            self.meta_utf8 = dict(attrs).get("charset", "").lower() == "utf-8"
        if tag == "div":
            assert self.current is None, "Unexpected nested label"
            self.current = []

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)

    def handle_endtag(self, tag):
        if tag == "div":
            assert self.current is not None, "Unbalanced label"
            self.labels.append("".join(self.current))
            self.current = None
        if tag == "html":
            self.closed = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--html", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    raw = args.html.read_bytes()
    text = raw.decode("utf-8", errors="strict")
    source = json.loads(args.snapshot.read_text(encoding="utf-8"))
    assert source["schema"] == "sn100-render-snapshot-v1"
    reader = Labels()
    reader.feed(text)
    reader.close()
    assert reader.closed and reader.meta_utf8 and reader.current is None
    source_fields = list(source["headers"].items())
    for index, row in enumerate(source["rows"]):
        source_fields.extend((f"row[{index}].{key}", value) for key, value in row.items())
    checked = []
    for key, value in source_fields:
        if not value.strip():
            continue
        assert any(value in label for label in reader.labels), f"Missing or altered field {key}"
        checked.append(key)
    results = [row["project_result"] for row in source["rows"]]
    actual_results = [label for label in reader.labels if label in results]
    for value in set(results):
        assert actual_results.count(value) == results.count(value), f"Missing/duplicate result {value}"
    result = {
        "passed": True,
        "parser": "Python standard-library HTMLParser, strict UTF-8",
        "html_sha256": hashlib.sha256(raw).hexdigest(),
        "snapshot_sha256": hashlib.sha256(args.snapshot.read_bytes()).hexdigest(),
        "result_rows": len(source["rows"]),
        "label_count": len(reader.labels),
        "checked_nonempty_fields": checked,
        "note": "Original HTML is a continuous absolute-positioned document; PDF page parity and browser visual QA remain separate.",
    }
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
        output.write("\n")
    print(json.dumps({"passed": True, "result_rows": len(source["rows"]), "labels": len(reader.labels)}))


if __name__ == "__main__":
    main()
