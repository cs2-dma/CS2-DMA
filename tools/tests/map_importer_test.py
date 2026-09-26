import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'maps'))
import sync_overviews as sync

overview = sync.parse_overview('''// title
"root" {
    "pos_x" "10"
    "pos_y" "20"
    "scale" "3"
    "verticalsections" { "default" { "AltitudeMin" "0" "AltitudeMax" "100" } }
}
''')['root']
definition = sync.make_map('test', 'Test', overview, section='default', altitude=overview['verticalsections']['default'])
assert definition['bounds'] == dict(min_x=10, max_x=3082, min_y=-3052, max_y=20)
assert definition['altitude'] == dict(min=0, max=100)
resources = sync.register_resources('', [definition])
assert resources.count('RCDATA') == 2
assert sync.register_resources(resources, [definition]) == resources
for source in ('"a" {', '}', '"a" }', '"a"'):
    try:
        sync.parse_overview(source)
    except ValueError:
        pass
    else:
        raise AssertionError(source)
for field, value in [('scale', '0'), ('scale', 'nan'), ('pos_x', 'inf')]:
    try:
        sync.make_map('test', 'Test', dict(overview, **{field: value}))
    except ValueError:
        pass
    else:
        raise AssertionError((field, value))
print('Map importer tests passed: parsing, projection, altitude, resource registration, invalid data')
