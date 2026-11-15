import argparse
import json

from .config import Config
from .launcher import run
from .runtime import inspect_runtime,runtime_environment


def main():
    parser=argparse.ArgumentParser(description='Dirt Town OpenXR VR mod')
    parser.add_argument('--game',help='Dirt Town Demo installation folder')
    parser.add_argument('--proton',help='Linux Proton executable or installation folder')
    parser.add_argument('--runtime',help='OpenXR runtime manifest override for this launch only')
    parser.add_argument('--resolution-scale',type=float,help='Fraction of runtime-recommended width/height, 0.25–1.5')
    parser.add_argument('--no-controllers',action='store_true')
    parser.add_argument('--no-taa',action='store_true')
    parser.add_argument('--graphics',choices=('auto','d3d12','d3d11'))
    parser.add_argument('--diagnostic',action='store_true',help='Run the game on the monitor with hook diagnostics only')
    parser.add_argument('command',nargs='?',choices=('gui','launch','runtime','probe','gpu-test'),default='gui')
    args=parser.parse_args()
    config=Config.load()
    for key,arg in [('game','game'),('proton','proton'),('runtime','runtime'),('scale','resolution_scale'),('graphics','graphics')]:
        value=getattr(args,arg)
        if value is not None:setattr(config,key,value)
    if args.no_controllers:config.controllers=False
    if args.no_taa:config.antialiasing=False
    try:
        if args.command=='gui':
            from .gui import main as gui_main
            return gui_main(config)
        if args.command=='runtime':
            print(json.dumps(inspect_runtime(runtime_environment(config.runtime)),indent=2));return 0
        if args.command=='launch':return run(config,diagnostic=args.diagnostic)
        return run(config,'vr-probe.exe' if args.command=='probe' else 'temporal-test.exe')
    except (RuntimeError,OSError,ValueError) as exc:parser.exit(1,str(exc)+'\n')


if __name__=='__main__':raise SystemExit(main())
