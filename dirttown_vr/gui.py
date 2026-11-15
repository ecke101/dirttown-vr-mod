"""Small VR-only launcher for Windows and Linux."""
from concurrent.futures import ThreadPoolExecutor
import os
import shutil
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog,messagebox,ttk

from .config import LOCAL,ROOT,Config
from .launcher import run
from .platform import is_game_running
from .runtime import inspect_runtime,runtime_environment


class App:
    def __init__(self,root,config):
        self.root=root;self.config=config;self.pool=ThreadPoolExecutor(max_workers=1)
        self.future=None;self.done=None;self.info=None
        root.title('Dirt Town VR Mod')
        root.protocol('WM_DELETE_WINDOW',self.close)
        form=ttk.Frame(root,padding=22);form.pack(fill='both',expand=True)
        ttk.Label(form,text='Dirt Town VR',font=('Sans',21,'bold')).pack(anchor='w')
        ttk.Label(form,text='OpenXR · Windows / Linux · Demo build 26658761').pack(anchor='w',pady=(4,17))
        self.game=tk.StringVar(value=config.game);self.runtime=tk.StringVar(value=config.runtime)
        self.proton=tk.StringVar(value=config.proton)
        self.folder(form,'Game installation',self.game,True)
        self.folder(form,'OpenXR runtime manifest (blank = active runtime)',self.runtime,False)
        if sys.platform!='win32':self.folder(form,'Proton executable (blank = detect)',self.proton,False)
        settings=ttk.LabelFrame(form,text='Rendering and input',padding=12);settings.pack(fill='x',pady=12)
        row=ttk.Frame(settings);row.pack(fill='x')
        ttk.Label(row,text='Resolution scale:').pack(side='left')
        self.scale=tk.StringVar(value=f'{config.scale*100:g}%')
        scale=ttk.Combobox(row,textvariable=self.scale,values=['25%','50%','62.5%','75%','100%','125%','150%'],state='readonly',width=10)
        scale.pack(side='left',padx=10);scale.bind('<<ComboboxSelected>>',lambda _:self.show_resolution())
        ttk.Label(row,text='Graphics:').pack(side='left',padx=(12,0))
        self.graphics=tk.StringVar(value=config.graphics)
        ttk.Combobox(row,textvariable=self.graphics,values=['auto','d3d12','d3d11'],state='readonly',width=10).pack(side='left',padx=10)
        self.resolution=ttk.Label(settings,text='Resolution follows the headset recommendation.')
        self.resolution.pack(anchor='w',pady=(9,6))
        self.controllers=tk.BooleanVar(value=config.controllers);self.taa=tk.BooleanVar(value=config.antialiasing)
        ttk.Checkbutton(settings,text='Use VR controllers',variable=self.controllers).pack(anchor='w')
        ttk.Checkbutton(settings,text='Temporal antialiasing',variable=self.taa).pack(anchor='w')
        ttk.Button(settings,text='Controller settings…',command=self.edit_controls).pack(anchor='w',pady=(8,0))
        self.status=ttk.Label(form,text='Start your OpenXR runtime and connect the headset.',wraplength=690)
        self.status.pack(anchor='w',pady=(4,10))
        buttons=ttk.Frame(form);buttons.pack(fill='x')
        self.actions=[]
        for text,fn in [('Detect headset',self.detect),('Launch VR',lambda:self.execute('game')),
                        ('Test headset',lambda:self.execute('vr-probe.exe')),('GPU self-test',lambda:self.execute('temporal-test.exe'))]:
            button=ttk.Button(buttons,text=text,command=fn);button.pack(side='left',padx=(0,8));self.actions.append(button)
        ttk.Label(form,text='Load a site on the monitor. F9: recenter · F10: VR on/off · F11: antialiasing\n'
                  'Walking: right stick looks. Dozer/loader: right stick controls the implement.\n'
                  'Menus and HUD remain on the monitor.',wraplength=690).pack(anchor='w',pady=16)
        ttk.Button(form,text='Open logs',command=lambda:self.open_path(LOCAL/'logs')).pack(anchor='w')
        root.update_idletasks()
        width=max(760,root.winfo_reqwidth());height=root.winfo_reqheight()
        root.geometry(f'{width}x{height}');root.minsize(width,height)
        root.after(200,self.poll)

    def folder(self,parent,label,var,directory):
        ttk.Label(parent,text=label).pack(anchor='w')
        row=ttk.Frame(parent);row.pack(fill='x',pady=(4,10))
        ttk.Entry(row,textvariable=var).pack(side='left',fill='x',expand=True)
        def browse():
            choice=filedialog.askdirectory(parent=self.root) if directory else filedialog.askopenfilename(parent=self.root)
            if choice:var.set(choice)
        ttk.Button(row,text='Browse…',command=browse).pack(side='right',padx=(8,0))

    def preferences(self):
        result=Config(self.game.get().strip(),self.proton.get().strip(),self.runtime.get().strip(),
                      float(self.scale.get().rstrip('%'))/100,self.controllers.get(),self.taa.get(),self.graphics.get())
        result.save();return result

    def work(self,fn,done):
        if self.future is not None:return
        self.future=self.pool.submit(fn);self.done=done
        for action in self.actions:action.configure(state='disabled')

    def detect(self):
        config=self.preferences();self.status.configure(text='Checking the selected OpenXR runtime…')
        self.work(lambda:inspect_runtime(runtime_environment(config.runtime)),self.detected)

    def detected(self,info):
        self.info=info;self.show_resolution()
        self.status.configure(text=f"{info['runtime']} · {info['headset']}")

    def show_resolution(self):
        if self.info:
            factor=float(self.scale.get().rstrip('%'))/100
            sizes=[' × '.join(str(max(1,round(v*factor))) for v in eye) for eye in self.info['eyes']]
            self.resolution.configure(text='Per-eye target: '+' / '.join(sizes))

    def execute(self,executable):
        config=self.preferences();self.status.configure(text='Running… Load a site on the monitor after the game opens.')
        self.work(lambda:run(config,executable),lambda code:self.status.configure(text=f'Exited with code {code}. Details are in .local/logs.'))

    def poll(self):
        if self.future is not None and self.future.done():
            future=self.future;self.future=None
            try:self.done(future.result())
            except Exception as exc:
                self.status.configure(text=str(exc));messagebox.showerror('Dirt Town VR',str(exc),parent=self.root)
        available=self.future is None and not is_game_running()
        for action in self.actions:action.configure(state='normal' if available else 'disabled')
        self.root.after(500,self.poll)

    def edit_controls(self):
        LOCAL.mkdir(parents=True,exist_ok=True)
        file=LOCAL/'controls.ini'
        if not file.exists():shutil.copy2(ROOT/'controls.example.ini',file)
        self.open_path(file)

    @staticmethod
    def open_path(path):
        if path.suffix=='':path.mkdir(parents=True,exist_ok=True)
        if sys.platform=='win32':os.startfile(path)
        else:subprocess.Popen(['xdg-open',str(path)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

    def close(self):
        self.pool.shutdown(wait=False,cancel_futures=True);self.root.destroy()


def main(config=None):
    root=tk.Tk();App(root,config or Config.load());root.mainloop();return 0
