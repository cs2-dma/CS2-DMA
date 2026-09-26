import hashlib
import io
import json
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

def encode_verified(image, format, **options):
    stream = io.BytesIO()
    image.save(stream, format, **options)
    data = stream.getvalue()
    if Image.open(io.BytesIO(data)).convert('RGBA').tobytes() != image.tobytes():
        raise ValueError('RGBA roundtrip mismatch')
    return data

def main():
    raw_path = ROOT / 'src/assets/weapon_icons/weapon_icon_atlas.rgba'
    png_path = raw_path.with_suffix('.png')
    header = (ROOT / 'include/Features/ESP/Render/weapon_icon_atlas_data.generated.h').read_text()
    size = tuple(int(re.search(r'kAtlas' + axis + r'\s*=\s*(\d+)', header)[1]) for axis in ('Width', 'Height'))
    original = raw_path.read_bytes() if raw_path.exists() else Image.open(png_path).convert('RGBA').tobytes()
    encoded = encode_verified(Image.frombytes('RGBA', size, original), 'PNG', optimize=True)
    png_path.write_bytes(encoded)
    print(json.dumps(dict(asset='atlas',original=len(original),compressed=len(encoded),rgba_sha256=hashlib.sha256(original).hexdigest())))
    root = ROOT / 'src/Features/WebRadar/Assets/assets/characters'
    for name in ('tm_phoenix', 'ctm_sas'):
        png = root / (name + '.png')
        webp = root / (name + '.webp')
        source = png if png.exists() else webp
        image = Image.open(source).convert('RGBA')
        encoded = encode_verified(image, 'WEBP', lossless=True, quality=100, method=6, exact=True)
        webp.write_bytes(encoded)
        print(json.dumps(dict(asset=name,original=source.stat().st_size,compressed=len(encoded),rgba_sha256=hashlib.sha256(image.tobytes()).hexdigest())))

if __name__ == '__main__':
    main()
