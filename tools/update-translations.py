#!/usr/bin/env python3
"""Synchronize Qt's English catalog with dlTr() sources; never translates text."""
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'translations/disklens_en.ts'
LITERAL = r'"(?:\\.|[^"\\])*"'
CALL = re.compile(r'\bdlTr\(\s*((?:' + LITERAL + r'\s*)+)\)')
PLACEHOLDER = re.compile(r'%(?:L?\d+|n)')


def sources():
    messages = collections.defaultdict(set)
    for path in sorted((ROOT / 'src').glob('*')):
        if path.suffix not in ('.h', '.cpp'):
            continue
        for match in CALL.finditer(path.read_text()):
            source = ''.join(json.loads(s) for s in re.findall(LITERAL, match[1]))
            messages[source].add(str(path.relative_to(ROOT)))
    return messages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='fail on missing, stale or invalid translations')
    parser.add_argument('--compile', action='store_true', help='run Qt lrelease after synchronization')
    args = parser.parse_args()
    current = sources()
    existing = {}
    if CATALOG.exists():
        for message in ET.parse(CATALOG).findall('./context/message'):
            translation = message.find('translation')
            existing[message.findtext('source')] = '' if translation is None else (translation.text or '')
    errors = []
    for source in current:
        translation = existing.get(source, '')
        if not translation:
            errors.append(f'Missing translation: {source!r}')
        elif collections.Counter(PLACEHOLDER.findall(source)) != collections.Counter(PLACEHOLDER.findall(translation)):
            errors.append(f'Placeholder mismatch: {source!r}')
    if args.check:
        errors += [f'Stale translation: {s!r}' for s in existing.keys() - current.keys()]
        if errors:
            print('\n'.join(errors), file=sys.stderr)
            return 1
        print(f'{len(current)} translations: coverage and placeholders verified')
        return 0
    tree = ET.Element('TS', version='2.1', language='en_US', sourcelanguage='zh_CN')
    context = ET.SubElement(tree, 'context')
    ET.SubElement(context, 'name').text = 'DiskLens'
    for source, paths in sorted(current.items()):
        message = ET.SubElement(context, 'message')
        for path in sorted(paths):
            ET.SubElement(message, 'location', filename='../' + path)
        ET.SubElement(message, 'source').text = source
        translation = ET.SubElement(message, 'translation')
        translation.text = existing.get(source, '')
        if not translation.text:
            translation.set('type', 'unfinished')
    ET.indent(tree, space='  ')
    CATALOG.parent.mkdir(exist_ok=True)
    CATALOG.write_text('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + ET.tostring(tree, encoding='unicode') + '\n')
    print(f'Synchronized {len(current)} sources; {len(errors)} incomplete translations')
    if args.compile:
        if errors:
            print('Complete translations before compiling.', file=sys.stderr)
            return 1
        subprocess.run(['lrelease', str(CATALOG), '-qm', str(CATALOG.with_suffix('.qm'))], check=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
