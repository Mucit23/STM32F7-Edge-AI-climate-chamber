import numpy as np
U=np.array([-1000,-750,-500,-250,0,150,300,500,750,1000]); Y=np.array([-12.6,-9.2,-4.3,0.3,2.0,11.5,20.4,30.1,42,44])
def sim(kp,ti,sched=False,tau=1140,L=60,amb=21):
    sps=[22,32,17,27,37,24]; n=len(sps)*9000
    y=23-amb; integ=0; buf=[0]*L; iae=0; os_=[]; sat=0
    out=[]
    for k in range(n):
        sp=sps[k//9000]; pv=y+amb; e=sp-pv
        kpe=kp*(4.0 if (sched and integ<0) else 1.0)
        p=kpe*e; integ+= kpe/ti*e
        integ=np.clip(integ,-1000-p,1000-p) if True else integ
        u=np.clip(p+integ,-1000,1000); sat+= abs(u)>=999
        buf.append(u); ua=buf.pop(0)
        ys=np.interp(ua,U,Y); y+= (ys-y)/tau
        iae+=abs(e); out.append(pv)
    out=np.array(out).reshape(len(sps),9000)
    ov=[ (o.max()-s if s>prev else s-o.min()) for o,s,prev in zip(out,sps,[23]+sps[:-1])]
    st=[ np.argmax(np.all(np.abs(o[i:]-s)<0.5) if False else 0) for o,s in zip(out,sps)]
    # settling time to +-0.5K
    ts=[]
    for o,s in zip(out,sps):
        bad=np.where(np.abs(o-s)>0.5)[0]; ts.append((bad[-1]+1)/60 if len(bad) else 0)
    return iae/n, max(ov), ts
for kp,ti,sc in [(150,600,False),(50,1140,False),(100,1140,False),(150,1140,False),(60,1140,True)]:
    a,o,ts=sim(kp,ti,sc); print(kp,ti,sc,'IAE %.2f K, max asim %.2f K, oturma(dk)'%(a,o),[round(x) for x in ts])
