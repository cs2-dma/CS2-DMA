import argparse
import io
import json
import math
import re
import tempfile
import urllib.request
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'src/Features/WebRadar/Assets'
POOL_REVISION = '3fc98e763328f7d1627405b389d1b6b69c5b0e38'

def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'KevqDMA-map-maintenance'})
    with urllib.request.urlopen(request, timeout=25) as response:
        data = response.read(32 * 1024 * 1024 + 1)
    if len(data) > 32 * 1024 * 1024:
        raise ValueError('Asset exceeds size limit')
    return data

def parse_overview(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\r\n]*', '', text)
    tokens = re.findall(r'"([^"\r\n]*)"|([{}])', text)
    position = 0
    def read_object(nested=False):
        nonlocal position
        result = {}
        while position < len(tokens):
            word, brace = tokens[position]
            position += 1
            if brace == '}':
                if not nested:
                    raise ValueError('Unexpected closing brace')
                return result
            if brace or position >= len(tokens):
                raise ValueError('Incomplete overview field')
            value, opening = tokens[position]
            position += 1
            if opening == '}':
                raise ValueError('Missing overview value')
            result[word] = read_object(True) if opening == '{' else value
        if nested:
            raise ValueError('Unclosed overview object')
        return result
    return read_object()

def make_map(name, title, overview, parent='', section='', altitude=None):
    if not re.fullmatch(r'[a-z0-9_]+', name):
        raise ValueError(f'Invalid map name: {name}')
    x, y, scale = (float(overview[key]) for key in ('pos_x', 'pos_y', 'scale'))
    if not all(math.isfinite(v) for v in (x, y, scale)) or not 0.01 < scale < 100:
        raise ValueError(f'Invalid projection for {name}')
    result = dict(name=name, display_name=title, origin=dict(x=x, y=y), scale=scale,
        bounds=dict(min_x=x, max_x=x+1024*scale, min_y=y-1024*scale, max_y=y),
        images=dict(radar=f'/data/{name}/radar.webp', background=f'/data/{name}/radar.webp'))
    if parent:
        result['parent'] = parent
    if section:
        low, high = float(altitude['AltitudeMin']), float(altitude['AltitudeMax'])
        if not math.isfinite(low) or not math.isfinite(high) or low >= high:
            raise ValueError(f'Invalid altitude interval: {name}')
        result['section'] = section
        result['altitude'] = dict(min=low, max=high)
    return result

def read_pool(revision):
    url = f'https://raw.githubusercontent.com/SteamTracking/GameTracking-CS2/{revision}/game/csgo/pak01_dir/gamemodes.txt'
    game = parse_overview(fetch(url).decode('utf-8-sig'))['GameModes.txt']
    groups = game['mapgroups']
    wanted = set()
    for mode in ('competitive', 'casual', '2v2', 'retakes'):
        for group in game['templates#include'][mode]:
            wanted.update(groups[group]['maps'])
    for group in ('mg_armsrace', 'mg_rush_001'):
        wanted.update(groups[group]['maps'])
    return wanted, url

def visible_equal(a, b):
    if a.size != b.size or a.getchannel('A').tobytes() != b.getchannel('A').tobytes():
        return False
    mask = a.getchannel('A').point(lambda value: 255 if value else 0)
    difference = ImageChops.difference(a.convert('RGB'), b.convert('RGB'))
    return Image.composite(difference, Image.new('RGB', a.size), mask).getbbox() is None

def encode_lossless(image):
    stream = io.BytesIO()
    image.save(stream, 'WEBP', lossless=True, quality=100, method=6, exact=True)
    encoded = stream.getvalue()
    if Image.open(io.BytesIO(encoded)).convert('RGBA').tobytes() != image.tobytes():
        raise ValueError('Lossless roundtrip failed')
    return encoded

def register_resources(text, definitions):
    registered = {match[0] or match[1] for match in re.findall(
        r'^\s*(?:"([^"]+)"|(/\S+))\s+RCDATA', text, re.M)}
    additions = []
    for definition in definitions:
        paths = {definition['images']['radar'], definition['images']['background']}
        if not definition.get('dynamic'):
            paths.add(f"/data/{definition['name']}/data.json")
        for path in sorted(paths):
            if path not in registered:
                additions.append(f'"{path}" RCDATA "src/Features/WebRadar/Assets{path}"')
                registered.add(path)
    return text.rstrip() + '\n' + ''.join(line + '\n' for line in additions)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('commit')
    parser.add_argument('--pool-commit', default=POOL_REVISION)
    args = parser.parse_args()
    if not all(re.fullmatch(r'[0-9a-f]{40}', value) for value in (args.commit, args.pool_commit)):
        raise ValueError('Pinned 40-character revisions required')
    base = f'https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/{args.commit}'
    upstream = json.loads(fetch(f'{base}/data/available.json'))['maps']
    wanted, pool_url = read_pool(args.pool_commit)
    manifest_path = ASSETS / 'maps.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    current = {item['name']: item for item in manifest['maps']}
    protected = {name: (current[name]['origin'], current[name]['scale']) for name in ('de_mirage', 'de_vertigo')}
    provenance = dict(repository='https://github.com/MurkyYT/cs2-map-icons', commit=args.commit,
        pool_source=pool_url, pool_maps=sorted(wanted), missing_overviews=[],
        ownership='Map assets and overview data belong to Valve Corporation.', maps={})
    names = list(dict.fromkeys([name for name in current if name in upstream] + sorted(wanted)))
    staged = {}
    for name in names:
        entry = upstream.get(name, {})
        if not entry.get('radar_info') or not entry.get('radar_paths'):
            if name in wanted:
                provenance['missing_overviews'].append(name)
            continue
        overview = next(iter(parse_overview(fetch(f'{base}/data/radar_info/{name}.txt').decode('utf-8-sig')).values()))
        title = entry.get('display_name', name)
        sections = overview.get('verticalsections', {})
        variants = [(name, title, overview, '', 'default' if 'default' in sections else '', sections.get('default'))]
        variants += [(f'{name}_{key}', f'{title} / {key}', value, name, '', None)
            for key, value in overview.get('Volumes', {}).items()]
        variants += [(f'{name}_{key}', f'{title} / {key}', overview, name, key, value)
            for key, value in sections.items() if key != 'default']
        for map_name, map_title, info, parent, section, altitude in variants:
            candidates = [path for path in entry['radar_paths'] if re.search(
                r'/' + re.escape(map_name) + r'_radar(?:_(?:psd|tga))?\.png$', path)]
            if len(candidates) > 1:
                preferred = '_tga.png' if info.get('autogenerated_tga') == '1' else '_psd.png'
                candidates = [path for path in candidates if path.endswith(preferred)]
            if len(candidates) != 1:
                raise ValueError(f'Unambiguous radar required: {map_name}: {candidates}')
            url = candidates[0].replace('/main/', f'/{args.commit}/')
            image = Image.open(io.BytesIO(fetch(url))).convert('RGBA')
            old_path = ASSETS / 'data' / map_name / 'radar.webp'
            old_data = old_path.read_bytes() if old_path.exists() else None
            if old_data:
                old_image = Image.open(io.BytesIO(old_data)).convert('RGBA')
                if visible_equal(image, old_image):
                    image = old_image
                elif map_name in protected:
                    raise ValueError(f'Protected map image changed: {map_name}')
            encoded = encode_lossless(image)
            if old_data and image.tobytes() == Image.open(io.BytesIO(old_data)).convert('RGBA').tobytes() and len(old_data) <= len(encoded):
                encoded = old_data
            definition = make_map(map_name, map_title, info, parent, section, altitude)
            if map_name in protected and (definition['origin'], definition['scale']) != protected[map_name]:
                raise ValueError(f'Protected projection changed: {map_name}')
            current[map_name] = definition
            staged[f'data/{map_name}/radar.webp'] = encoded
            staged[f'data/{map_name}/data.json'] = (json.dumps(dict(x=definition['origin']['x'],
                y=definition['origin']['y'], scale=definition['scale']), indent=2)+'\n').encode()
            provenance['maps'][map_name] = dict(overview=f'data/radar_info/{name}.txt', radar=url, pixels=list(image.size))
            print(map_name, image.size, len(encoded), flush=True)
    manifest['maps'] = list(current.values())
    staged['maps.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2)+'\n').encode()
    staged['map-sources.json'] = (json.dumps(provenance, indent=2)+'\n').encode()
    staged['webradar_resources.rc'] = register_resources(
        (ASSETS / 'webradar_resources.rc').read_text(encoding='utf-8-sig'), manifest['maps']).encode('utf-8-sig')
    with tempfile.TemporaryDirectory(prefix='kevq-overviews-') as folder:
        temporary = Path(folder)
        for name, data in staged.items():
            path = temporary / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for name in staged:
            path = ASSETS / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((temporary / name).read_bytes())
    print('Missing verified overviews:', provenance['missing_overviews'])

if __name__ == '__main__':
    main()
