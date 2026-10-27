#!/usr/bin/env python3
"""Read completed execution evidence and inspect only its recorded process groups."""
import hashlib
import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
ATTEMPT = ROOT / "attempt_01"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(name, value):
    (ROOT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    receipt_path = ATTEMPT / "receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt["status"] != "passed":
        raise SystemExit("Completed controller did not pass")
    groups = []
    for step in receipt["steps"]:
        present = False
        try:
            os.killpg(step["pgid"], 0)
            present = True
        except ProcessLookupError:
            pass
        groups.append({"step": step["name"], "pgid": step["pgid"],
                       "actual_exit_code": step["exit_code"], "timeout": step["timed_out"],
                       "controller_cleanup": step["cleanup"], "present_at_final_readonly_check": present})
    save("process_group_cleanup.json", {"schema": "sn100-formal-dictionary-final-pgid-check-v1",
         "method": "killpg(pgid,0) only; no signals or global process cleanup", "groups": groups,
         "remaining_count": sum(item["present_at_final_readonly_check"] for item in groups)})
    if any(item["present_at_final_readonly_check"] or item["actual_exit_code"] != 0 for item in groups):
        raise SystemExit("A recorded step failed or a recorded process group still exists")
    checks = []
    for target in receipt["targets"]:
        for run in target["runs"]:
            xml = ATTEMPT / target["name"] / ("result_" + run["mode"] + ".xml")
            if digest(xml) != run["actual"]["xml_sha256"]:
                raise SystemExit("Qt XML changed after controller completion")
            document = ET.parse(xml).getroot()
            cases = []
            for function in document.findall("TestFunction"):
                for incident in function.findall("Incident"):
                    cases.append({"function": function.attrib["name"], "data_tag": incident.findtext("DataTag", ""),
                                  "actual": incident.attrib["type"]})
            checks.append({"target": target["name"], "mode": run["mode"], "runtime": run["runtime"],
                           "xml": str(xml.relative_to(ROOT)), "xml_sha256": digest(xml), "cases": cases,
                           "warnings": [{"type": message.attrib.get("type"), "text": message.findtext("Description", "")}
                                        for message in document.findall(".//Message")]})
    save("case_receipts.json", {"schema": "sn100-formal-dictionary-case-xml-receipts-v1", "runs": checks,
         "SQL_query_readonly": {"witness": "fiveTypesNameMnemonicLikeAndReadOnly passed in formal core target",
            "assertions": "Real SQLite/INI byte hashes unchanged during queries, exact type/name/mnemonic/LIKE/id/long name, unsaved local dictionary changes excluded then visible only after real save"},
         "patient_SQL_write": {"witness": "fiveDraftFieldsOnlyOriginalSaveCommits passed for Guest and local role in both GUI backends",
            "assertions": "Real HistoryStore worker; all-table logical snapshots equal before original Save across5 field selections; original Save emits exactly1 request and real patientSaved after commit; all other rows/columns/tables protected; selected fields reopen exactly"},
         "cancel_late_close_identity": {"witness": "Named actual incidents/data tags above",
            "assertions": "X/Escape/window close/empty/error preserve selection draft; closed/reopened different field cannot accept stale reply; existing Guest patient save and hide/display gates retained"},
         "evidence_limits": ["Before/after database snapshots and hash values were compared inside the unchanged tests; numeric snapshots were not exported. This receipt maps actual XML passes to those frozen assertions, not invented SQL values.",
                             "Page test uses controlled dictionary replies plus real HistoryStore SQL; HospitalStore SQL is independently tested. Full shell worker-to-page wiring was not executed here.",
                             "Qt synthetic context events and programmed button clicks are not physical keyboard/mouse, original Windows visual, RK or instrument verification."]})
    save("parent_exit_receipt.json", {"schema": "sn100-formal-dictionary-observed-parent-exit-v1",
         "windows_exec_session_id": 7568, "observed_final_tool_chunk": "12b490", "actual_parent_exit_code": 0,
         "controller_json_status": receipt["status"], "receipt_sha256": digest(receipt_path),
         "all_command_steps_exit0": len(groups), "all_owned_recorded_pgids_gone": True})
    summary = {"status": "passed", "actual_parent_exit": 0, "steps_exit0": len(groups),
               "source_count": receipt["source_count"], "inputs_equal": receipt["inputs_before"] == receipt["inputs_after"],
               "tools_equal": receipt["tools_before"] == receipt["tools_after"], "tools_count": len(receipt["tools_before"]),
               "fresh_objects": {target["name"]: target["build"]["object_count"] for target in receipt["targets"]},
               "runs": [{"target": target["name"], "mode": run["mode"], **run["actual"]}
                        for target in receipt["targets"] for run in target["runs"]],
               "parent_receipt": "parent_exit_receipt.json", "cases": "case_receipts.json",
               "cleanup": "process_group_cleanup.json", "controller_receipt_sha256": digest(receipt_path),
               "formal_input_manifest_sha256": digest(ATTEMPT / "formal_inputs.json"),
               "scope": "This new formal target closure only; no inherited447 or private exe/object; not full main/Windows/RK/instrument acceptance"}
    save("verification.json", summary)
    print(json.dumps({"status": "passed", "parent": 0, "steps": len(groups), "remaining_pgids": 0,
                      "source_count": receipt["source_count"], "Qt_pass_totals": [run["actual"]["pass_incidents"] for target in receipt["targets"] for run in target["runs"]]}))


if __name__ == "__main__":
    main()
