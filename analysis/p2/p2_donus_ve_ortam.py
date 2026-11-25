exec(open("p2_basamak_analizi.py").read().split("rows=[]")[0])
f1=lambda t,K,T,c: c+K*np.exp(-t/T)
print("Dinlenme (u=0) donusu: hava-ortam farki ustel")
for ph in range(2,24,2):
    s=df[df.phase==ph]; t=s.t.values-s.t.values[0]; y=(s.T_air_dht-s.T_ambient).rolling(60,min_periods=1).mean().values
    try:
        p,_=curve_fit(f1,t,y,p0=[y[0]-2,1500,2],bounds=([-60,100,-5],[60,30000,8]),maxfev=20000)
        print(ph,'onceki u',df[df.phase==ph-1].u_cmd.median()/10,'T=%.1f dk'%(p[1]/60),'son deger(asimptot) %.2f'%p[2],'bas %.1f'%y[0])
    except Exception as e: print(ph,e)
# ambient during cooling
for ph in [10,11,12,22,23]:
    s=df[df.phase==ph]
    print(ph, s.u_cmd.median(), 'amb bas %.2f orta %.2f son %.2f'%(s.T_ambient.iloc[:120].mean(),s.T_ambient.iloc[len(s)//2-60:len(s)//2+60].mean(),s.T_ambient.iloc[-120:].mean()))
s=df[(df.phase>=11)&(df.phase<=12)]
print(s.set_index('ts').T_ambient.resample('10min').mean().round(2).to_string())
