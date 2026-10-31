class Biquad{constructor(){this.b0=1;this.b1=0;this.b2=0;this.a1=0;this.a2=0;this.z1=0;this.z2=0;}
  set(type,f,Q,fs){const w=2*Math.PI*Math.max(10,Math.min(fs*0.45,f))/fs,c=Math.cos(w),sn=Math.sin(w),al=sn/(2*Math.max(0.1,Q));let b0,b1,b2,a0,a1,a2;
    if(type==='hpf'){b0=(1+c)/2;b1=-(1+c);b2=(1+c)/2;a0=1+al;a1=-2*c;a2=1-al;}
    else if(type==='lpf'){b0=(1-c)/2;b1=1-c;b2=(1-c)/2;a0=1+al;a1=-2*c;a2=1-al;}
    else if(type==='bell'){const A=Math.pow(10,6/40);b0=1+al*A;b1=-2*c;b2=1-al*A;a0=1+al/A;a1=-2*c;a2=1-al/A;}
    else{this.b0=1;this.b1=this.b2=this.a1=this.a2=0;return;}
    this.b0=b0/a0;this.b1=b1/a0;this.b2=b2/a0;this.a1=a1/a0;this.a2=a2/a0;}
  run(x){const y=this.b0*x+this.z1;this.z1=this.b1*x-this.a1*y+this.z2;this.z2=this.b2*x-this.a2*y;return y;}}
class CompProc extends AudioWorkletProcessor{
  constructor(){super();const fs=sampleRate;this.fs=fs;
    this.p={thr:0,ratio:1,knee:0,atk:0.0001,rel:0.01,mk:0,det:'peak',look:0,scType:'off',scF:1000,scQ:0.7,mode:'comp',ceil:-0.1};
    this.g=0;this.env=0;this.rmsAcc=0;this.rmsCoef=Math.exp(-1/(0.003*fs));
    this.maxLook=Math.ceil(0.02*fs);this.bufL=new Float32Array(this.maxLook+128);this.bufR=new Float32Array(this.maxLook+128);this.w=0;
    this.sc=[new Biquad(),new Biquad()];this.grMax=0;this.cnt=0;
    this.port.onmessage=e=>{Object.assign(this.p,e.data);this.sc.forEach(b=>b.set(this.p.scType,this.p.scF,this.p.scQ,fs));};}
  process(inputs,outputs){const inp=inputs[0],out=outputs[0];if(!inp||!inp.length)return true;
    const L=inp[0],R=inp[1]||inp[0],oL=out[0],oR=out[1]||out[0];const n=L.length,p=this.p,fs=this.fs;
    const look=Math.min(this.maxLook,Math.round(p.look*fs));
    const aA=Math.exp(-1/(Math.max(0.00002,p.atk)*fs)),aR=Math.exp(-1/(Math.max(0.001,p.rel)*fs));
    const lim=p.mode==='limiter';const thr=lim?p.ceil:p.thr,ratio=lim?100:p.ratio,knee=lim?0:p.knee;
    const mk=Math.pow(10,(lim?0:p.mk)/20);
    for(let i=0;i<n;i++){
      // détecteur (side-chain filtré)
      let dl=L[i],dr=R[i];if(p.scType!=='off'){dl=this.sc[0].run(dl);dr=this.sc[1].run(dr);}
      let lvl;
      if(lim){const a=Math.max(Math.abs(L[i]),Math.abs(R[i]));// true-peak approx. : interpolation x4 avec échantillon précédent
        const pl=this.pL||0,pr=this.pR||0;let tp=a;for(let k=1;k<4;k++){const t=k/4;tp=Math.max(tp,Math.abs(pl+(L[i]-pl)*t),Math.abs(pr+(R[i]-pr)*t));}this.pL=L[i];this.pR=R[i];lvl=tp;}
      else if(p.det==='rms'){const sq=(dl*dl+dr*dr)/2;this.rmsAcc=this.rmsCoef*this.rmsAcc+(1-this.rmsCoef)*sq;lvl=Math.sqrt(this.rmsAcc);}
      else lvl=Math.max(Math.abs(dl),Math.abs(dr));
      const db=lvl>1e-6?20*Math.log10(lvl):-120;
      // calculateur de gain (knee doux)
      const ex=db-thr,hk=knee/2;let gr;
      if(ex<=-hk)gr=0;else if(ex<hk&&knee>0){const t=ex+hk;gr=(1/ratio-1)*t*t/(2*knee);}else gr=(1/ratio-1)*ex;
      gr=-gr; // réduction positive en dB
      // lissage attaque / release en dB
      this.g=gr>this.g?aA*this.g+(1-aA)*gr:aR*this.g+(1-aR)*gr;
      if(this.g>this.grMax)this.grMax=this.g;
      const lin=Math.pow(10,-this.g/20)*mk;
      // lookahead : on applique le gain au signal retardé
      const wi=this.w;this.bufL[wi]=L[i];this.bufR[wi]=R[i];
      const ri=(wi-look+this.bufL.length)%this.bufL.length;
      let yl=this.bufL[ri]*lin,yr=this.bufR[ri]*lin;
      if(lim){const c=Math.pow(10,p.ceil/20);yl=Math.max(-c,Math.min(c,yl));yr=Math.max(-c,Math.min(c,yr));}
      if(p.scListen){yl=dl;yr=dr;} // écoute du side-chain (signal du détecteur, filtré)
      oL[i]=yl;oR[i]=yr;this.w=(wi+1)%this.bufL.length;
    }
    this.cnt+=n;if(this.cnt>=1024){this.port.postMessage({gr:this.g,grMax:this.grMax});this.grMax=0;this.cnt=0;}
    return true;}}
registerProcessor('al-comp',CompProc);
