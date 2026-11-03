from pathlib import Path
import re,xml.etree.ElementTree as ET
root=Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
ref=root/'06_programs/sn100_host_reference/Seerain450_PC'
source=(ref/'communication/commandreceiver.cpp').read_text(encoding='utf-8-sig')
new=(root/'06_programs/sn100_host/instrument/original_status_values.cpp').read_text()
ids=dict((name,int(value)) for name,value in re.findall(r'(SubSys_\w+)\s*=\s*(\d+)',(ref/'public/systemcommon.h').read_text(encoding='utf-8-sig')))
count=0
for name,body in re.findall(r'case SystemCommon::(SubSys_\w+):\{(.*?)\}break;',source,re.S):
    expected=re.findall(r'^\s*SystemCommon::driverInformation\.(\w+)\s*=\s*cmdStrList.at\(offset\+\+\)\.(toInt|toDouble|toFloat)\(\)',body,re.M)
    if not expected:continue
    actualbody=re.search(r'case '+str(ids[name])+r':.*?values.read\(block.fields, \{(.*?)\}\);',new,re.S).group(1)
    actual=re.findall(r'\{"(\w+)", FieldType::(\w+)\}',actualbody)
    wanted=[]
    for field,method in expected:
        kind={'toInt':'Boolean','toDouble':'Number','toFloat':'Float'}[method]
        if field=='normalWasteWater' and method=='toDouble':kind='NumericBoolean'
        wanted.append((field,kind))
    assert actual==wanted,(name,actual,wanted)
    count+=len(actual)
print('SOURCE_FIELD_ORDER_AND_TYPES_OK',len(ids)-1,count)
names={node.attrib.get('name') for node in ET.parse(root/'06_programs/sn100_host/instrument/frmdriverset.ui').iter('widget')}
controls=set(re.findall(r'labels.insert\(QStringLiteral\("(\w+)"\)',new))
assert not (controls-names),controls-names
print('UI_OUTPUT_NAMES_OK',len(controls))