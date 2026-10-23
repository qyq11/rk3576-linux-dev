import argparse, hashlib, json
from pathlib import Path
from pypdf import PdfReader
from pypdf.generic import ContentStream
p=argparse.ArgumentParser(); p.add_argument('--before',required=True); p.add_argument('--after',required=True); p.add_argument('--output',required=True); a=p.parse_args()
def inspect(path):
    reader=PdfReader(path); result=[]
    for page in reader.pages:
        fonts=page['/Resources']['/Font']; active=None; items=[]; font_hashes={}
        for name,ref in fonts.items():
            desc=ref.get_object()['/DescendantFonts'][0].get_object()
            stream=desc['/FontDescriptor']['/FontFile2'].get_data()
            font_hashes[str(name)]=hashlib.sha256(stream).hexdigest()
        for args,op in ContentStream(page.get_contents(),reader).operations:
            if op==b'Tf': active=str(args[0])
            if op==b'Tj':
                raw=bytes(args[0]) if isinstance(args[0],bytes) else args[0].original_bytes
                cid=int.from_bytes(raw,'big'); desc=fonts[active].get_object()['/DescendantFonts'][0].get_object(); mapping=desc['/CIDToGIDMap']
                gid=cid if mapping=='/Identity' else int.from_bytes(mapping.get_data()[2*cid:2*cid+2],'big')
                items.append(['Tj',active,gid])
            elif op in (b'Tf',b'Tm',b'Td',b'TD',b'T*',b'Tc',b'Tw',b'Tz',b'TL',b'Tr',b'Ts'):
                items.append([op.decode(),[str(x) for x in args]])
        result.append({'font_program_sha256':font_hashes,'text_operations':items})
    return result
old,new=inspect(a.before),inspect(a.after)
result={'before':a.before,'after':a.after,'pages':[],'passed':len(old)==len(new)}
for i,(x,y) in enumerate(zip(old,new)):
    item={'page':i+1,'embedded_font_bytes_identical':x['font_program_sha256']==y['font_program_sha256'],'text_positions_and_glyphs_identical':x['text_operations']==y['text_operations'],'glyph_count':sum(t[0]=='Tj' for t in x['text_operations'])}
    result['pages'].append(item); result['passed'] &= all(item[k] for k in ('embedded_font_bytes_identical','text_positions_and_glyphs_identical'))
Path(a.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8'); print(json.dumps(result)); raise SystemExit(0 if result['passed'] else 1)
