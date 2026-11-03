import os, glob, subprocess, sys, numpy as np
sys.path.insert(0,'/home/claude/elite-wsp')
from PIL import Image, ImageDraw, ImageFont
from elite_wsp.net import Net; from elite_wsp.features import features
V='vid'; FPS=25; MONO='/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'; BOLD='/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
ENC=['-c:v','libx264','-crf','18','-preset','veryfast','-pix_fmt','yuv420p']
YEL,RED,CYAN,GREEN,DIM,FG=(242,209,58),(232,69,60),(56,214,232),(95,220,95),(106,111,120),(232,232,232)
net=Net('/home/claude/elite-wsp/elite_pilot.npz')
log={}
for l in open('/tmp/claude-1000/swarm.log'):
    t=l.split(); log[int(t[0])]=t
def bbox(a):
    yel=(a[:,:,0]>200)&(a[:,:,1]>200)&(a[:,:,2]<60)
    rows=np.where(yel.sum(1)>200)[0]; cols=np.where(yel.sum(0)>200)[0]
    if len(rows)<2 or len(cols)<2: return None
    return cols[0],rows[0],cols[-1],rows[-1]
def good(a):
    b=bbox(a); return b is not None and 480<=b[3]-b[1]<=520 and 500<=b[2]-b[0]<=525
f11=ImageFont.truetype(MONO,11); f13=ImageFont.truetype(MONO,13); f16=ImageFont.truetype(BOLD,16); f22=ImageFont.truetype(MONO,21)
LN=['LAYER 1  the geometry','LAYER 2  roll, pitch, fire geometry','LAYER 3  fire, range bands, dead-zone kick','OUTPUT  stick, fire, speed']
def feats(fr):
    t=None
    for k in range(fr,fr-3,-1):
        if k in log: t=log[k]; break
    if t is None: return features(None,40,255,255,0,0), None
    x,y,z=int(t[1]),int(t[2]),int(t[3]); nv=[float(t[4]),float(t[5]),float(t[6])]
    tgt=dict(x=x,y=y,z=z,nosev=nv,speed=int(t[7]))
    return features(tgt,int(t[8]),int(t[9]),int(t[10]),int(t[11]),float(t[12])), t
def panel(d, x0, y0, acts, t):
    y=y0
    for l in range(4):
        d.text((x0,y),LN[l],font=f16,fill=CYAN); y+=22
        names=net.names[l]; a=acts[l]; cw,ch=122,19; per=4
        for j,n in enumerate(names):
            cx=x0+(j%per)*cw; cy=y+(j//per)*ch; v=float(a[j])
            if l==3: lit=1.0
            else: lit=min(1.0,abs(v)/(2.0 if l==0 else 1.0))
            on=v>0
            bg=tuple(int(c) for c in (np.array(YEL)*(0.15+0.6*lit) if on else np.array((0,0,0))))
            d.rectangle((cx,cy,cx+cw-3,cy+ch-2),fill=bg,outline=(30,34,41))
            d.text((cx+3,cy+3),n[:17],font=f11,fill=(255,255,255) if on else DIM)
            if l==3: d.text((cx+72,cy+3),str(int(round(v))),font=f11,fill=(26,22,0))
        y+=((len(names)+per-1)//per)*ch+8
    return y
def compose(a, fr, caption=None, title=False):
    im=Image.new('RGB',(1280,720),'black'); d=ImageDraw.Draw(im)
    b=bbox(a); b=b if b and good(a) else (128,80,640,580)
    g=Image.fromarray(a[b[1]-2:b[3]+3,b[0]-2:b[2]+3]).resize((690,673),Image.NEAREST); im.paste(g,(30,8))
    f,t=feats(fr); acts=net.forward(f,trace=True)[1]
    y=panel(d,760,14,acts,t)
    if t: d.text((760,y+2),f"x {t[1]:>6}  y {t[2]:>6}  z {t[3]:>6}   stick {t[13]} {t[14]}  {"FIRE" if t[15]=="1" else "    "}  speed {t[16]}",font=f11,fill=FG)
    else: d.text((760,y+2),'no target in the bubble',font=f11,fill=DIM)
    if title:
        band=Image.new('RGB',(1280,720),'black'); im=Image.blend(im,band,0.2); d=ImageDraw.Draw(im)
        ov=Image.new('RGBA',(1280,720),(0,0,0,0)); ImageDraw.Draw(ov).rectangle((0,540,1280,720),fill=(0,0,0,215)); im=Image.alpha_composite(im.convert('RGBA'),ov).convert('RGB'); d=ImageDraw.Draw(im)
        d.line((0,540,1280,540),fill=YEL)
        d.text((30,562),'WEIGHT-SPACE PILOT',font=ImageFont.truetype(BOLD,62),fill=YEL)
        d.text((34,636),'2,053 hand-written weights fly the real Elite. No training.',font=f22,fill=FG)
    elif caption:
        d.rectangle((0,672,1280,720),fill=(0,0,0)); d.line((0,672,1280,672),fill=(30,34,41)); d.text((24,686),caption,font=f22,fill=YEL)
    return im
RAW='/tmp/claude-1000/swarm.raw'; FR=768*640*3
def raw(k):
    with open(RAW,'rb') as f: f.seek(k*FR); return np.frombuffer(f.read(FR),np.uint8).reshape(640,768,3)
def scene(name, s0, s1, caps):
    last=None; n=0
    for k in range(s0*25, s1*25):
        a=raw(k); fr=2*k
        if good(a) or last is None: last=a
        cap=caps[min(len(caps)-1, n//(25*8))]
        compose(last,fr,cap).save(f'{V}/{name}_{n:04d}.jpg',quality=92); n+=1
    subprocess.run(['ffmpeg','-y','-loglevel','error','-framerate','25','-i',f'{V}/{name}_%04d.jpg',*ENC,f'{V}/{name}.tmp.mp4'],check=True); os.rename(f'{V}/{name}.tmp.mp4',f'{V}/{name}.mp4')
    for i in range(n): os.remove(f'{V}/{name}_{i:04d}.jpg')
    print(name, n/25, 's')
# thumbnail: the busiest good frame in the window
cands=[(k,raw(k)) for k in range(100,700,4)]
k,a=max(((a[60:520,70:700].sum(2)>0).sum(),k,a) for k,a in cands if good(a) and 2*k in log)[1:]
th=compose(a,2*k,title=True); th.save('thumb.png'); th.save('/home/claude/elite-wsp/thumbnail.png')
subprocess.run(['ffmpeg','-y','-loglevel','error','-framerate','25','-i','thumb.png','-vf','format=yuv420p,loop=-1:1,trim=duration=1.2',*ENC,f'{V}/open.tmp.mp4'],check=True); os.rename(f'{V}/open.tmp.mp4',f'{V}/open.mp4')
scene('swarmA',2,15,['Witchspace: 3 Thargoids and 6 Thargons, a snapshot from a Dangerous-rated C-agent career.','The network flies: stick, laser, throttle. The C code keeps missiles, ECM and the career.'])
scene('swarmB',64,100,['Layer 1 reads the target. Layer 2 steers. Layer 3 gates fire and sets the gains by range.',"20 swarms replayed: all cleared, no deaths, 13.8 points/min to the C pilot's 20.3.","20 pirate packs of 4-5: 12.5 to the C pilot's 13.1. Two weight edits got it here, no training.",'2,053 weights, every one named. Read a unit, ablate it, edit it.'])
