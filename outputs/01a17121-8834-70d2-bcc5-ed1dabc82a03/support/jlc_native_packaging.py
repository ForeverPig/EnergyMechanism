"""Preserve JLC's original XLSX container while serializing authored values.

The artifact workbook is the authoritative edited data. This compatibility
pass preserves the input's relationships, content types, metadata and shared
string convention for the factory importer. It performs no geometry changes.
"""
import hashlib
import copy
import html
import json
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parent.parent
NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}


def read_values(archive):
    strings = [''.join(si.itertext()) for si in ET.fromstring(archive.read('xl/sharedStrings.xml'))]
    sheet = ET.fromstring(archive.read('xl/worksheets/sheet1.xml'))
    values = {}
    for cell in sheet.findall('s:sheetData/s:row/s:c', NS):
        node = cell.find('s:v', NS)
        value = node.text if node is not None else None
        if cell.get('t') == 's':
            value = strings[int(value)]
        elif cell.get('t') == 'inlineStr':
            value = ''.join(cell.find('s:is', NS).itertext())
        elif cell.get('t') not in ('str', 'e', 'b') and value is not None:
            numeric = float(value)
            value = int(numeric) if numeric.is_integer() else numeric
        values[cell.get('r')] = value
    return values, strings


def main():
    authored, destination = map(Path, sys.argv[1:3])
    analysis = json.loads((ROOT / 'support/analysis.json').read_text(encoding='utf-8'))
    original = Path(analysis['source'])
    with zipfile.ZipFile(original) as src, zipfile.ZipFile(authored) as edited:
        source_values, strings = read_values(src)
        edited_values, _ = read_values(edited)
        expected = {}
        for r, row in enumerate([analysis['header'], *analysis['correctedRows']], 1):
            for c, value in enumerate(row):
                expected[f'{chr(65 + c)}{r}'] = value
        assert edited_values == expected, 'Authored workbook differs from verified coordinate results'
        assert set(source_values) == set(edited_values), 'Source inventory changed'
        changes = {key: value for key, value in edited_values.items() if value != source_values[key]}
        assert len(changes) == len(analysis['changes']) == 1986
        assert all(re.fullmatch(r'[DEM][2-9]\d*|[DEM]1\d+', key) for key in changes), 'Unexpected column/header edit'
        original_strings = strings.copy()
        indexes = {value: i for i, value in enumerate(strings)}
        added = []
        visited = set()

        def replace_cell(match):
            address = match.group(1).decode('ascii')
            if address not in changes:
                return match.group(0)
            assert b't="s"' in match.group(0), 'JLC original cell is not a shared string'
            value = changes[address]
            assert isinstance(value, str)
            if value not in indexes:
                indexes[value] = len(strings)
                strings.append(value)
                preserve = ' xml:space="preserve"' if value != value.strip() else ''
                added.append(f'<si><t{preserve}>{html.escape(value, quote=False)}</t></si>'.encode('utf-8'))
            visited.add(address)
            return re.sub(rb'<v>[^<]*</v>', f'<v>{indexes[value]}</v>'.encode('ascii'), match.group(0), count=1)

        sheet_bytes = re.sub(rb'<c\b[^>]*\br="([A-Z]+[0-9]+)"[^>]*>.*?</c>', replace_cell,
                             src.read('xl/worksheets/sheet1.xml'), flags=re.S)
        assert visited == set(changes)
        strings_bytes = src.read('xl/sharedStrings.xml')
        assert strings_bytes.count(b'</sst>') == 1
        strings_bytes = strings_bytes.replace(b'</sst>', b''.join(added) + b'</sst>')
        strings_bytes, count = re.subn(rb'(<sst\b[^>]*\buniqueCount=")[0-9]+(")',
                                       lambda m: m[1] + str(len(strings)).encode('ascii') + m[2], strings_bytes, count=1)
        assert count == 1
        replacements = {'xl/worksheets/sheet1.xml': sheet_bytes, 'xl/sharedStrings.xml': strings_bytes}
        with zipfile.ZipFile(destination, 'w') as output:
            for info in src.infolist():
                output.writestr(copy.copy(info), replacements.get(info.filename, src.read(info.filename)))
        with zipfile.ZipFile(destination) as final:
            final_values, final_strings = read_values(final)
            assert final_values == edited_values
            assert final_strings[:len(original_strings)] == original_strings
            assert final.namelist() == src.namelist(), 'Archive structure changed'
            preserved = []
            for name in src.namelist():
                if name not in replacements:
                    assert final.read(name) == src.read(name), name
                    preserved.append(name)
            xml = ET.fromstring(final.read('xl/sharedStrings.xml'))
            assert int(xml.get('uniqueCount')) == len(final_strings)
            sheet = ET.fromstring(final.read('xl/worksheets/sheet1.xml'))
            cells = sheet.findall('s:sheetData/s:row/s:c', NS)
            assert int(xml.get('count')) == sum(c.get('t') == 's' for c in cells)
            assert not any(c.get('t') in ('str', 'inlineStr') for c in cells)
            # Both the content-type declaration and relationships are exactly
            # the ones already accepted by the user's original-file importer.
            for name in ('[Content_Types].xml', '_rels/.rels', 'xl/_rels/workbook.xml.rels', 'xl/workbook.xml'):
                assert final.read(name) == src.read(name)
            for name in ('A1','D1','E1','K1','L1','A2','K2'):
                assert final_values[name] == source_values[name]
        report = {'output': str(destination), 'rowsIncludingHeader': 684, 'changedCells': len(changes),
                  'valueEqualityWithAuthoredWorkbook': True, 'onlyChangedXmlParts': list(replacements),
                  'sharedStringIndicesForOriginalEntriesPreserved': True, 'preservedArchiveEntries': len(preserved),
                  'sharedStringCount': int(xml.get('count')), 'sharedStringUniqueCount': len(strings),
                  'originalRelationshipsAndContentTypesPreserved': True,
                  'factoryWebsiteImportTested': False,
                  'sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}
        (ROOT / 'support/jlc-packaging-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
