#!/usr/bin/env python3
"""Rebuild a raw ext4 image from an Android block OTA (transfer.list + new.dat).

usage: sdat2img.py system.transfer.list system.new.dat system.img
"""
import sys

BLOCK = 4096


def ranges(spec):
    nums = [int(x) for x in spec.split(',')]
    if nums[0] != len(nums) - 1 or nums[0] % 2:
        raise ValueError('bad rangeset: ' + spec)
    return list(zip(nums[1::2], nums[2::2]))


def main(listing, data, out):
    with open(listing) as f:
        lines = f.read().split('\n')
    version = int(lines[0])
    total = int(lines[1])
    body = lines[4:] if version >= 2 else lines[2:]
    size = total
    for line in body:
        if line.strip():
            size = max([size] + [e for _, e in ranges(line.split(' ', 1)[1])])
    with open(data, 'rb') as src, open(out, 'wb') as dst:
        dst.truncate(size * BLOCK)
        for line in body:
            if not line.strip():
                continue
            cmd, spec = line.split(' ', 1)
            if cmd != 'new':
                if cmd not in ('erase', 'zero'):
                    raise ValueError('unsupported command: ' + cmd)
                continue
            for begin, end in ranges(spec):
                dst.seek(begin * BLOCK)
                for _ in range(end - begin):
                    block = src.read(BLOCK)
                    if len(block) != BLOCK:
                        raise ValueError('new.dat ended early')
                    dst.write(block)
    print('version %d, %d blocks written, %d in image -> %s' % (version, total, size, out))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip())
    main(*sys.argv[1:])
