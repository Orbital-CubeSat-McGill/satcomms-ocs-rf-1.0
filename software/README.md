Software for the green board

## Tooling

Zephyr builds need the local venv (jsonschema + west deps):

```bash
cd software
source .venv/bin/activate   # or call ./.venv/bin/west …
```

Bare Homebrew `west` → CMake error `Missing jsonschema dependency`.
