"""Read-only validation of a built single-file native EXE (stdlib only)."""
import hashlib
import json
import struct
import sys
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    def u16(at): return struct.unpack_from('<H', data, at)[0]
    def u32(at): return struct.unpack_from('<I', data, at)[0]
    assert data[:2] == b'MZ', 'DOS header missing'
    pe = u32(0x3c)
    assert data[pe:pe+4] == b'PE\0\0', 'PE header missing'
    machine, sections, optional_size = u16(pe+4), u16(pe+6), u16(pe+20)
    optional = pe+24
    assert machine == 0x8664 and u16(optional) == 0x20b, 'expected native AMD64 PE32+'
    directory = optional+112
    cli_rva, cli_size = u32(directory+14*8), u32(directory+14*8+4)
    assert cli_rva == cli_size == 0, 'CLR runtime header must be absent'
    assert u16(optional+68) == 2, 'expected Windows GUI subsystem'
    security = u16(optional+70)
    assert security & 0x160 == 0x160, 'ASLR/DEP/high-entropy VA flags missing'
    table = []
    for i in range(sections):
        s = optional+optional_size+i*40
        table.append((u32(s+12), max(u32(s+8),u32(s+16)), u32(s+20)))
    def offset(rva):
        if rva < u32(optional+60): return rva
        for start, length, raw in table:
            if start <= rva < start+length: return raw+rva-start
        raise AssertionError('invalid RVA')
    imports = []
    at = offset(u32(directory+8))
    while any(data[at:at+20]):
        name = offset(u32(at+12))
        imports.append(data[name:data.index(0,name)].decode('ascii'))
        at += 20
    allowed = {'advapi32.dll','cfgmgr32.dll','comctl32.dll','comdlg32.dll','gdi32.dll','gdiplus.dll',
               'kernel32.dll','msvcrt.dll','ole32.dll','setupapi.dll','shell32.dll','user32.dll'}
    assert {name.lower() for name in imports} <= allowed, f'unexpected runtime dependency: {imports}'
    assert 'gdiplus.dll' in {name.lower() for name in imports}, 'product graphic system DLL missing'
    # Verify published product icon provenance and core notices are embedded,
    # rather than shipping images/styles requiring another runtime folder.
    for marker in [b'Apache License',b'Pictogrammers',b'MIT License',b'Microsoft.Windows.Common-Controls']:
        assert marker in data or marker.decode().encode('utf-16le') in data, f'missing embedded notice: {marker}'
    return {'file':path.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest().upper(),
            'machine':'AMD64','subsystem':'Windows GUI','clr_header':False,
            'static_application_runtime':True,'dotnet_import':False,'imports':imports,
            'aslr':True,'dep':True,'high_entropy_va':True,'embedded_notices':True}


if __name__ == '__main__':
    print(json.dumps([inspect(Path(arg)) for arg in sys.argv[1:]],ensure_ascii=False,indent=2))
