#!/usr/bin/env python3
"""Map .text offsets of LO16 relocations against .rodata.str* local labels to the string."""
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
f = ELFFile(open(sys.argv[1], 'rb'))
symtab = f.get_section_by_name('.symtab')
out = {}
for sec in f.iter_sections():
    if not isinstance(sec, RelocationSection): continue
    tgt = f.get_section(sec['sh_info'])
    if tgt.name != '.text': continue
    for r in sec.iter_relocations():
        if r['r_info_type'] != 6: continue
        s = symtab.get_symbol(r['r_info_sym'])
        if not s.name.startswith('$LC'): continue
        ss = f.get_section(s['st_shndx'])
        if not ss.name.startswith('.rodata.str'): continue
        data = ss.data(); o = s['st_value']
        out[r['r_offset']] = data[o:data.index(b'\0', o)].decode('latin1')
for k in sorted(out): print('%x\t%r' % (k, out[k][:100]))
