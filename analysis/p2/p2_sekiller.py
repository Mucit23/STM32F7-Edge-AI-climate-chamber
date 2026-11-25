exec(open("p2_basamak_analizi.py").read().split("rows=[]")[0])
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'axes.grid':True,'grid.color':'#e5e5e5','grid.linewidth':0.6})
C1,C2,TX='#257de0','#f47230','#333333'
amps=[(150,1,13),(300,5,17),(500,9,21),(-500,3,15),(-750,7,19),(-1000,11,23)]
fig,axs=plt.subplots(2,3,figsize=(12,6.4),sharex=True)
f1=lambda t,K,T,L: K*(1-np.exp(-np.clip(t-L,0,None)/T))
for ax,(u,pa,pb) in zip(axs.flat,amps):
    taus=[]
    for ph,c,lab in [(pa,C1,'1. tekrar'),(pb,C2,'2. tekrar')]:
        s=df[df.phase==ph]; pre=df[df.phase==ph-1]; y0=pre.T_air_dht.iloc[-120:].mean()
        t=s.t.values-s.t.values[0]; y=s.T_air_dht.values-y0
        p,_=curve_fit(f1,t,y,p0=[y[-1],1000,60],maxfev=20000)
        ax.plot(t/60,y,color=c,lw=2,label=lab)
        ax.plot(t/60,f1(t,*p),color=TX,lw=1,ls='--',label='1. derece model' if ph==pb else None)
        taus.append(p[1]/60)
    ax.set_title(f'{u/10:+.0f} %   (τ = {taus[-2]:.0f} / {taus[-1]:.0f} dk)',color=TX)
    ax.axhline(0,color='#999',lw=0.8)
for ax in axs[1]: ax.set_xlabel('Basamaktan sonra süre (dk)')
for ax in axs[:,0]: ax.set_ylabel('Hava sıcaklığı değişimi (K)')
axs[0,0].legend(loc='lower right',fontsize=9,frameon=False)
fig.tight_layout(); fig.savefig('P2_basamak_tepkileri.png',dpi=150)
# gain curve
U1=[-100,-75,-50,-25,0,25,50,75]; Y1=[-12.6,-8.2,-3.75,-0.3,None,16.2,29.8,41.8]
r=pd.read_csv('p2_ozet.csv')
fig,ax=plt.subplots(figsize=(9,4.2))
for sel in (lambda u:u<=0, lambda u:u>0):
    xs=[u for u,y in zip(U1,Y1) if y is not None and sel(u)]; ys=[y for u,y in zip(U1,Y1) if y is not None and sel(u)]
    ax.plot(xs,ys,'o-',color=C1,lw=2,ms=8,label='P1 #2: hava − ortam, 60 dk sonunda' if sel(-1) else None)
g=r.groupby('u').dT90.agg(['mean','min','max']).reset_index()
for sel in (g.u<0, g.u>0):
    gg=g[sel]; ax.errorbar(gg.u,gg['mean'],yerr=[gg['mean']-gg['min'],gg['max']-gg['mean']],fmt='s-',color=C2,lw=2,ms=8,capsize=4,label='P2: basamak öncesine göre değişim, 90 dk sonunda (2 tekrar, aralık)' if (gg.u<0).all() else None)
ax.axhline(0,color='#999',lw=0.8); ax.axvline(0,color='#999',lw=0.8)
ax.set_xlabel('Peltier komutu (%)'); ax.set_ylabel('Sıcaklık değişimi (K)')
ax.legend(frameon=False,loc='upper left'); fig.tight_layout(); fig.savefig('P2_kazanc_egrisi.png',dpi=150)
