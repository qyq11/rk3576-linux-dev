import json
import re
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[4]
blob = (root / '05_tools/tests/sn100_host/ACA_V1.0OffLineD-500').read_bytes()
section_offset = struct.unpack_from('<Q', blob, 40)[0]
section_size, section_count, names_index = struct.unpack_from('<HHH', blob, 58)
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, section_offset + i * section_size) for i in range(section_count)]
section_names = sections[names_index]
names = blob[section_names[4]:section_names[4] + section_names[5]]

def cstr(data, at):
    return data[at:data.index(b'\0', at)].decode('utf-8', errors='replace')

def virtual_bytes(address, count):
    for section in sections:
        if section[3] <= address < section[3] + section[5]:
            if section[1] == 8:
                return bytes(count)
            at = section[4] + address - section[3]
            return blob[at:at+count]
    raise ValueError(hex(address))

symbols = {}
for section in sections:
    if section[1] != 2:
        continue
    strings_section = sections[section[6]]
    strings = blob[strings_section[4]:strings_section[4]+strings_section[5]]
    for at in range(section[4], section[4]+section[5], section[9]):
        name_at, info, other, index, value, size = struct.unpack_from('<IBBHQQ', blob, at)
        name = cstr(strings, name_at)
        if name.startswith('is') and ('Debug' in name or 'Offline' in name) and index:
            symbols[name] = {'address': hex(value), 'size': size, 'initial_bytes': virtual_bytes(value, max(size, 1)).hex()}

result = {'flags': symbols}
(Path(__file__).parent / 'binary_flags.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
for start, end in [(0x64d650,0x64dce0)]:
    block = virtual_bytes(start, end-start)
    for match in re.finditer(rb'[\x20-\x7e]{4,}', block):
        print(hex(start+match.start()), match.group().decode('ascii'))
