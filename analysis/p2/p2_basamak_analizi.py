import sys; sys.path.insert(0,'/home/claude/stm32f7-edge-ai-climate-chamber/analysis')
from plot_run import load
import numpy as np, pandas as pd
from scipy.optimize import curve_fit
df,_=load(['P2_20261124_0938.txt','P2_20261124_0938_2.txt'])
print('DHT NaN', df.T_air_dht.isna().sum(), 'RH NaN', df.RH_dht.isna().sum(), 'yuzey NaN', df.T_heatsink.isna().sum(), 'ortam NaN', df.T_ambient.isna().sum())
for c in ['T_air_dht','RH_dht','T_heatsink','T_wall','T_ambient']: df[c]=df[c].interpolate(limit_direction='both')
df['t']=(df.ts-df.ts.iloc[0]).dt.total_seconds()
# dew point
a,b=17.62,243.12
g=np.log(df.RH_dht/100)+a*df.T_air_dht/(b+df.T_air_dht); df['dew']=b*g/(a-g)
rows=[]
for ph in range(1,24,2):
    s=df[df.phase==ph]; pre=df[df.phase==ph-1]
    y0=pre.T_air_dht.iloc[-120:].mean(); amb=s.T_ambient.mean()
    t=s.t.values-s.t.values[0]; y=s.T_air_dht.values-y0
    slope_end=np.polyfit(t[-900:]/60,y[-900:],1)[0]  # K/min last 15 min
    # 1st order + dead time
    f1=lambda t,K,T,L: K*(1-np.exp(-np.clip(t-L,0,None)/T))
    p1,_=curve_fit(f1,t,y,p0=[y[-1],1000,60],maxfev=20000)
    r1=np.sqrt(np.mean((f1(t,*p1)-y)**2))
    f2=lambda t,K1,T1,K2,T2,L: K1*(1-np.exp(-np.clip(t-L,0,None)/T1))+K2*(1-np.exp(-np.clip(t-L,0,None)/T2))
    try:
        p2,_=curve_fit(f2,t,y,p0=[y[-1]*.7,700,y[-1]*.5,8000,40],bounds=([-60,50,-60,1500,0],[60,3000,60,80000,600]),maxfev=40000)
        r2=np.sqrt(np.mean((f2(t,*p2)-y)**2))
    except Exception as e: p2=[np.nan]*5; r2=np.nan
    # t63 empirical
    yf=y[-1]; i63=np.argmax(np.abs(y)>=0.632*abs(yf)); 
    # delay: first time |dy| > 0.3K
    idel=np.argmax(np.abs(y)>0.3)
    u=s.u_cmd.median()/10
    surf_air=(s.T_heatsink-s.T_air_dht).iloc[-300:].mean()
    rows.append(dict(ph=ph,u=u,y0=round(y0,2),dT90=round(y[-300:].mean(),2),gain=round(y[-300:].mean()/u,3),
      slope_end_mK_min=round(slope_end*1000,1),t63_min=round(t[i63]/60,1),gecikme_s=int(t[idel]),
      K1=round(p1[0],2),T1_min=round(p1[1]/60,1),L1_s=int(p1[2]),rmse1=round(r1,3),
      Kf=round(p2[0],2),Tf_min=round(p2[1]/60,1),Ks=round(p2[2],2),Ts_min=round(p2[3]/60,0),L2=int(p2[4]),rmse2=round(r2,3),
      yuzey_hava=round(surf_air,2),amb_deg=round(s.T_ambient.iloc[-300:].mean()-pre.T_ambient.iloc[-120:].mean(),2),
      RHmax=s.RH_dht.max(),dew_end=round(s.dew.iloc[-300:].mean(),1),min_surf=s.T_heatsink.min(),wall_air=round((s.T_wall-s.T_air_dht).iloc[-300:].mean(),2)))
r=pd.DataFrame(rows); pd.set_option('display.width',250); print(r.to_string())
# rest phases: recovery
print()
for ph in range(0,24,2):
    s=df[df.phase==ph]; t=s.t.values-s.t.values[0]
    print(ph, 'hava-ortam son5dk %.2f'%((s.T_air_dht-s.T_ambient).iloc[-300:].mean()), 'egim mK/dk %.1f'%(np.polyfit(t[-900:]/60,s.T_air_dht.values[-900:],1)[0]*1000), 'RHmax %.1f'%s.RH_dht.max())
r.to_csv('p2_ozet.csv',index=False)
