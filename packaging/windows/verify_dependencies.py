"""Check PE imports against bundled DLL exports before creating the portable archive."""
from pathlib import Path
import re
import subprocess


def verify(app, objdump, system_dlls):
    files = sorted(Path(app).rglob('*'))
    files = [f for f in files if f.suffix.lower() in ('.dll', '.exe')]
    libraries = {f.name.lower(): f for f in files if f.suffix.lower() == '.dll'}
    details = {f: subprocess.check_output([objdump, '-p', str(f)], text=True) for f in files}
    errors = []
    for binary, output in details.items():
        # Stop at the end of the import section; export names aren't imports.
        imports = output.split('The Import Tables', 1)
        if len(imports) < 2:
            continue
        imports = re.split(r'The (?:Export|Function) Table', imports[1], maxsplit=1)[0]
        for block in imports.split('DLL Name:')[1:]:
            name = block.split()[0]
            if name.lower() in system_dlls or name.lower().startswith(('api-ms-', 'ext-ms-')):
                continue
            library = libraries.get(name.lower())
            if library is None:
                errors.append(f'{binary.name}: missing {name}')
                continue
            exports = details[library].split('[Ordinal/Name Pointer] Table', 1)
            names = set()
            if len(exports) == 2:
                names = set(re.findall(r'^\s*\[\s*\d+\]\s+(\S+)\s*$', exports[1], re.M))
            for symbol in re.findall(r'^\s*[0-9a-fA-F]+\s+\d+\s+(\S+)\s*$', block, re.M):
                if symbol not in names:
                    errors.append(f'{binary.name}: {name} does not export {symbol}')
    if errors:
        raise RuntimeError('Incompatible Windows dependencies:\n' + '\n'.join(errors))
    print(f'Validated imports for {len(files)} Windows binaries')
