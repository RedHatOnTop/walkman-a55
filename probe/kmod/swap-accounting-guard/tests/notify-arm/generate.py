import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

BASE = 0xc0008000
SHA256 = '71f050c3d24067ad86ad3e8ea4d4712fd723e22147e002d71107faa8cefef2ed'


def read_spec(path):
    lines = path.read_text().splitlines()
    if not lines or lines[0] != 'SPEC 22 11':
        raise ValueError('wrong shared spec counts')
    sites, targets = [], []
    for line in lines[1:]:
        match = re.fullmatch(r'SITE (\w+) ([0-9a-f]{8}) ([0-9a-f]+) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})', line)
        target = re.fullmatch(r'TARGET (\w+) ([0-9a-f]{8})', line)
        if match:
            name, addr, offset, old, new, alternate = match.groups()
            sites.append({'name':name, 'base':int(addr,16), 'offset':int(offset,16), 'old':int(old,16), 'new':int(new,16), 'alternate':int(alternate,16)})
        elif target:
            targets.append((target[1],int(target[2],16)))
        else:
            raise ValueError('malformed spec line: ' + line)
    if len(sites) != 22 or len(targets) != 5 or len({name for name, addr in targets}) != 5:
        raise ValueError('incomplete/duplicate spec')
    if sum(site['old'] != site['new'] for site in sites) != 11:
        raise ValueError('wrong changed-word count')
    addresses = [site['base'] + site['offset'] for site in sites]
    if len(set(addresses)) != 22 or any(address % 4 or address > 0xffffffff for address in addresses):
        raise ValueError('duplicate/unaligned word address')
    alternate = [(site['base']+site['offset'],site['old'],site['new'],site['alternate'])
                 for site in sites if site['alternate']]
    if alternate != [(0xc03481e8,0xebf31b28,0xebf31b28,0xe8bd4000)]:
        raise ValueError('wrong live ftrace alternate')
    return sites, targets


def kernel_symbols(path):
    rows = [line.split() for line in path.read_text().splitlines()]
    if not rows or any(len(row)<3 or not re.fullmatch(r'[0-9a-f]{8}',row[0]) for row in rows):
        raise ValueError('malformed kallsyms')
    return [(int(row[0],16),row[2]) for row in rows]


def symbol(rows, name):
    matches = [address for address, label in rows if label == name]
    if len(matches) != 1:
        raise ValueError('missing/duplicate stock symbol: ' + name)
    return matches[0]


def end_of(rows, name):
    start = symbol(rows, name)
    return min(address for address, label in rows if address > start)


def slice_code(raw, start, end):
    if start < BASE or end <= start or end-BASE > len(raw) or end-start > 8192:
        raise ValueError('invalid kernel code bounds')
    return raw[start-BASE:end-BASE]


def ftrace_nop(data, expected):
    words = list(struct.unpack('<'+'I'*(len(data)//4),data))
    for offset, old in expected:
        if words[offset//4] != old:
            raise ValueError('wrong harness mcount word')
    return b''.join(struct.pack('<I',0xe8bd4000 if i*4 in dict(expected) else word)
                    for i, word in enumerate(words))


def generate(args):
    sites, targets = read_spec(args.spec)
    rows = kernel_symbols(args.kallsyms)
    raw = args.vmlinux.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise ValueError('not the verified stock image')
    for name, expected in targets:
        if symbol(rows,name) != expected:
            raise ValueError('stock target address mismatch: ' + name)
    for site in sites:
        address = site['base'] + site['offset']
        if symbol(rows,site['name']) != site['base']:
            raise ValueError('stock site symbol mismatch')
        if struct.unpack('<I',slice_code(raw,address,address+4))[0] != site['old']:
            raise ValueError('stock word mismatch at ' + hex(address))
    notify = sorted((site for site in sites if site['name']=='zram_slot_free_notify'), key=lambda site:site['offset'])
    if [site['offset'] for site in notify] != list(range(0,80,4)):
        raise ValueError('incomplete callback guard span')
    old = slice_code(raw,symbol(rows,'zram_slot_free_notify'),symbol(rows,'zram_slot_free_notify')+80)
    new = b''.join(struct.pack('<I',site['new']) for site in notify)
    public = slice_code(raw,symbol(rows,'down_write_trylock'),end_of(rows,'up_write'))
    inner = slice_code(raw,symbol(rows,'__down_write_trylock'),end_of(rows,'__up_write'))
    variants = [('notify-stock.bin',old),('notify-trylock.bin',new),
                ('rw-public.bin',public),('rw-inner.bin',inner),
                ('notify-stock-nop.bin',ftrace_nop(old,[(16,0xebf31b28)])),
                ('notify-trylock-nop.bin',ftrace_nop(new,[(16,0xebf31b28)])),
                ('rw-public-nop.bin',ftrace_nop(public,[(16,0xebfe4c8b),(72,0xebfe4c7d)]))]
    for name, data in variants:
        (args.out/name).write_bytes(data)
    (args.out/'spec-proof.json').write_text(json.dumps({'checked_words':22,'changed_words':11,'callback':notify,'targets':targets},indent=2)+'\n')
    print('PASS shared spec: 22 stock words, mcount BL/NOP only, 11 changed words, 5 call targets; ARM candidate generated from module header')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('spec','vmlinux','kallsyms','out'):
        parser.add_argument('--'+name,required=True,type=Path)
    generate(parser.parse_args())
