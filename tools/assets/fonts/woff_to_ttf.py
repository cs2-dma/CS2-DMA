import argparse
import struct
import zlib
from pathlib import Path


def checksum(data):
    padded = data + bytes((-len(data)) % 4)
    return sum(struct.unpack(f'>{len(padded) // 4}I', padded)) & 0xFFFFFFFF


def convert(source, destination):
    data = source.read_bytes()
    signature, flavor, length, count, reserved, sfnt_size = struct.unpack_from('>4sIIHHI', data)
    if signature != b'wOFF' or length != len(data) or reserved or not 1 <= count <= 4096:
        raise ValueError('Invalid WOFF header')
    tables = []
    for index in range(count):
        tag, offset, compressed, original, expected = struct.unpack_from('>4sIIII', data, 44 + index * 20)
        if offset + compressed > len(data) or compressed > original or original > 16000000:
            raise ValueError('Invalid WOFF table')
        payload = data[offset:offset + compressed]
        if compressed != original:
            payload = zlib.decompress(payload)
        if len(payload) != original:
            raise ValueError('Invalid table size')
        if tag == b'head':
            payload = payload[:8] + bytes(4) + payload[12:]
        if checksum(payload) != expected:
            raise ValueError(f'Table checksum mismatch: {tag!r}')
        tables.append((tag, payload, expected))
    tables.sort()
    selector = count.bit_length() - 1
    search_range = 16 * (1 << selector)
    result = bytearray(struct.pack('>IHHHH', flavor, count, search_range, selector, 16 * count - search_range))
    offset = 12 + 16 * count
    head_offset = None
    for tag, payload, expected in tables:
        result.extend(struct.pack('>4sIII', tag, expected, offset, len(payload)))
        if tag == b'head':
            head_offset = offset
        offset += (len(payload) + 3) & ~3
    for _, payload, _ in tables:
        result.extend(payload)
        result.extend(bytes((-len(payload)) % 4))
    if len(result) != sfnt_size or head_offset is None:
        raise ValueError('Invalid SFNT layout')
    struct.pack_into('>I', result, head_offset + 8, (0xB1B0AFBA - checksum(result)) & 0xFFFFFFFF)
    if checksum(result) != 0xB1B0AFBA:
        raise ValueError('Invalid SFNT checksum')
    destination.write_bytes(result)
    print(f'Validated icon font: {count} tables, {len(result)} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    convert(args.source, args.destination)
