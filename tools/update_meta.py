from pathlib import Path
import zlib
import json
import re

root = Path(__file__).resolve().parents[1]
image = (root / 'built/flopnix.ku').read_bytes()
version = re.search(r'#define OS_VER\s+"([^"]+)"', (root / 'src/os.h').read_text()).group(1)
abi = int(re.search(r'#define KAPI_VERSION\s+(\d+)', (root / 'src/kapi.h').read_text()).group(1))
(root / 'built/flopnix-update.json').write_text(json.dumps({
    'format': 2, 'version': version, 'api': abi, 'size': len(image),
    'crc32': f'{zlib.crc32(image):08x}',
}, indent=2) + '\n')
