import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('baseline', type=Path)
    args = parser.parse_args()
    atlas = 'src/assets/weapon_icons/weapon_icon_atlas'
    original = (args.baseline / (atlas + '.rgba')).read_bytes()
    encoded = ROOT / (atlas + '.png')
    assert Image.open(encoded).convert('RGBA').tobytes() == original
    old_bytes, new_bytes = len(original), encoded.stat().st_size
    characters = 'src/Features/WebRadar/Assets/assets/characters'
    for name in ('ctm_sas', 'tm_phoenix'):
        before = args.baseline / characters / (name + '.png')
        after = ROOT / characters / (name + '.webp')
        assert Image.open(before).convert('RGBA').tobytes() == Image.open(after).convert('RGBA').tobytes()
        old_bytes += before.stat().st_size
        new_bytes += after.stat().st_size
    maps_path = 'src/Features/WebRadar/Assets/data'
    count = 0
    for before in (args.baseline / maps_path).glob('*/radar.webp'):
        after = ROOT / maps_path / before.parent.name / before.name
        assert Image.open(before).convert('RGBA').tobytes() == Image.open(after).convert('RGBA').tobytes(), before
        old_bytes += before.stat().st_size
        new_bytes += after.stat().st_size
        count += 1
    manifest_path = 'src/Features/WebRadar/Assets/maps.json'
    before = {m['name']: m for m in json.loads((args.baseline / manifest_path).read_text(encoding='utf-8-sig'))['maps']}
    after = {m['name']: m for m in json.loads((ROOT / manifest_path).read_text())['maps']}
    for name in ('de_mirage', 'de_vertigo'):
        for key in ('origin', 'scale', 'bounds'):
            assert before[name][key] == after[name][key], (name, key)
    print(json.dumps(dict(existing_maps=count, old_bytes=old_bytes, new_bytes=new_bytes,
        saved_bytes=old_bytes-new_bytes, atlas_rgba_sha256=hashlib.sha256(original).hexdigest())))

if __name__ == '__main__':
    main()
