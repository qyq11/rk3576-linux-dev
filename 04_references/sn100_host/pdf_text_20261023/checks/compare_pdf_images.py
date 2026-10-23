"""Same-tool, same-DPI whole-page PDF pixel comparison; creates no PDFs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from PIL import Image, ImageChops
from pypdf import PdfReader


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--pdftoppm", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dpi", type=int, default=120)
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=False)
    version = subprocess.run([str(options.pdftoppm), "-v"], capture_output=True, timeout=10)
    result = {"before_sha256": sha(options.before), "after_sha256": sha(options.after),
              "pdftoppm_sha256": sha(options.pdftoppm), "dpi": options.dpi,
              "renderer_version": (version.stdout + version.stderr).decode("utf-8", errors="replace"),
              "page_count_before": len(PdfReader(options.before).pages),
              "page_count_after": len(PdfReader(options.after).pages), "pages": []}
    for name, source in (("before", options.before), ("after", options.after)):
        directory = options.output / name
        directory.mkdir()
        start = time.perf_counter()
        process = subprocess.run([str(options.pdftoppm), "-png", "-r", str(options.dpi),
                                  str(source.resolve()), str((directory / "page").resolve())],
                                 capture_output=True, timeout=60)
        (directory / "render_log.txt").write_bytes(process.stdout + process.stderr)
        result[name + "_render"] = {"elapsed_ms": (time.perf_counter() - start) * 1000,
                                     "exit_code": process.returncode}
        if process.returncode:
            raise RuntimeError(f"{name} rendering failed: {process.returncode}")
    # pdftoppm numbers may be padded when the source has many pages.
    def rendered(name):
        return sorted((options.output / name).glob("page-*.png"), key=lambda path: int(path.stem.split("-")[-1]))
    before = rendered("before")
    after = rendered("after")
    for index, (old, new) in enumerate(zip(before, after)):
        with Image.open(old) as image:
            old_image = image.convert("RGB")
        with Image.open(new) as image:
            new_image = image.convert("RGB")
        entry = {"page": index + 1, "before_size": old_image.size, "after_size": new_image.size,
                 "before_png_sha256": sha(old), "after_png_sha256": sha(new)}
        entry["same_dimensions"] = old_image.size == new_image.size
        if entry["same_dimensions"]:
            difference = ImageChops.difference(old_image, new_image)
            entry["identical_pixels"] = difference.getbbox() is None
            entry["different_pixel_count"] = sum(1 for pixel in difference.getdata() if pixel != (0, 0, 0))
            entry["maximum_channel_difference"] = max(high for low, high in difference.getextrema())
            if not entry["identical_pixels"]:
                difference.save(options.output / f"difference_page_{index + 1}.png")
        else:
            entry["identical_pixels"] = False
        result["pages"].append(entry)
    result["passed"] = (len(before) == len(after) == result["page_count_before"] == result["page_count_after"]
                        and all(page["identical_pixels"] for page in result["pages"]))
    (options.output / "comparison.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "page_count": len(before),
                      "different_pixel_counts": [page.get("different_pixel_count") for page in result["pages"]]}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
