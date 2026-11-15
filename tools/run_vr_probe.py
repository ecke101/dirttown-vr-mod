#!/usr/bin/env python3
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from dirttown_vr.__main__ import main
if __name__=='__main__':
    temporal='--temporal-test' in sys.argv
    if temporal:sys.argv.remove('--temporal-test')
    sys.argv.append('gpu-test' if temporal else 'probe')
    raise SystemExit(main())
