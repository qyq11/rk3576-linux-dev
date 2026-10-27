"""Read only the original standalone Makefile's actual dependency graph and flags."""
import re
import shlex


def parse_makefile(text):
    logical = text.replace('\\\n', ' ')
    values = dict(re.findall(r'^([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*)$', logical, re.M))

    def words(name):
        value = values[name]
        for _ in range(10):
            if '$(' not in value:
                break
            def replace(match):
                key = match.group(1)
                if key not in values and key != 'SUBLIBS':
                    raise RuntimeError('Unknown original Makefile variable: ' + key)
                return values.get(key, '')
            value = re.sub(r'\$\(([A-Za-z_][A-Za-z_0-9]*)\)', replace, value)
        if '$(' in value:
            raise RuntimeError('Unresolved original Makefile variable: ' + name)
        return shlex.split(value)

    flags = {name: words(name) for name in ('DEFINES', 'CXXFLAGS', 'INCPATH', 'LFLAGS', 'LIBS', 'OBJECTS')}
    rules, recipes = {}, {}
    current = []
    for line in logical.splitlines():
        if line.startswith('\t'):
            for target in current:
                recipes.setdefault(target, []).append(line[1:])
            continue
        match = re.match(r'^([^:=]+):\s*(.*)$', line)
        current = []
        if match and not line.startswith('#'):
            current = shlex.split(match.group(1))
            prerequisites = shlex.split(match.group(2))
            for target in current:
                rules.setdefault(target, []).extend(prerequisites)

    def closure(targets):
        visited, pending = set(), list(targets)
        while pending:
            token = pending.pop()
            if token in visited:
                continue
            if '$(' in token:
                raise RuntimeError('Unresolved prerequisite: ' + token)
            visited.add(token)
            pending.extend(rules.get(token, ()))
        return sorted(visited)

    objects = flags['OBJECTS']
    if len(objects) != 59 or len(set(objects)) != 59 or any(name not in rules for name in objects):
        raise RuntimeError('Expected 59 original standalone object rules')
    return {'flags': flags, 'dependencies': closure([*objects, 'Makefile']),
            'object_dependencies': {name: closure([name]) for name in objects},
            'object_rules': {name: {'prerequisites': rules[name], 'recipes': recipes.get(name, [])}
                             for name in objects}}
