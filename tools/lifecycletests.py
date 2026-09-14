import argparse,shutil,struct
from desktoptests import Guest,ROOT
from fscp import do_copy

def run(memory,mode):
    folder=ROOT/'out/lifecycle-tests'/f'{memory}mb-{mode}';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    do_copy(str(img),str(ROOT/'kexts/lifecycletest.kx'),'sys/lifetest.kx')
    seed=folder/'autoexec';seed.write_bytes(('kext load sys/lifetest.kx\nlifecycle '+mode+'\n').encode());do_copy(str(img),str(seed),'autoexec')
    b=bytearray(img.read_bytes());struct.pack_into('<IBBBB',b,256*512,0x47464346,1,0,2,0);img.write_bytes(b)
    g=Guest(folder,img,'pentium2',memory)
    try:
        g.wait('LIFECYCLE DONE:',100);log=g.serial();print(log,flush=True)
        assert 'LIFECYCLE DONE: 0 failures' in log
    finally:g.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=8);p.add_argument('--mode',choices=['timers','graphics','open-fault'],default='timers');a=p.parse_args();run(a.memory,a.mode)
