#!/usr/bin/env python3
"""The extension id in identity.json must be the id Chrome derives from the manifest key."""
import base64, hashlib, json, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
m = json.loads((root / 'extension/manifest.json').read_text())
ident = json.loads((root / 'identity.json').read_text())
key = m.get('key')
if not key: print('manifest.json has no key'); sys.exit(1)
der = base64.b64decode(key)
derived = ''.join(chr(ord('a') + int(c, 16)) for c in hashlib.sha256(der).hexdigest()[:32])
if derived != ident['extension_id']:
    print(f'identity mismatch: manifest key -> {derived}, identity.json -> {ident["extension_id"]}'); sys.exit(1)
print(f'identity: ok ({derived})')
