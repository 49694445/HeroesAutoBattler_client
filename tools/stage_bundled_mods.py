"""Stage legally obtained mod folders into a NEW Android generated assets folder.

Example: python tools/stage_bundled_mods.py --mods C:/resources/Mods
         --assets build-android/android-package/assets
No downloads, user-data changes, signing keys or asset licenses are supplied.
"""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mods', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    args = parser.parse_args()
    destination = args.assets.resolve() / 'hab-bundle'
    if destination.exists():
        raise SystemExit('Use a new generated asset folder; existing files are preserved')
    records, sources = {}, {}
    for name in ('hota', 'chinese-translation'):
        source = args.mods.resolve() / name
        if not (source / 'mod.json').is_file():
            raise SystemExit('Missing mod metadata: ' + name)
        for path in sorted(source.rglob('*')):
            if path.is_symlink():
                raise SystemExit('Symlinks are not accepted')
            if not path.is_file():
                continue
            if path.suffix.lower() in ('.exe', '.dll', '.key', '.pem', '.keystore', '.jks'):
                raise SystemExit('Unexpected executable or key in resource folder')
            relative = 'Mods/' + path.relative_to(args.mods.resolve()).as_posix()
            sources[relative] = path
            records[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name, source in sources.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != records[name]:
            raise SystemExit('Source changed during staging; do not package this folder')
        target.write_bytes(data)
    manifest = json.dumps(records, sort_keys=True, indent=2)
    (destination / 'manifest.json').write_text(manifest, encoding='utf-8')
    (destination / 'bundle-id.txt').write_text(hashlib.sha256(manifest.encode()).hexdigest(), encoding='ascii')
    print('Staged', len(records), 'resource files')


if __name__ == '__main__':
    main()
