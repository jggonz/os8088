#!/usr/bin/env python3
"""8BitBike's build is independent of its offline reference (SPEC.md §102).

Imported text and generated art are committed inputs. Only offline refresh
commands may read the reference: neither application code nor the build
compiler and its imports may do so. Comments and provenance are allowed to
name source files. This gate checks executable code, not historical wording.
Its negative controls exercise the same scanner as the real tree.
"""
import ast
import os
from pathlib import Path
import re
import sys
import sysconfig
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = re.compile(r"EXCITEBIKE_(?:REF|SOURCE)|NES-Games-Disassembly|CHR_ROM(?:\.chr)?|bank_FF\.asm")
OFFLINE_MODULES = {"exbref", "excitebike_import", "exbnes", "excitebike_art"}
REFRESH_TARGETS = {"excitebike-import", "excitebike-fixtures", "excitebikeimport",
                   "excitebike-oracle-check"}
OFFLINE_TOOL = re.compile(r"(?:exbref|excitebike_import|excitebike_art)\.py|tools/exbnes/|"
                          r"(?:^|\s)-m\s+(?:tools\.)?(?:exbref|excitebike_import|exbnes)(?:\.|\s|$)")


def committed_reference_code(source):
    # Exact, committed DrMarco paths are allowed; suffixes and traversal are not.
    return re.sub(r'reference/drmario/(?:CHR_ROM\.chr|bank_FF\.asm)(?![\w./-])',
                  '<committed-drmarco-source>', source)


def offline_module(module):
    return bool(OFFLINE_MODULES.intersection(module.split('.')))


def python_violations(source, name):
    tree = ast.parse(source)
    errors, imports, prose = [], [], set()
    import_functions = {'__import__'}
    # ast.walk visits parents first, so standalone strings can be excluded in
    # this single traversal rather than walking the compiler three times.
    for node in ast.walk(tree):
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant):
            if isinstance(node.value.value, str):
                prose.add(id(node.value))
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            if id(node) not in prose:
                match = REFERENCE.search(committed_reference_code(node.value))
                if match:
                    errors.append(f"{name}:{node.lineno}: executable reference {match.group()}")
        elif isinstance(node, (ast.Import, ast.ImportFrom)):
            if isinstance(node, ast.Import):
                names = [(alias.name, 0, True) for alias in node.names]
            else:
                base = node.module or ''
                names = [(base, node.level, True)]
                names += [((base + '.' if base else '') + alias.name, node.level, False)
                          for alias in node.names if alias.name != '*']
                if node.module == 'importlib':
                    import_functions.update(alias.asname or alias.name for alias in node.names
                                            if alias.name == 'import_module')
            for module, level, required in names:
                imports.append((module, node.lineno, level, required))
                if offline_module(module):
                    errors.append(f"{name}:{node.lineno}: offline module {module} on build path")
        elif isinstance(node, ast.Call) and (
                isinstance(node.func, ast.Attribute) and node.func.attr == 'import_module' or
                isinstance(node.func, ast.Name) and node.func.id in import_functions):
            if node.args and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
                module = node.args[0].value
                imports.append((module, node.lineno, 0, True))
                if offline_module(module):
                    errors.append(f"{name}:{node.lineno}: offline dynamic import {module}")
            else:
                errors.append(f"{name}:{node.lineno}: unresolved dynamic import on build path")
    return errors, imports


def assembly_violations(source, name):
    errors = []
    # NASM's semicolon starts a comment outside quoted literals.
    for lineno, line in enumerate(source.splitlines(), 1):
        code = re.split(r";(?=(?:[^'\"]|'[^']*'|\"[^\"]*\")*$)", line, maxsplit=1)[0]
        match = REFERENCE.search(committed_reference_code(code))
        if match:
            errors.append(f"{name}:{lineno}: executable reference {match.group()}")
    return errors


def c_violations(source, name):
    # Preserve literals and line numbers while removing C/C++ comments.
    tokens = r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/'''
    code = re.sub(tokens, lambda m: '\n' * m.group().count('\n')
                  if m.group().startswith(('//', '/*')) else m.group(), source, flags=re.S)
    return [f"{name}:{number}: executable reference {match.group()}"
            for number, line in enumerate(code.splitlines(), 1)
            if (match := REFERENCE.search(committed_reference_code(line)))]


def make_violations(source, name='Makefile'):
    """Refresh recipes are explicit exceptions; no build may call them.

    Scan the whole Makefile, including assignments and prerequisites, so a
    reference hidden in a shared variable cannot escape a game-block check.
    Logical lines preserve continuation context and their original line number.
    """
    errors = []
    targets = set()
    pending = ""
    first = 0
    recipe = False
    for lineno, line in enumerate(source.splitlines(), 1):
        if not pending:
            first = lineno
            recipe = line.startswith('\t')
        pending += line.rstrip('\\') if line.endswith('\\') else line
        if line.endswith('\\'):
            pending += ' '
            continue
        logical = pending
        pending = ""
        code = logical if recipe else logical.split('#', 1)[0]
        if not code.strip():
            continue
        fragments = [(code, recipe)]
        if not recipe:
            match = re.match(r"^([^:=]+):(?![=])", code)
            if match:
                targets = set(match.group(1).split())
                if ';' in code:
                    header, command = code.split(';', 1)
                    fragments = [(header, False), (command, True)]
            else:
                targets = set()
        for code, is_recipe in fragments:
            if is_recipe and targets and targets <= REFRESH_TARGETS:
                continue
            match = REFERENCE.search(committed_reference_code(code))
            if match:
                errors.append(f"{name}:{first}: build reference {match.group()}")
            # Scan assignments too: a shared tool variable is an indirect call.
            if OFFLINE_TOOL.search(code):
                errors.append(f"{name}:{first}: offline tool on build path")
            if any(target in code and re.search(r"(?<![\w-])" + re.escape(target) + r"(?![\w-])", code)
                   for target in REFRESH_TARGETS):
                if not (not is_recipe and targets and targets <= REFRESH_TARGETS):
                    if not code.startswith('.PHONY:'):
                        errors.append(f"{name}:{first}: refresh target on build path")
    return errors


def compiler_violations(root):
    """Follow local imports, enforcing the stdlib boundary transitively."""
    errors = []
    pending = [root / 'tools/excitebike_assets.py']
    visited = set()
    stdlib = set(getattr(sys, 'stdlib_module_names', ())) | set(sys.builtin_module_names)
    if not getattr(sys, 'stdlib_module_names', None):  # Python 3.9 on macOS
        directory = Path(sysconfig.get_path('stdlib'))
        stdlib.update(p.stem for p in directory.glob('*.py'))
        stdlib.update(p.name for p in directory.iterdir() if p.is_dir() and
                      (p / '__init__.py').is_file())
        dynload = directory / 'lib-dynload'
        if dynload.is_dir():
            stdlib.update(p.name.split('.')[0] for p in dynload.iterdir())
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        name = str(path.relative_to(root))
        found, tree = python_violations(path.read_text(), name)
        errors.extend(found)
        for module, lineno, level, required in tree:
            if level:
                search_root = root / 'tools' if path.is_relative_to(root / 'tools') else root
                package = list(path.parent.relative_to(search_root).parts)
                if level > len(package):
                    errors.append(f"{name}:{lineno}: relative import outside local package")
                    continue
                prefix = package[:len(package) - level + 1]
                module = '.'.join(prefix + ([module] if module else []))
            if offline_module(module):
                continue  # python_violations already names the forbidden import.
            found = False
            for directory in (root / 'tools', root):
                parts = module.split('.')
                local = directory.joinpath(*parts)
                file = local.with_suffix('.py')
                package = local / '__init__.py'
                if file.is_file() or local.is_dir():
                    found = True
                    if file.is_file():
                        pending.append(file)
                    elif package.is_file():
                        pending.append(package)
                    # Importing pkg.child executes every parent __init__.py.
                    for i in range(1, len(parts)):
                        parent = directory.joinpath(*parts[:i]) / '__init__.py'
                        if parent.is_file():
                            pending.append(parent)
                    break
            # A local package takes precedence even when its name is stdlib.
            if required and not found and (level or module.split('.')[0] not in stdlib):
                errors.append(f"{name}:{lineno}: non-stdlib or missing local import {module}")
    return errors, visited


def makefile_violations(root):
    """Include fragments are build code too, even when stored outside apps/."""
    errors, visited = [], set()
    pending = [root / 'Makefile']
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        source = path.read_text()
        errors.extend(make_violations(source, str(path.relative_to(root))))
        for match in re.finditer(r'^\s*(-?include|sinclude)\s+([^#\n]+)', source, re.M):
            for included in match[2].split():
                if '$' in included:
                    continue  # Variable-expanded include paths are not statically resolvable.
                file = root / included
                if file.is_file():
                    pending.append(file)
                elif match[1] == 'include':
                    errors.append(f'{path.relative_to(root)}: missing build fragment {included}')
    return errors, visited


def audit(root):
    errors, compiler = compiler_violations(root)
    make_errors, makefiles = makefile_violations(root)
    errors.extend(make_errors)
    count = 0
    for path in sorted((root / 'apps').rglob('*')):
        if path.suffix not in ('.asm', '.inc', '.py', '.c', '.h') or not path.is_file():
            continue
        count += 1
        source = path.read_text()
        if path.name.startswith('Makefile'):
            if path not in makefiles:
                errors.extend(make_violations(source, str(path.relative_to(root))))
            continue
        # Most applications contain no reference tokens. Avoid parsing them.
        if any(word in source for word in ('EXCITEBIKE_REF', 'EXCITEBIKE_SOURCE',
                                          'NES-Games-Disassembly', 'CHR_ROM', 'bank_FF.asm')) or (path.suffix == '.py' and
                                       any(m in source for m in OFFLINE_MODULES)):
            name = str(path.relative_to(root))
            if path.suffix == '.py':
                errors.extend(python_violations(source, name)[0])
            elif path.suffix in ('.c', '.h'):
                errors.extend(c_violations(source, name))
            else:
                errors.extend(assembly_violations(source, name))
    return errors, len(compiler), count


def main():
    errors, compilers, apps = audit(ROOT)
    check(compilers >= 2 and apps >= 100, 'the fence covers compiler imports and application code',
          got=(compilers, apps), want='>=2 compiler modules, >=100 application sources')
    check(not errors, 'the build reads committed inputs without a reference',
          got='\n'.join(errors), want='', why='refresh with make excitebike-import offline')
    planted = (ROOT / 'tools/excitebike_assets.py').read_text() + (
        '\n_reference = os.environ["EXCITEBIKE_REF"]\n'
        'open(os.path.join(_reference, "bank_FF.asm"))\n')
    check(bool(python_violations(planted, 'tools/excitebike_assets.py')[0]),
          'negative control: a planted compiler reference read is rejected')
    for source in ('import exbref\n', 'from excitebike_import import main\n'):
        check(bool(python_violations(source, 'compiler.py')[0]),
              'negative control: an offline reader imported by the compiler is rejected')
    check(not python_violations('"""EXCITEBIKE_REF bank_FF.asm"""\n# CHR_ROM.chr\nx = 1\n',
                                'provenance.py')[0],
          'provenance comments do not become executable reads')
    check(bool(assembly_violations("file: db 'CHR_ROM.chr',0\n", 'apps/game.asm')),
          'negative control: an application reference filename is rejected')
    check(not assembly_violations('; bank_FF.asm\nx: db 0 ; EXCITEBIKE_REF\n', 'game.asm'),
          'assembly provenance comments are permitted')
    check(bool(make_violations('all:\n\tpython3 tools/excitebike_import.py\n')),
          'negative control: a build recipe invoking the importer is rejected')
    check(bool(make_violations('EXB_DATA := $(EXCITEBIKE_REF)/bank_FF.asm\nall: $(EXB_DATA)\n')),
          'negative control: a shared Makefile reference variable is rejected')
    check(bool(make_violations('all: excitebike-import\n')),
          'negative control: refresh may not become a build prerequisite')
    check(not make_violations('excitebike-import:\n\tpython3 tools/excitebike_import.py\n'),
          'explicit offline refresh recipes are permitted')
    for source in ('all: ; python3 tools/excitebike_import.py\n',
                   'all:\n\tpython3 -m exbnes.record --refresh\n',
                   'IMPORTER := tools/excitebike_import.py\nall:\n\tpython3 $(IMPORTER)\n',
                   'RECORDER := tools/exbnes/record.py\nall:\n\tpython3 $(RECORDER)\n'):
        check(bool(make_violations(source)),
              'negative control: inline, module-form and variable-aliased refresh calls are rejected')
    check(not make_violations('excitebike-fixtures: ; python3 -m exbnes.record --refresh\n'),
          'an explicit offline target may use an inline module-form recipe')
    for source in ('import importlib\nimportlib.import_module("exbref")\n',
                   'from importlib import import_module as load\nload("exbref")\n',
                   '__import__("exbref")\n', 'from tools.exbref import Reader\n',
                   'from tools import exbref\n'):
        check(bool(python_violations(source, 'compiler.py')[0]),
              'negative control: dynamic and qualified offline imports are rejected')
    check(bool(c_violations('getenv("EXCITEBIKE_REF");\n', 'apps/game.c')),
          'negative control: an application C reference read is rejected')
    check(not c_violations('/* EXCITEBIKE_REF\nbank_FF.asm */\n// CHR_ROM.chr\nchar *s="ok";\n',
                           'apps/game.c'), 'C provenance comments are permitted')
    check(bool(c_violations('char *s="http://example/CHR_ROM.chr";\n', 'apps/game.c')),
          'C comment stripping preserves comment markers inside strings')
    for scanner in (assembly_violations, c_violations):
        check(not scanner('data: "reference/drmario/CHR_ROM.chr"\n', 'committed-source'),
              'DrMarco committed reference inputs remain permitted')
        check(bool(scanner('data: "reference/excitebike/CHR_ROM.chr"\n', 'reference-source')),
              'the committed DrMarco exemption does not cover Excitebike')
    with tempfile.TemporaryDirectory(prefix='exb-fence-') as temporary:
        root = Path(temporary)
        tools = root / 'tools'
        (tools / 'json').mkdir(parents=True)
        (tools / 'pkg').mkdir()
        (root / 'apps').mkdir()
        (root / 'Makefile').write_text('all:\n')
        compiler = tools / 'excitebike_assets.py'
        (tools / 'excitebike_audio.py').write_text('x = 1\n')
        (tools / 'json/__init__.py').write_text('import os\nx=os.environ["EXCITEBIKE_REF"]\n')
        compiler.write_text('import excitebike_audio\nimport json\n')
        errors, visited = compiler_violations(root)
        check(bool(errors) and tools / 'json/__init__.py' in visited,
              'negative control: a local package shadowing stdlib is audited')
        (tools / 'pkg/__init__.py').write_text('x = 1\n')
        (tools / 'pkg/leaf.py').write_text('from . import helper\n')
        (tools / 'pkg/helper.py').write_text('import os\nx=os.environ["EXCITEBIKE_REF"]\n')
        for imported in ('import pkg.leaf\n', 'from pkg import leaf\n'):
            compiler.write_text('import excitebike_audio\n' + imported)
            errors, visited = compiler_violations(root)
            check(bool(errors) and tools / 'pkg/helper.py' in visited,
                  'negative control: dotted and relative package imports retain their closure')
        (tools / 'pkg/leaf.py').write_text('x = 1\n')
        (tools / 'pkg/__init__.py').write_text('import os\nx=os.environ["EXCITEBIKE_REF"]\n')
        compiler.write_text('import excitebike_audio\nimport pkg.leaf\n')
        check(bool(compiler_violations(root)[0]),
              'negative control: importing a child audits its package initializer')
        compiler.write_text('import excitebike_audio\nimport unavailable_dependency\n')
        check(bool(compiler_violations(root)[0]),
              'negative control: an unknown static dependency is refused')
        compiler.write_text('import excitebike_audio\n')
        (root / 'apps/game.c').write_text('getenv("EXCITEBIKE_REF");\n')
        check(bool(audit(root)[0]),
              'negative control: the application walk includes C source files')
        (root / 'apps/game.c').write_text('/* EXCITEBIKE_REF is provenance only. */\n')
        (root / 'Makefile').write_text('include tools/fragment.mk\nall:\n')
        (tools / 'fragment.mk').write_text('all: ; python3 tools/excitebike_import.py\n')
        check(bool(audit(root)[0]),
              'negative control: an included Makefile fragment cannot invoke the importer')
    done('t_excitebike_clean')


if __name__ == '__main__':
    main()
