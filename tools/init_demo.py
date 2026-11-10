#!/usr/bin/env python3
"""Create a working data directory without overwriting existing files."""
from pathlib import Path
import shutil
import sys
root = Path(__file__).resolve().parents[1]
destination = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'runtime'
destination.mkdir(parents=True, exist_ok=False)
for source in (root / 'fixtures').iterdir():
    if source.is_file(): shutil.copy2(source, destination)
print(f'Demo data copied to {destination}')
