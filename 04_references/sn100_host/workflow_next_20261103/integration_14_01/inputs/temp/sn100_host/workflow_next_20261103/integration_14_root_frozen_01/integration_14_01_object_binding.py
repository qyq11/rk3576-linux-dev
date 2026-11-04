"""只读绑定实际对象和直接生成输入；来源按冻结脚本重建，不是编译器.d。"""
from pathlib import Path, PurePosixPath
import fnmatch
import hashlib
import json
import os
import re
import xml.etree.ElementTree as ET

_KINDS = ("source", "form", "header", "resource", "helper_source")
_ARRAY_NAMES = ("sources", "forms", "headers", "resource_names", "resource_files", "helper_sources")
_ARRAY = re.compile(r"^(sources|forms|headers|resource_names|resource_files|helper_sources)(\+)?=\((.*?)\)[ \t]*(?:\n|$)", re.M | re.S)
_INVENTORY_START = 'for input in "${sources[@]}" "${forms[@]}" "${headers[@]}" "${resource_files[@]}" "${helper_sources[@]}"; do'
_SHA = re.compile(r"[a-f0-9]{64}\Z")

# 只允许路径列表变化；控制流程按可读模板核对，不以某次build.sh的SHA替代规则审查。
_KNOWN_RULE_SKELETON = r'''set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: shell/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
list_inputs=${SN100_ARM64_LIST_INPUTS:-0}
[[ $list_inputs == 0 || $list_inputs == 1 ]] || { echo 'SN100_ARM64_LIST_INPUTS must be 0 or 1.' >&2; exit 2; }
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/arm64_sysroot}
[[ $sysroot == /* ]] || { echo 'SN100_ARM64_SYSROOT must be absolute.' >&2; exit 2; }
if [[ $list_inputs == 0 ]]; then
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || bash "$program_dir/prepare_arm64.sh"
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 ]] || {
echo 'Report ARM64 inputs missing; use shell/rk.sh prepare to restore the isolated sysroot.' >&2; exit 1;
}
[[ -f $sysroot/usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer && -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Svg.so ]] || {
echo 'Sample UI ARM64 SVG inputs missing; restore matching archived Qt5.15.13 SVG headers/runtime before building.' >&2; exit 1;
}
fi
include=$(readlink -f -- "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
build_dir=${SN100_ARM64_BUILD_DIR:-$root/temp/sn100_host/integration/arm64_build}
[[ $build_dir == /* ]] || { echo 'SN100_ARM64_BUILD_DIR must be absolute.' >&2; exit 2; }
if [[ $list_inputs == 0 ]]; then
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This ARM64 build directory is already in use.' >&2; exit 1; }
fi
case ${RK_BUILD_TYPE:-Release} in
Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
*) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
qt_tools=/usr/lib/qt5/bin
vendor=$program_dir/thirdparty/qtrpt
include_flags=(-I"$module_dir" -I"$program_dir" -I"$program_dir/ui" -I"$program_dir/history" -I"$build_dir" -I"$module_dir/widgets" -I"$module_dir/widgets/material")
include_flags+=(-I"$vendor/QtRPT" -I"$vendor/CommonFiles")
include_flags+=(-I"$program_dir/projects" -I"$program_dir/reagents" -I"$program_dir/samples" -I"$program_dir/samples/material")
include_flags+=(-I"$program_dir/instrument")
include_flags+=(-I"$program_dir/quality_control")
include_flags+=(-I"$program_dir/calibration")
include_flags+=(-I"$program_dir/alarm_history" -I"$program_dir/alarm_history/calendar")
include_flags+=(-I"$program_dir/lis_raw_inbox")
modules=(QtCore QtGui QtWidgets QtSql QtXml QtPrintSupport QtQml QtNetwork QtCharts QtSvg)
for module in "${modules[@]}"; do include_flags+=(-I"$include/$module"); done
is_report_helper_source()
{
case $1 in
"$vendor/"*|*/history_value_text.cpp|*/report_document.cpp|*/report_render.cpp|*/qrc_report_resources.cpp|*/qrc_imagesRpt.cpp)
return 0 ;;
esac
return 1
}
for input in "${sources[@]}" "${forms[@]}" "${headers[@]}" "${resource_files[@]}" "${helper_sources[@]}"; do
[[ -f $input ]] || { echo "Required ARM64 build input missing: $input" >&2; exit 1; }
done
if [[ $list_inputs == 1 ]]; then
printf 'source\t%s\n' "${sources[@]}"
printf 'form\t%s\n' "${forms[@]}"
printf 'header\t%s\n' "${headers[@]}"
printf 'resource\t%s\n' "${resource_files[@]}"
printf 'include\t%s\n' "${include_flags[@]}"
for source in "${sources[@]}"; do
if is_report_helper_source "$source"; then printf 'helper_source\t%s\n' "$source"; fi
done
printf 'helper_source\t%s\n' "${helper_sources[@]}"
exit 0
fi
for form in "${forms[@]}"; do
name=$(basename -- "${form%.ui}")
"$qt_tools/uic" "$form" -o "$build_dir/ui_$name.h"
done
for header in "${headers[@]}"; do
if grep -q 'Q_OBJECT' "$header"; then
name=$(basename -- "${header%.h}")
generated=$build_dir/moc_$name.cpp
"$qt_tools/moc" -DNO_BARCODE -DHAVE_QT5 -I"$include" "${include_flags[@]}" "$header" -o "$generated"
sources+=("$generated")
fi
done
for index in "${!resource_files[@]}"; do
name=${resource_names[index]}
"$qt_tools/rcc" -name "$name" "${resource_files[index]}" -o "$build_dir/qrc_$name.cpp"
sources+=("$build_dir/qrc_$name.cpp")
done
output=$(realpath -m -- "$1")
renderer_output=$(dirname -- "$output")/sn100_report_renderer
[[ $output != "$renderer_output" ]] || { echo 'The main output must not use the reserved helper name.' >&2; exit 2; }
temporary=$output.new
renderer_temporary=$renderer_output.new
trap 'rm -f -- "$temporary" "$renderer_temporary"' EXIT
jobs=${SN100_JOBS:-2}
[[ $jobs =~ ^[0-9]+$ && $jobs -ge 1 && $jobs -le 2 ]] || { echo 'SN100_JOBS must be 1 or 2 for the current WSL build budget.' >&2; exit 2; }
compile_flags=(-std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra -DNO_BARCODE -DHAVE_QT5 "${include_flags[@]}"
-isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtGui"
-isystem "$include/QtWidgets" -isystem "$include/QtSql")
objects=()
build_pids=()
declare -A reusable=()
if [[ -n ${SN100_ARM64_OBJECT_REUSE_MANIFEST:-} ]]; then
[[ $SN100_ARM64_OBJECT_REUSE_MANIFEST == /* && -n ${SN100_ARM64_FROZEN_SNAPSHOT:-} ]] || {
echo 'Explicit object reuse requires absolute manifest and frozen snapshot paths.' >&2; exit 2;
}
python3 "$module_dir/arm64_object_reuse.py" reusable "$SN100_ARM64_OBJECT_REUSE_MANIFEST" \
"$SN100_ARM64_FROZEN_SNAPSHOT" "$program_dir" "$build_dir" aarch64-linux-gnu-g++ "${RK_BUILD_TYPE:-Release}" \
--flags "${compile_flags[@]}" --sources "${sources[@]}" > "$build_dir/reusable_indices.txt"
while read -r index; do reusable[$index]=1; done < "$build_dir/reusable_indices.txt"
printf 'VERIFIED_OBJECT_REUSE=%s\n' "${#reusable[@]}"
fi
wait_batch()
{
local failed=0
for task_pid in "${build_pids[@]}"; do
wait "$task_pid" || failed=1
done
build_pids=()
return "$failed"
}
for source in "${sources[@]}"; do
index=${#objects[@]}
object=$build_dir/obj_$index.o
objects+=("$object")
if [[ ${reusable[$index]+exists} ]]; then continue; fi
aarch64-linux-gnu-g++ "${compile_flags[@]}" -c "$source" -o "$object" &
build_pids+=("$!")
if [[ ${#build_pids[@]} -ge $jobs ]]; then wait_batch; fi
done
wait_batch
link_flags=(
-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" \
-Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
-lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -lQt5Svg "${link_flags[@]}" -o "$temporary"
renderer_objects=()
for index in "${!sources[@]}"; do
source=${sources[index]}
if is_report_helper_source "$source"; then renderer_objects+=("${objects[index]}"); fi
done
for header in "$vendor/QtRPT/"*.h "$vendor/CommonFiles/"*.h; do
if ! grep -q 'Q_OBJECT' "$header"; then continue; fi
generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
for index in "${!sources[@]}"; do
[[ ${sources[index]} != "$generated" ]] || {
candidate=${objects[index]}
if [[ " ${renderer_objects[*]} " != *" $candidate "* ]]; then renderer_objects+=("$candidate"); fi
}
done
done
for source in "${helper_sources[@]}"; do
object=$build_dir/helper_$(basename -- "${source%.cpp}").o
aarch64-linux-gnu-g++ "${compile_flags[@]}" -c "$source" -o "$object"
renderer_objects+=("$object")
done
aarch64-linux-gnu-g++ "${renderer_objects[@]}" "${link_flags[@]}" -o "$renderer_temporary"
for candidate in "$temporary" "$renderer_temporary"; do readelf -h "$candidate" | grep -q 'Machine:.*AArch64'; done
chmod +x "$temporary" "$renderer_temporary"
mv -f -- "$renderer_temporary" "$renderer_output"
mv -f -- "$temporary" "$output"
file "$output" "$renderer_output"
sha256sum "$output" "$renderer_output"'''


def _context():
    # common在调用阶段才已定义完毕；模块加载时不互相import或执行阶段。
    from integration_14_01_tool_support import ROOT, PROGRAM, WORK, require_regular, sha
    return Path(ROOT), Path(PROGRAM), Path(WORK), require_regular, sha


def _normalise_rules(text):
    return "\n".join(line.strip() for line in text.splitlines()
                     if line.strip() and not line.lstrip().startswith("#"))


def _local_path(value):
    value = str(value)
    if any(char in value for char in "\0\r\n"):
        raise RuntimeError("构建输入路径含控制字符。")
    if os.name == "nt" and value.startswith("/mnt/c/"):
        value = "C:/" + value[7:]
    elif os.name != "nt" and re.match(r"^[Cc]:[/\\]", value):
        value = "/mnt/c/" + value[3:].replace("\\", "/")
    path = Path(value)
    if not path.is_absolute():
        raise RuntimeError("构建清单必须使用绝对路径：" + value)
    return path


def _regular(path, within):
    _, _, _, require_regular, _ = _context()
    path = _local_path(path)
    require_regular(path, within=within)  # 先核原路径，不能resolve后隐藏链接。
    return path.resolve()


def _module_path(path, program):
    try:
        return path.relative_to(program.resolve()).as_posix()
    except ValueError as error:
        raise RuntimeError("直接构建输入越出正式程序目录：" + str(path)) from error


def _normalise_rows(rows):
    _, program, _, _, _ = _context()
    if not isinstance(rows, dict) or set(rows) != set(_KINDS):
        raise RuntimeError("构建输入必须完整给出五类列表。")
    result = {}
    for kind in _KINDS:
        if not isinstance(rows[kind], list):
            raise RuntimeError("构建输入类型不是列表：" + kind)
        result[kind] = [_regular(path, program) for path in rows[kind]]
    return result


def read_build_rows():
    """只解析实际TSV，不依赖调用者目录；Windows和WSL都返回本地绝对Path。"""
    _, _, work, _, _ = _context()
    path = _regular(work / "build_inputs.tsv", work)
    rows = {kind: [] for kind in _KINDS}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "\t" not in line:
            raise RuntimeError("实际构建清单行缺少类型或路径。")
        kind, value = line.split("\t", 1)
        if kind == "include":
            continue
        if kind not in rows or not value:
            raise RuntimeError("实际构建清单存在未知类型或空路径：" + kind)
        rows[kind].append(_local_path(value))
    return _normalise_rows(rows)


def _expand_path_token(token, program):
    variables = {"program_dir": program, "module_dir": program / "shell", "vendor": program / "thirdparty/qtrpt"}
    match = re.fullmatch(r'"\$(program_dir|module_dir|vendor)(/[^"$`\\\r\n]*)?"([^"$`\\\s;(){}]*)', token)
    if not match:
        raise RuntimeError("未识别的构建路径表达式，未猜测展开：" + token)
    quoted = match.group(2) or ""
    suffix = match.group(3)
    if any(char in quoted for char in "*?[]"):
        raise RuntimeError("带引号的通配路径不在已审规则内：" + token)
    path = Path(str(variables[match.group(1)]) + quoted + suffix)
    if any(char in str(path) for char in "*?[]"):
        if path.name not in ("*.cpp", "*.h") or any(char in str(path.parent) for char in "*?[]"):
            raise RuntimeError("未识别的构建通配规则：" + token)
        matches = sorted((p for p in path.parent.iterdir() if fnmatch.fnmatchcase(p.name, path.name)),
                         key=lambda p: p.name.encode("utf-8"))
        if not matches:
            raise RuntimeError("构建通配没有实际输入：" + token)
        return [_regular(p, program) for p in matches]
    return [_regular(path, program)]


def _is_report_source(path, program):
    vendor = program.resolve() / "thirdparty/qtrpt"
    return (vendor in path.parents or path.name in
            ("history_value_text.cpp", "report_document.cpp", "report_render.cpp",
             "qrc_report_resources.cpp", "qrc_imagesRpt.cpp"))


def _recognise_build(script_text, rows, program):
    if script_text.count(_INVENTORY_START) != 1:
        raise RuntimeError("未识别输入清单出口，未构造对象来源。")
    prefix, suffix = script_text.split(_INVENTORY_START, 1)
    arrays = {name: [] for name in _ARRAY_NAMES}
    initialised = set()
    for match in _ARRAY.finditer(prefix):
        name, append, body = match.groups()
        if not append:
            if name in initialised:
                raise RuntimeError("构建列表重复重置：" + name)
            initialised.add(name)
        elif name not in initialised:
            raise RuntimeError("构建列表在初始化前追加：" + name)
        tokens = re.findall(r'"[^"\n]*"[^\s]*|[^\s]+', body)
        if name == "resource_names":
            if not all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) for value in tokens):
                raise RuntimeError("资源生成名字不是已审的简单常量。")
            arrays[name].extend(tokens)
        else:
            for token in tokens:
                arrays[name].extend(_expand_path_token(token, program))
    if initialised != set(_ARRAY_NAMES):
        raise RuntimeError("构建脚本缺少完整的直接输入列表。")
    skeleton = _normalise_rules(_ARRAY.sub("", prefix) + _INVENTORY_START + suffix)
    if skeleton != _KNOWN_RULE_SKELETON:
        raise RuntimeError("生成、编译或链接控制规则发生变化，须先审查新规则。")
    declared = {"source": arrays["sources"], "form": arrays["forms"], "header": arrays["headers"],
                "resource": arrays["resource_files"],
                "helper_source": [p for p in arrays["sources"] if _is_report_source(p, program)] + arrays["helper_sources"]}
    if declared != rows:
        raise RuntimeError("冻结build.sh声明的实际列表与build_inputs.tsv不一致。")
    for name in ("sources", "forms", "headers", "resource_files", "helper_sources", "resource_names"):
        if len(set(arrays[name])) != len(arrays[name]):
            raise RuntimeError("构建输入或生成名字重复，未接受模糊映射：" + name)
    if set(arrays["helper_sources"]) & set(arrays["sources"]):
        raise RuntimeError("独立helper输入与主源重复，不在当前已审计数规则内。")
    if len(arrays["resource_names"]) != len(rows["resource"]):
        raise RuntimeError("资源名与qrc列表长度不一致。")
    return arrays, skeleton


def _qrc_inputs(qrc, program):
    """只读qrc及其直接文件；不解析父审核JSON，不访问任何网络。"""
    raw = qrc.read_bytes()
    if re.search(br"<!\s*(DOCTYPE|ENTITY)", raw, re.I):
        raise RuntimeError("qrc含未支持的DTD或实体：" + str(qrc))
    try:
        tree = ET.fromstring(raw)
    except ET.ParseError as error:
        raise RuntimeError("qrc XML无法解析：" + str(qrc)) from error
    if tree.tag != "RCC" or set(tree.attrib) - {"version"}:
        raise RuntimeError("未识别的qrc根结构：" + str(qrc))
    inputs, seen = [qrc], {qrc}
    for group in tree:
        if group.tag != "qresource" or set(group.attrib) - {"prefix", "lang"}:
            raise RuntimeError("未识别的qresource结构：" + str(qrc))
        for node in group:
            if node.tag != "file" or list(node) or set(node.attrib) - {"alias", "compress", "threshold", "compression-algorithm"}:
                raise RuntimeError("未识别的qrc文件条目：" + str(qrc))
            value = node.text or ""
            if (not value or value != value.strip() or any(ord(c) < 32 for c in value)
                    or "\\" in value or ":" in value or PurePosixPath(value).is_absolute()):
                raise RuntimeError("qrc资源必须是明确的本地相对文件：" + repr(value))
            path = _regular(qrc.parent / value, program)
            if path not in seen:
                seen.add(path)
                inputs.append(path)
    return inputs


def create_binding(snapshot, rows, object_counts):
    """纯读并返回稳定JSON值；不写文件、不生成代码、不运行编译器。"""
    root, program, work, require_regular, sha = _context()
    root, program, work = root.resolve(), program.resolve(), work.resolve()
    if not isinstance(snapshot, dict) or not snapshot:
        raise RuntimeError("缺少冻结的完整源码表。")
    cache = {}

    def record(path):
        path = _regular(path, root)
        if path not in cache:
            before = path.stat()
            value = sha(path)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise RuntimeError("读取时文件变化：" + str(path))
            cache[path] = {"path": path.relative_to(root).as_posix(), "sha256": value, "bytes": after.st_size}
        return dict(cache[path])

    frozen = {}
    for name, wanted in sorted(snapshot.items()):
        if (not isinstance(name, str) or not name or "\\" in name or ":" in name
                or PurePosixPath(name).is_absolute() or any(p in ("", ".", "..") for p in name.split("/"))
                or not isinstance(wanted, str) or not _SHA.fullmatch(wanted)):
            raise RuntimeError("冻结源码路径或指纹格式不正确。")
        path = _regular(program / name, program)
        value = record(path)
        if value["sha256"] != wanted:
            raise RuntimeError("冻结源码实际字节不符：" + name)
        frozen[path] = value

    def frozen_records(paths):
        result = []
        for path in paths:
            path = _regular(path, program)
            if path not in frozen:
                raise RuntimeError("直接输入没有被完整源码表覆盖：" + _module_path(path, program))
            result.append(dict(frozen[path]))
        return result

    snapshot_path = _regular(work / "source_snapshot.json", work)
    if json.loads(snapshot_path.read_text(encoding="utf-8")) != snapshot:
        raise RuntimeError("实际构建快照与冻结文件表不同。")
    normal_rows = _normalise_rows(rows)
    if normal_rows != read_build_rows():
        raise RuntimeError("传入列表与实际build_inputs.tsv不同。")
    script = _regular(program / "shell/build.sh", program)
    frozen_records([script])
    arrays, skeleton = _recognise_build(script.read_text(encoding="utf-8"), normal_rows, program)
    for values in normal_rows.values():
        frozen_records(values)
    build = work / "arm64_build"
    main_inputs, generated = [], []
    for source in normal_rows["source"]:
        main_inputs.append(("source", source, [source]))
    for header in normal_rows["header"]:
        if b"Q_OBJECT" in header.read_bytes():
            source = build / ("moc_" + header.stem + ".cpp")
            main_inputs.append(("moc", source, [header]))
            generated.append(("moc", source, [header]))
    for name, qrc in zip(arrays["resource_names"], normal_rows["resource"]):
        source = build / ("qrc_" + name + ".cpp")
        owners = _qrc_inputs(qrc, program)
        frozen_records(owners)
        main_inputs.append(("resource", source, owners))
        generated.append(("rcc", source, owners))
    for form in normal_rows["form"]:
        generated.append(("uic", build / ("ui_" + form.stem + ".h"), [form]))
    generated_paths = [path for _, path, _ in generated]
    if len(set(generated_paths)) != len(generated_paths):
        raise RuntimeError("不同输入将覆盖同一生成文件，未接受此构建规则。")
    actual_generated = {p for p in build.rglob("*") if p.suffix in (".cpp", ".h")}
    if actual_generated != set(generated_paths):
        raise RuntimeError("生成源和界面头的实际文件集合不等于冻结规则推导。")
    calculated = {"main": len(main_inputs), "helper": len(arrays["helper_sources"]),
                  "moc": sum(kind == "moc" for kind, _, _ in main_inputs),
                  "resource": len(normal_rows["resource"])}
    calculated["total"] = calculated["main"] + calculated["helper"]
    calculated["aarch64"] = calculated["total"]
    if (not isinstance(object_counts, dict) or set(object_counts) != set(calculated)
            or any(type(object_counts[k]) is not int or object_counts[k] != v for k, v in calculated.items())):
        raise RuntimeError("对象计数与实际输入及已审生成规则不一致。")

    def elf_record(path, allowed):
        path = _regular(path, work)
        with path.open("rb") as stream:
            header = stream.read(64)
        if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01"
                or int.from_bytes(header[16:18], "little") not in allowed
                or int.from_bytes(header[18:20], "little") != 183):
            raise RuntimeError("不是所需的ARM64小端ELF：" + str(path))
        return record(path)

    objects, source_to_object, generated_objects = [], {}, {}
    for index, (kind, source, owners) in enumerate(main_inputs):
        obj = build / ("obj_" + str(index) + ".o")
        item = {"object": elf_record(obj, {1}), "source": record(source), "source_kind": kind,
                "frozen_inputs": frozen_records(owners), "link_roles": ["main"], "main_index": index}
        objects.append(item)
        source_to_object[source] = item
        if kind != "source":
            generated_objects[source] = item["object"]
    renderer_objects = []
    for _, source, _ in main_inputs:
        if _is_report_source(source, program):
            renderer_objects.append(source_to_object[source])
    vendor = program / "thirdparty/qtrpt"
    for directory in (vendor / "QtRPT", vendor / "CommonFiles"):
        for header in sorted(directory.glob("*.h"), key=lambda p: p.name.encode("utf-8")):
            header = _regular(header, program)
            if b"Q_OBJECT" not in header.read_bytes():
                continue
            source = build / ("moc_" + header.stem + ".cpp")
            item = source_to_object.get(source)
            if item is not None and item not in renderer_objects:
                renderer_objects.append(item)
    for source in arrays["helper_sources"]:
        obj = build / ("helper_" + source.stem + ".o")
        item = {"object": elf_record(obj, {1}), "source": record(source), "source_kind": "helper",
                "frozen_inputs": frozen_records([source]), "link_roles": ["report_renderer"]}
        objects.append(item)
        renderer_objects.append(item)
    for item in renderer_objects:
        if "report_renderer" not in item["link_roles"]:
            item["link_roles"].append("report_renderer")
    expected_objects = {root / item["object"]["path"] for item in objects}
    if len(expected_objects) != calculated["total"] or set(build.rglob("*.o")) != expected_objects:
        raise RuntimeError("实际对象文件集合不等于逐源推导的对象集合。")
    generated_files = []
    for kind, path, owners in generated:
        item = {"kind": kind, "file": record(path), "frozen_inputs": frozen_records(owners)}
        if path in generated_objects:
            item["object"] = dict(generated_objects[path])
        generated_files.append(item)
    directory_files = []
    for path in sorted(build.rglob("*"), key=lambda p: p.relative_to(build).as_posix()):
        if path.is_dir():
            if path.is_symlink():
                raise RuntimeError("构建目录存在链接子目录。")
            continue
        directory_files.append(record(path))
    binaries = {name: elf_record(work / "build" / name, {2, 3})
                for name in ("sn100_shell", "sn100_report_renderer")}
    return {"format": "sn100-integration-14-01-object-binding-v1",
            "mapping_basis": "reconstructed_from_frozen_build_rules_and_actual_tsv",
            "dependency_scope": "direct_generation_inputs_only_not_compiler_trace_or_full_header_graph",
            "build_script": record(script), "build_inputs": record(work / "build_inputs.tsv"),
            "source_snapshot": record(snapshot_path), "source_files": dict(sorted(snapshot.items())),
            "frozen_inputs": [dict(frozen[p]) for p in sorted(frozen, key=lambda p: _module_path(p, program))],
            "rules": {"kind": "sn100-shell-explicit-arrays-and-generation-v1",
                      "skeleton_sha256": hashlib.sha256(skeleton.encode("utf-8")).hexdigest()},
            "object_counts": calculated, "objects": objects, "generated_files": generated_files,
            "build_directory_files": directory_files, "binaries": binaries,
            "link_objects": {"main": [dict(i["object"]) for i in objects if "main" in i["link_roles"]],
                             "report_renderer": [dict(i["object"]) for i in renderer_objects]}}


def verify_binding(binding, snapshot, rows, object_counts):
    """重读现存对象与生成输入；类型或任意字段变化都拒绝，不只核数量。"""
    if not isinstance(binding, dict):
        raise RuntimeError("对象绑定不是JSON对象。")
    actual = create_binding(snapshot, rows, object_counts)
    try:
        wanted = json.dumps(binding, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
        rebuilt = json.dumps(actual, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
    except (TypeError, ValueError) as error:
        raise RuntimeError("对象绑定含不支持的JSON值。") from error
    if wanted != rebuilt:
        raise RuntimeError("对象、生成输入、冻结源码或链接角色已变化，未通过原绑定。")
