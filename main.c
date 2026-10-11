// STANDSTILL — a boomer-shooter where time only moves when you do.
// Framebuffer textured raycaster + fully procedural art & audio + a PS5-style
// shell. Portable C11 + SDL2 (no external assets), so it still cross-compiles to
// PS5 homebrew. Build: nix-shell && make run.
#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "audio.h"

#define WIN_W 1920
#define WIN_H 1080
#define FB_W 960          // internal render resolution (scaled to the window)
#define FB_H 540
#define MAP_W 16
#define MAP_H 16
#define TEX 64
#define NTEX 6            // wall textures, indexed by tile value (1..5)
#define N_ENEMIES 10
#define MAX_BULLETS 256
#define MAX_PARTS 700
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int worldMap[MAP_H][MAP_W]; // generated each game by genMap()

// ---------- 5x7 font: index 0=space, 1..10='0'..'9', 11..36='A'..'Z' ----------
static const unsigned char FONT[37][7] = {
 {0,0,0,0,0,0,0},
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
 {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
 {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
 {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

// ---------- globals ----------
static uint32_t fb[FB_W*FB_H];
static uint32_t wallTex[NTEX][TEX*TEX];
static uint32_t floorTex[TEX*TEX], ceilTex[TEX*TEX];
static uint32_t enemyTex[3][TEX*TEX];
static double zbuf[FB_W];

enum { ST_BOOT, ST_TITLE, ST_PLAY, ST_PAUSE, ST_WIN, ST_LOSE, ST_PS5HOME };
typedef struct { double x,y; int alive,type,hp; double cooldown,hurt; } Enemy;
typedef struct { double x,y,vx,vy; int alive,fromPlayer; } Bullet;
typedef struct { double x,y,vx,vy,life,maxlife; uint32_t col; } Part;
typedef struct { double d; int kind,idx; } Spr;

static Enemy enemies[N_ENEMIES];
static Bullet bullets[MAX_BULLETS];
static Part parts[MAX_PARTS];
static double px,py,dirX,dirY,planeX,planeY;
static int health, gstate, score, combo;
static double timeScale, muzzle, bob, shake, comboTimer, stateTime;
static double inFwd, inStrafe, inTurn; // current action amounts (for indicator)
static int menuSel = 0;                // (unused)
static int weapon = 0;                 // 0 = blaster, 1 = scatter
static double flSwitch, flMelee, flDash, dashT, dashCd; // button-flash + dash timers

// ---------- small helpers ----------
static inline uint32_t rgb(int r,int g,int b){
    if(r<0)r=0;if(r>255)r=255;if(g<0)g=0;if(g>255)g=255;if(b<0)b=0;if(b>255)b=255;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
static inline uint32_t argb(int a,int r,int g,int b){ return ((a&255)<<24)|((r&255)<<16)|((g&255)<<8)|(b&255); }
static inline uint32_t shadeC(uint32_t c,float f){
    int a=(c>>24)&255,r=(int)(((c>>16)&255)*f),g=(int)(((c>>8)&255)*f),b=(int)((c&255)*f);
    if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
    return (a<<24)|(r<<16)|(g<<8)|b;
}
static float frand01(uint32_t *s){ *s^=*s<<13;*s^=*s>>17;*s^=*s<<5; return (float)(*s&0xffffff)/16777215.0f; }
static float hash2(int x,int y){ uint32_t h=(uint32_t)(x*374761393+y*668265263); h=(h^(h>>13))*1274126177u; return (float)(h&0xffff)/65535.0f; }
static float vnoise(float x,float y){
    int xi=(int)floorf(x), yi=(int)floorf(y); float fx=x-xi, fy=y-yi;
    float a=hash2(xi,yi),b=hash2(xi+1,yi),c=hash2(xi,yi+1),d=hash2(xi+1,yi+1);
    float u=fx*fx*(3-2*fx), v=fy*fy*(3-2*fy);
    return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v;
}
static float fogf(double d){ float f=1.0f-(float)d*0.07f; return f<0.14f?0.14f:f; }

// ---------- procedural textures ----------
static void genTextures(void){
    for(int t=0;t<TEX*TEX;t++){ // base
        int x=t%TEX, y=t/TEX;
        // floor: dark tiles with grout + noise
        float n=vnoise(x*0.25f,y*0.25f);
        int fg=(x%32<2||y%32<2)?34:60+(int)(n*22);
        floorTex[t]=rgb(fg-6,fg-4,fg+4);
        // ceiling: smooth dark blue
        int cg=22+(int)(vnoise(x*0.12f,y*0.12f)*14);
        ceilTex[t]=rgb(cg,cg+2,cg+10);
    }
    for(int ty=0;ty<TEX;ty++)for(int tx=0;tx<TEX;tx++){
        int i=ty*TEX+tx; float n=vnoise(tx*0.5f,ty*0.5f);
        // 1 = brick (red)
        { int by=ty%16, bx=(tx+((ty/16)&1?8:0))%16; int mort=(by<2||bx<2);
          int base=150+(int)(n*30); int r=mort?70:base, g=mort?60:60+(int)(n*20), b=mort?58:52;
          wallTex[1][i]=rgb(r,g,b); }
        // 2 = blue tech panel with rivets
        { int seam=(tx%32<1||ty%32<1); int rv=((tx%32-16)*(tx%32-16)+(ty%32-16)*(ty%32-16)<9);
          int base=70+(int)(n*20); int r=seam?30:base-20, g=seam?70:120+(int)(n*20), b=seam?110:170;
          if(rv){r=210;g=225;b=235;} wallTex[2][i]=rgb(r,g,b); }
        // 3 = cracked stone (green-grey)
        { float c=vnoise(tx*0.3f+9,ty*0.3f); int crack=(fabsf(c-0.5f)<0.04f); int g=90+(int)(n*40);
          wallTex[3][i]=crack?rgb(30,34,30):rgb(g-30,g,g-26); }
        // 4 = hazard stripes (yellow/black)
        { int s=((tx+ty)/8)&1; wallTex[4][i]= s?rgb(210,180+(int)(n*20),40):rgb(26,24,20); }
        // 5 = circuit panel (dark + glowing traces)
        { int trace=((tx%12==0)||(ty%12==0)||((tx+ty)%24<1)); int base=30+(int)(n*14);
          wallTex[5][i]=trace?rgb(30,200,150):rgb(base,base+4,base+8); }
        wallTex[0][i]=rgb(120,120,120);
    }
    // enemy sprites (ARGB w/ alpha)
    for(int ty=0;ty<TEX;ty++)for(int tx=0;tx<TEX;tx++){
        int i=ty*TEX+tx; float nx=(tx-32)/30.0f, ny=(ty-30)/30.0f; float d=sqrtf(nx*nx+ny*ny);
        // type 0: hovering drone (dark body, red eye, antenna)
        uint32_t c0=0;
        if(d<0.72f){ float sh=0.55f+0.45f*(1.0f-ny); int base=(int)(70*sh);
            c0=argb(255,base+10,base+14,base+22);
            float ed=sqrtf(nx*nx+(ny+0.02f)*(ny+0.02f));
            if(ed<0.22f){ float g=1.0f-ed/0.22f; c0=argb(255,180+(int)(70*g),30,26); }
            if(d>0.64f) c0=argb(255,20,22,30); }
        if(tx>=31&&tx<=32&&ty<=10&&ty>=2) c0=argb(255,30,32,40); // antenna
        if((tx-32)*(tx-32)+(ty-3)*(ty-3)<4) c0=argb(255,230,60,40);
        enemyTex[0][i]=c0;
        // type 1: brute (wider red body, two eyes)
        uint32_t c1=0; float ex=(tx-32)/26.0f, ey=(ty-34)/28.0f; float e=ex*ex+ey*ey;
        if(e<1.0f){ float sh=0.5f+0.5f*(1.0f-ey); int r=(int)(150*sh)+40;
            c1=argb(255,r,(int)(30*sh),(int)(26*sh));
            if(e>0.84f) c1=argb(255,40,10,10);
            float le=(tx-24)*(tx-24)+(ty-26)*(ty-26), re=(tx-40)*(tx-40)+(ty-26)*(ty-26);
            if(le<10||re<10) c1=argb(255,255,210,60); }
        enemyTex[1][i]=c1;
        // type 2: spitter — orange diamond with a bright core
        uint32_t c2=0; float dd=fabsf(nx)+fabsf(ny);
        if(dd<0.82f){ float sh=0.6f+0.4f*(1.0f-ny); int base=(int)(110*sh);
            c2=argb(255,base+100,base+30,20);
            if(dd>0.70f) c2=argb(255,50,18,8);
            float cc=sqrtf(nx*nx+ny*ny); if(cc<0.26f){ float g=1.0f-cc/0.26f; c2=argb(255,255,180+(int)(60*g),60); } }
        enemyTex[2][i]=c2;
    }
}

// ---------- game helpers ----------
static int isWall(double x,double y){ int mx=(int)x,my=(int)y; if(mx<0||mx>=MAP_W||my<0||my>=MAP_H)return 1; return worldMap[my][mx]>0; }
static void spawnParts(double x,double y,uint32_t col,int n,double spd){
    uint32_t s=(uint32_t)(x*131+y*977)+1;
    for(int k=0;k<n;k++){ for(int i=0;i<MAX_PARTS;i++) if(parts[i].life<=0){
        float a=frand01(&s)*6.2832f, v=spd*(0.3f+frand01(&s));
        parts[i]=(Part){x,y,cos(a)*v,sin(a)*v,0.5+frand01(&s)*0.4,0.9,col}; break; } }
}
static int roomX[8], roomY[8], nRooms=0;
static uint32_t mapRng=1;
static void genMap(void){                 // procedural rooms + corridors each game
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++) worldMap[y][x]=1;
    nRooms=0; int target=4+(int)(frand01(&mapRng)*3);
    for(int k=0;k<target && nRooms<8;k++){
        int rw=3+(int)(frand01(&mapRng)*3), rh=3+(int)(frand01(&mapRng)*3);
        int rx=1+(int)(frand01(&mapRng)*(MAP_W-2-rw)), ry=1+(int)(frand01(&mapRng)*(MAP_H-2-rh));
        for(int yy=ry;yy<ry+rh;yy++)for(int xx=rx;xx<rx+rw;xx++) worldMap[yy][xx]=0;
        roomX[nRooms]=rx+rw/2; roomY[nRooms]=ry+rh/2; nRooms++;
    }
    for(int k=1;k<nRooms;k++){ int x0=roomX[k-1],y0=roomY[k-1],x1=roomX[k],y1=roomY[k];
        int xa=x0<x1?x0:x1,xb=x0<x1?x1:x0; for(int x=xa;x<=xb;x++) worldMap[y0][x]=0;
        int ya=y0<y1?y0:y1,yb=y0<y1?y1:y0; for(int y=ya;y<=yb;y++) worldMap[y][x1]=0; }
    for(int x=0;x<MAP_W;x++){ worldMap[0][x]=1; worldMap[MAP_H-1][x]=1; }
    for(int y=0;y<MAP_H;y++){ worldMap[y][0]=1; worldMap[y][MAP_W-1]=1; }
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++) if(worldMap[y][x]>0){ int t=1+(int)(hash2(x/3,y/3)*4.99f); worldMap[y][x]=t<1?1:(t>5?5:t); }
    if(nRooms==0){ roomX[0]=MAP_W/2; roomY[0]=MAP_H/2; nRooms=1; worldMap[MAP_H/2][MAP_W/2]=0; }
}
static void resetGame(void){
    genMap();
    px=roomX[0]+0.5; py=roomY[0]+0.5; dirX=1;dirY=0;planeX=0;planeY=0.66; health=100;score=0;combo=0;comboTimer=0;
    weapon=0; dashT=dashCd=flMelee=flSwitch=flDash=0;
    for(int i=0;i<N_ENEMIES;i++){
        int t=i%3; int hp=(t==1)?3:1;
        int rm = nRooms>1 ? 1+(i%(nRooms-1)) : 0;
        double ex=roomX[rm]+0.5+(frand01(&mapRng)*2-1), ey=roomY[rm]+0.5+(frand01(&mapRng)*2-1);
        if(isWall(ex,ey)){ ex=roomX[rm]+0.5; ey=roomY[rm]+0.5; }
        enemies[i]=(Enemy){ex,ey,1,t,hp,1.0+i*0.3,0};
    }
    for(int i=0;i<MAX_BULLETS;i++)bullets[i].alive=0;
    for(int i=0;i<MAX_PARTS;i++)parts[i].life=0;
}
static void spawnBullet(double x,double y,double vx,double vy,int fp){
    for(int i=0;i<MAX_BULLETS;i++) if(!bullets[i].alive){ bullets[i]=(Bullet){x,y,vx,vy,1,fp}; return; }
}
static void hitEnemy(int i){
    enemies[i].hp--; enemies[i].hurt=0.12;
    if(enemies[i].hp<=0){ enemies[i].alive=0; audio_death();
        spawnParts(enemies[i].x,enemies[i].y,rgb(230,80,50),16,2.5);
        combo++; comboTimer=2.2; score+=100*combo; shake=fmax(shake,0.6);
    } else audio_hit();
}
static void playerShoot(void){           // X / R2
    muzzle=0.09; shake=fmax(shake,0.35); audio_shoot();
    if(weapon==0){ int best=-1; double bestAlign=0.986;   // blaster: precise single
        for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){
            double dx=enemies[i].x-px,dy=enemies[i].y-py,l=sqrt(dx*dx+dy*dy); if(l<0.01)continue;
            double a=(dx*dirX+dy*dirY)/l; if(a>bestAlign){bestAlign=a;best=i;} }
        if(best>=0) hitEnemy(best);
    } else {                                              // scatter: wide, short range
        for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){
            double dx=enemies[i].x-px,dy=enemies[i].y-py,l=sqrt(dx*dx+dy*dy); if(l<0.01||l>4.5)continue;
            if((dx*dirX+dy*dirY)/l>0.86) hitEnemy(i); }
    }
}
static void playerMelee(void){           // Square: close slash
    flMelee=0.16; shake=fmax(shake,0.4); audio_shoot();
    for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){
        double dx=enemies[i].x-px,dy=enemies[i].y-py,l=sqrt(dx*dx+dy*dy); if(l<0.01||l>1.6)continue;
        if((dx*dirX+dy*dirY)/l>0.3) hitEnemy(i); }
}
static void weaponSwitch(void){ weapon=1-weapon; flSwitch=0.18; audio_hit(); }  // Triangle
static void playerDash(void){ if(dashT>0||dashCd>0)return; dashT=0.2; dashCd=0.7; flDash=0.22; shake=fmax(shake,0.3); audio_whoosh(); }  // Circle
static int cmpSpr(const void*a,const void*b){ double d=((const Spr*)b)->d-((const Spr*)a)->d; return d>0?1:(d<0?-1:0); }

// ---------- world render into fb ----------
static void renderWorld(void){
    // floor + ceiling cast
    double rdx0=dirX-planeX, rdy0=dirY-planeY, rdx1=dirX+planeX, rdy1=dirY+planeY;
    for(int y=FB_H/2+1;y<FB_H;y++){
        int p=y-FB_H/2; double rowDist=(0.5*FB_H)/p;
        double stepX=rowDist*(rdx1-rdx0)/FB_W, stepY=rowDist*(rdy1-rdy0)/FB_W;
        double fx=px+rowDist*rdx0, fy=py+rowDist*rdy0; float fog=fogf(rowDist);
        for(int x=0;x<FB_W;x++){
            int tx=((int)(TEX*(fx-floor(fx))))&(TEX-1), ty=((int)(TEX*(fy-floor(fy))))&(TEX-1);
            fb[y*FB_W+x]=shadeC(floorTex[TEX*ty+tx],fog);
            fb[(FB_H-y-1)*FB_W+x]=shadeC(ceilTex[TEX*ty+tx],fog*0.85f);
            fx+=stepX; fy+=stepY;
        }
    }
    // walls
    for(int x=0;x<FB_W;x++){
        double camX=2.0*x/FB_W-1.0, rdx=dirX+planeX*camX, rdy=dirY+planeY*camX;
        int mx=(int)px,my=(int)py;
        double ddx=rdx==0?1e30:fabs(1/rdx), ddy=rdy==0?1e30:fabs(1/rdy);
        double sdx,sdy; int stepx,stepy,side=0,tile=1;
        if(rdx<0){stepx=-1;sdx=(px-mx)*ddx;}else{stepx=1;sdx=(mx+1.0-px)*ddx;}
        if(rdy<0){stepy=-1;sdy=(py-my)*ddy;}else{stepy=1;sdy=(my+1.0-py)*ddy;}
        int hit=0;
        while(!hit){ if(sdx<sdy){sdx+=ddx;mx+=stepx;side=0;}else{sdy+=ddy;my+=stepy;side=1;}
            if(mx<0||mx>=MAP_W||my<0||my>=MAP_H){hit=1;break;} if(worldMap[my][mx]>0){hit=1;tile=worldMap[my][mx];} }
        double perp=side==0?(sdx-ddx):(sdy-ddy); if(perp<0.0001)perp=0.0001; zbuf[x]=perp;
        int lineH=(int)(FB_H/perp); int y0=-lineH/2+FB_H/2, y1=lineH/2+FB_H/2;
        int dy0=y0<0?0:y0, dy1=y1>=FB_H?FB_H-1:y1;
        double wallX=side==0?py+perp*rdy:px+perp*rdx; wallX-=floor(wallX);
        int texX=(int)(wallX*TEX); if((side==0&&rdx>0)||(side==1&&rdy<0))texX=TEX-texX-1;
        if(tile<0||tile>=NTEX)tile=0;
        float sh=(side==1?0.72f:1.0f)*fogf(perp);
        double step=(double)TEX/lineH, texPos=(dy0-FB_H/2+lineH/2.0)*step;
        for(int y=dy0;y<=dy1;y++){ int texY=((int)texPos)&(TEX-1); texPos+=step;
            fb[y*FB_W+x]=shadeC(wallTex[tile][TEX*texY+texX],sh); }
    }
    // sprites: enemies + bullets + particles
    Spr sp[N_ENEMIES+MAX_BULLETS+MAX_PARTS]; int ns=0;
    for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){ double dx=enemies[i].x-px,dy=enemies[i].y-py; sp[ns++]=(Spr){dx*dx+dy*dy,0,i}; }
    for(int i=0;i<MAX_BULLETS;i++) if(bullets[i].alive){ double dx=bullets[i].x-px,dy=bullets[i].y-py; sp[ns++]=(Spr){dx*dx+dy*dy,1,i}; }
    for(int i=0;i<MAX_PARTS;i++) if(parts[i].life>0){ double dx=parts[i].x-px,dy=parts[i].y-py; sp[ns++]=(Spr){dx*dx+dy*dy,2,i}; }
    qsort(sp,ns,sizeof(Spr),cmpSpr);
    double invDet=1.0/(planeX*dirY-dirX*planeY);
    for(int s=0;s<ns;s++){
        double wx,wy,scale; int kind=sp[s].kind;
        if(kind==0){ wx=enemies[sp[s].idx].x; wy=enemies[sp[s].idx].y; scale=0.95; }
        else if(kind==1){ wx=bullets[sp[s].idx].x; wy=bullets[sp[s].idx].y; scale=0.16; }
        else { wx=parts[sp[s].idx].x; wy=parts[sp[s].idx].y; scale=0.09; }
        double spx=wx-px, spy=wy-py;
        double tX=invDet*(dirY*spx-dirX*spy), tY=invDet*(-planeY*spx+planeX*spy);
        if(tY<=0.08)continue;
        int scrX=(int)((FB_W/2)*(1+tX/tY)); int h=(int)(FB_H/tY*scale); if(h<1)h=1;
        int y0=-h/2+FB_H/2, y1=h/2+FB_H/2, x0=-h/2+scrX, x1=h/2+scrX;
        float fog=fogf(tY);
        if(kind==0){ int et=enemies[sp[s].idx].type; float hurt=enemies[sp[s].idx].hurt>0?2.2f:1.0f;
            for(int x=x0;x<=x1;x++){ if(x<0||x>=FB_W||tY>=zbuf[x])continue; int txs=(x-x0)*TEX/h; if(txs<0||txs>=TEX)continue;
                for(int y=y0;y<=y1;y++){ if(y<0||y>=FB_H)continue; int tys=(y-y0)*TEX/h; if(tys<0||tys>=TEX)continue;
                    uint32_t c=enemyTex[et][TEX*tys+txs]; if((c>>24)<128)continue; fb[y*FB_W+x]=shadeC(c,fog*hurt); } } }
        else { uint32_t col = kind==1 ? (bullets[sp[s].idx].fromPlayer?rgb(120,220,255):rgb(255,210,70))
                                      : parts[sp[s].idx].col;
            double cx=(x0+x1)/2.0, cy=(y0+y1)/2.0, rr=(h/2.0)*(h/2.0);
            float life = kind==2 ? (float)(parts[sp[s].idx].life/parts[sp[s].idx].maxlife) : 1.0f;
            for(int x=x0;x<=x1;x++){ if(x<0||x>=FB_W||tY>=zbuf[x])continue;
                for(int y=y0;y<=y1;y++){ if(y<0||y>=FB_H)continue; if((x-cx)*(x-cx)+(y-cy)*(y-cy)>rr)continue;
                    fb[y*FB_W+x]=shadeC(col,fog*(0.4f+0.6f*life)); } } }
    }
    // muzzle flash lights the view centre
    if(muzzle>0){ float m=(float)(muzzle/0.09); int cx=FB_W/2,cy=FB_H/2;
        for(int y=cy-70;y<cy+40;y++){ if(y<0||y>=FB_H)continue; for(int x=cx-70;x<cx+70;x++){ if(x<0||x>=FB_W)continue;
            float d2=(float)((x-cx)*(x-cx)+(y-cy)*(y-cy)); float g=(1.0f-d2/5000.0f)*m*0.5f; if(g<=0)continue;
            uint32_t c=fb[y*FB_W+x]; fb[y*FB_W+x]=rgb(((c>>16)&255)+(int)(g*180),((c>>8)&255)+(int)(g*160),(c&255)+(int)(g*90)); } } }
    // post: vignette + time-freeze grade
    float fz=1.0f-(float)timeScale; if(fz<0)fz=0;
    for(int y=0;y<FB_H;y++){ float ny=(y-FB_H*0.5f)/(FB_H*0.5f);
        for(int x=0;x<FB_W;x++){ float nx=(x-FB_W*0.5f)/(FB_W*0.5f); float vig=1.0f-0.5f*(nx*nx+ny*ny);
            uint32_t c=fb[y*FB_W+x]; int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(fz>0.01f){ int lum=(r*3+g*6+b)/10; r+=(int)((lum*0.55f-r)*fz); g+=(int)((lum*0.7f-g)*fz); b+=(int)((lum*1.2f+18-b)*fz); }
            fb[y*FB_W+x]=rgb((int)(r*vig),(int)(g*vig),(int)(b*vig)); }
    }
}

// ---------- overlay (SDL primitives, 1920x1080) ----------
static void sc(SDL_Renderer*r,int R,int G,int B,int A){ SDL_SetRenderDrawColor(r,R,G,B,A); }
static void fillCircle(SDL_Renderer*r,int cx,int cy,int rad){
    for(int dy=-rad;dy<=rad;dy++){ int dx=(int)sqrt((double)rad*rad-dy*dy); SDL_RenderDrawLine(r,cx-dx,cy+dy,cx+dx,cy+dy); }
}
static void ring(SDL_Renderer*r,int cx,int cy,int rad){
    for(int a=0;a<360;a+=6){ double t=a*M_PI/180,t2=(a+6)*M_PI/180;
        SDL_RenderDrawLine(r,cx+(int)(cos(t)*rad),cy+(int)(sin(t)*rad),cx+(int)(cos(t2)*rad),cy+(int)(sin(t2)*rad)); }
}
static int gi(char c){ if(c==' ')return 0; if(c>='0'&&c<='9')return 1+c-'0'; if(c>='a'&&c<='z')c-=32; if(c>='A'&&c<='Z')return 11+c-'A'; return -1; }
static int textW(const char*s,int sc_){ return (int)strlen(s)*6*sc_; }
static void text(SDL_Renderer*r,int x,int y,int s,const char*str,int R,int G,int B){
    sc(r,R,G,B,255);
    for(const char*p=str;*p;p++){ int g=gi(*p); if(g>=0){ for(int ry=0;ry<7;ry++){ unsigned char row=FONT[g][ry];
        for(int rx=0;rx<5;rx++) if(row&(1<<(4-rx))){ SDL_Rect q={x+rx*s,y+ry*s,s,s}; SDL_RenderFillRect(r,&q); } } } x+=6*s; }
}
static void textC(SDL_Renderer*r,int cx,int y,int s,const char*str,int R,int G,int B){ text(r,cx-textW(str,s)/2,y,s,str,R,G,B); }

// a PlayStation ✕ button glyph (bg circle + X), lit = highlighted
static void glyphCross(SDL_Renderer*r,int cx,int cy,int rad,int lit){
    if(lit)sc(r,70,150,230,255);else sc(r,30,40,60,255); fillCircle(r,cx,cy,rad);
    sc(r,150,200,255,255); ring(r,cx,cy,rad);
    int d=(int)(rad*0.5); sc(r, lit?235:150, lit?245:190, 255,255);
    SDL_RenderDrawLine(r,cx-d,cy-d,cx+d,cy+d); SDL_RenderDrawLine(r,cx-d,cy-d+1,cx+d,cy+d+1);
    SDL_RenderDrawLine(r,cx-d,cy+d,cx+d,cy-d); SDL_RenderDrawLine(r,cx-d,cy+d-1,cx+d,cy-d-1);
}

// bottom-left mini DualSense showing live input
static void drawController(SDL_Renderer*r){
    int ox=210, oy=WIN_H-165; // centre
    sc(r,18,20,28,220); SDL_Rect body={ox-120,oy-55,240,110}; SDL_RenderFillRect(r,&body);
    sc(r,40,44,56,255); fillCircle(r,ox-120,oy,55); fillCircle(r,ox+120,oy,55);
    sc(r,18,20,28,255); fillCircle(r,ox-120,oy,55-14); fillCircle(r,ox+120,oy,55-14);
    sc(r,30,34,44,255); SDL_Rect tp={ox-26,oy-34,52,40}; SDL_RenderFillRect(r,&tp);
    // sticks
    int lsx=ox-62+(int)(inStrafe*14), lsy=oy+14+(int)(-inFwd*14);
    int rsx=ox+62+(int)(inTurn*14), rsy=oy+14;
    sc(r,24,26,34,255); fillCircle(r,ox-62,oy+14,20); fillCircle(r,ox+62,oy+14,20);
    int lMove=(fabs(inFwd)+fabs(inStrafe))>0.1, rMove=fabs(inTurn)>0.1;
    sc(r,lMove?80:50,lMove?170:54,lMove?240:66,255); fillCircle(r,lsx,lsy,13);
    sc(r,rMove?80:50,rMove?170:54,rMove?240:66,255); fillCircle(r,rsx,rsy,13);
    // face buttons (right cluster), cross lit when firing
    int fx=ox+62, fy=oy-26; int fire=muzzle>0;
    glyphCross(r,fx,fy+12,9,fire);
    sc(r, flDash>0?90:60, flDash>0?170:30, flDash>0?240:40,255); fillCircle(r,fx+14,fy,7);
    sc(r, flMelee>0?220:40, flMelee>0?110:30, flMelee>0?230:55,255); fillCircle(r,fx-14,fy,7);
    sc(r, flSwitch>0?70:30, flSwitch>0?210:55, flSwitch>0?120:40,255); fillCircle(r,fx,fy-12,7);
    // R2 trigger (top-right), lit on fire
    sc(r,fire?80:40,fire?170:44,fire?240:56,255); SDL_Rect r2={ox+94,oy-70,34,16}; SDL_RenderFillRect(r,&r2);
    text(r,ox+96,oy-67,2,"R2",200,220,255);
    text(r,ox-120,oy+60,2,"DUALSENSE",90,100,120);
}

static void drawMinimap(SDL_Renderer*r){
    int cell=11, ox=WIN_W-20-MAP_W*cell, oy=WIN_H-20-MAP_H*cell;
    sc(r,10,12,18,210); SDL_Rect bg={ox-5,oy-5,MAP_W*cell+10,MAP_H*cell+10}; SDL_RenderFillRect(r,&bg);
    int col[6][3]={{90,90,90},{150,70,70},{70,120,160},{80,150,90},{175,150,60},{60,160,130}};
    for(int y=0;y<MAP_H;y++)for(int x=0;x<MAP_W;x++){ int w=worldMap[y][x];
        if(w)sc(r,col[w][0],col[w][1],col[w][2],255); else sc(r,26,28,36,255);
        SDL_Rect c={ox+x*cell,oy+y*cell,cell-1,cell-1}; SDL_RenderFillRect(r,&c); }
    for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){ sc(r,235,70,60,255); SDL_Rect e={ox+(int)(enemies[i].x*cell)-2,oy+(int)(enemies[i].y*cell)-2,5,5}; SDL_RenderFillRect(r,&e); }
    int pxp=ox+(int)(px*cell), pyp=oy+(int)(py*cell);
    sc(r,120,220,255,255); fillCircle(r,pxp,pyp,4);
    sc(r,200,240,255,255); SDL_RenderDrawLine(r,pxp,pyp,pxp+(int)(dirX*12),pyp+(int)(dirY*12));
}

static void drawHUD(SDL_Renderer*r){
    // time bar (the hook)
    sc(r,16,34,44,255); SDL_Rect tb={40,36,360,22}; SDL_RenderFillRect(r,&tb);
    int frozen=timeScale<0.4; sc(r, frozen?90:70, frozen?200:200, 240,255);
    SDL_Rect tf={40,36,(int)(360*timeScale),22}; SDL_RenderFillRect(r,&tf);
    text(r,46,62,2, frozen?"TIME  FROZEN":"TIME  FLOWING", 120,160,190);
    // health
    sc(r,60,18,20,255); SDL_Rect hb={40,WIN_H-60,360,24}; SDL_RenderFillRect(r,&hb);
    sc(r,220,55,55,255); SDL_Rect hf={40,WIN_H-60,(int)(360*(health/100.0)),24}; SDL_RenderFillRect(r,&hf);
    text(r,46,WIN_H-86,2,"HEALTH",200,120,120);
    // score + combo (top-right)
    char buf[64]; snprintf(buf,sizeof buf,"SCORE %d",score); text(r,WIN_W-textW(buf,4)-40,36,4,buf,235,235,245);
    if(combo>1){ snprintf(buf,sizeof buf,"X%d COMBO",combo); text(r,WIN_W-textW(buf,3)-40,90,3,buf,255,200,70); }
    // enemy pips
    int alive=0; for(int i=0;i<N_ENEMIES;i++)alive+=enemies[i].alive;
    for(int i=0;i<alive;i++){ sc(r,220,60,60,255); SDL_Rect p={WIN_W-40-i*26,140,16,16}; SDL_RenderFillRect(r,&p); }
    // weapon + muzzle flash
    int gx=WIN_W/2, gy=WIN_H+(int)(bob*18);
    sc(r,38,40,48,255); SDL_Rect barrel={gx-18,gy-240,36,170}; SDL_RenderFillRect(r,&barrel);
    sc(r,66,68,80,255); SDL_Rect gb={gx-70,gy-110,140,120}; SDL_RenderFillRect(r,&gb);
    sc(r,20,22,28,255); SDL_Rect sl={gx-10,gy-250,20,30}; SDL_RenderFillRect(r,&sl);
    if(muzzle>0){ int fr=(int)(muzzle/0.09*60)+16; sc(r,255,225,110,255); fillCircle(r,gx,gy-245,fr);
        sc(r,255,255,220,255); fillCircle(r,gx,gy-245,fr/2); }
    // crosshair
    int tgt=0; for(int i=0;i<N_ENEMIES;i++) if(enemies[i].alive){ double dx=enemies[i].x-px,dy=enemies[i].y-py,l=sqrt(dx*dx+dy*dy); if(l>0.01&&(dx*dirX+dy*dirY)/l>0.986)tgt=1; }
    sc(r,tgt?255:220,tgt?60:220,tgt?60:220,255);
    SDL_RenderDrawLine(r,WIN_W/2-12,WIN_H/2,WIN_W/2-4,WIN_H/2); SDL_RenderDrawLine(r,WIN_W/2+4,WIN_H/2,WIN_W/2+12,WIN_H/2);
    SDL_RenderDrawLine(r,WIN_W/2,WIN_H/2-12,WIN_W/2,WIN_H/2-4); SDL_RenderDrawLine(r,WIN_W/2,WIN_H/2+4,WIN_W/2,WIN_H/2+12);
    textC(r,WIN_W/2,40,3, weapon?"SCATTER":"BLASTER", 190,205,230);
    drawController(r);
    drawMinimap(r);
}

static void psBackground(SDL_Renderer*r,double t){
    for(int y=0;y<WIN_H;y+=4){ int v=14+y*24/WIN_H; sc(r,v,v+2,v+8,255); SDL_RenderDrawLine(r,0,y,WIN_W,y); SDL_RenderDrawLine(r,0,y+1,WIN_W,y+1);}
    uint32_t s=1; for(int i=0;i<80;i++){ float fx=frand01(&s)*WIN_W, fy=frand01(&s)*WIN_H; int yy=(int)(fy+t*20*(0.3+frand01(&s)))%WIN_H;
        int b=60+(int)(frand01(&s)*120); sc(r,b/2,b/2,b,255); SDL_Rect d={(int)fx,yy,2,2}; SDL_RenderFillRect(r,&d); }
}

// a simulated PS5 home screen: STANDSTILL as a selectable game tile
static void drawPS5Home(SDL_Renderer*r,double t){
    for(int y=0;y<WIN_H;y++){ int v=12+y*34/WIN_H; sc(r,v,v+6,v+22,255); SDL_RenderDrawLine(r,0,y,WIN_W,y); }
    uint32_t s=7; for(int i=0;i<60;i++){ int yy=((int)(frand01(&s)*WIN_H+t*18))%WIN_H; sc(r,40,60,90,255); SDL_Rect d={(int)(frand01(&s)*WIN_W),yy,2,2}; SDL_RenderFillRect(r,&d); }
    sc(r,150,185,230,255); fillCircle(r,90,80,26); sc(r,22,32,52,255); fillCircle(r,90,80,20);
    text(r,130,66,3,"PLAYER",180,200,230);
    text(r,WIN_W-250,66,4,"2022",190,210,240);
    const char*names[4]={"STANDSTILL","STORE","LIBRARY","SETTINGS"};
    int tileY=300, tw=320, th=300, gap=40;
    for(int i=0;i<4;i++){ int sel=(i==menuSel);
        int cx=WIN_W/2+(i-menuSel)*(tw+gap);
        int w=sel?tw:tw-70, h=sel?th:th-70, cyc=tileY+th/2;
        int cardx=cx-w/2, cardy=cyc-h/2;
        if(cardx>WIN_W||cardx+w<0) continue;
        sc(r,sel?36:22,sel?58:30,sel?104:46,255); SDL_Rect card={cardx,cardy,w,h}; SDL_RenderFillRect(r,&card);
        if(sel){ sc(r,120,185,255,255); for(int o=0;o<4;o++){ SDL_Rect b={cardx-o,cardy-o,w+2*o,h+2*o}; SDL_RenderDrawRect(r,&b); } }
        if(i==0){ sc(r,24,34,64,255); SDL_Rect a={cardx+18,cardy+18,w-36,h-36}; SDL_RenderFillRect(r,&a);
            sc(r,110,195,255,255); ring(r,cx,cyc,w/5); ring(r,cx,cyc,w/5-6);
            sc(r,235,85,62,255); fillCircle(r,cx,cyc,w/11);
        } else { sc(r,46,52,68,255); SDL_Rect a={cardx+18,cardy+18,w-36,h-36}; SDL_RenderFillRect(r,&a); }
        textC(r,cx,cardy+h+14,sel?4:2,names[i],sel?235:140,sel?240:150,sel?250:170);
    }
    if(menuSel==0){
        textC(r,WIN_W/2,tileY+th+96,4,"TIME MOVES ONLY WHEN YOU DO",120,170,210);
        glyphCross(r,WIN_W/2-170,tileY+th+184,22,1); textC(r,WIN_W/2+30,tileY+th+164,6,"LAUNCH",210,225,245);
    } else {
        textC(r,WIN_W/2,tileY+th+170,4,"COMING SOON",120,135,160);
    }
    textC(r,WIN_W/2,WIN_H-44,2,"PS5 HOME       PLAYSTATION 5 HOMEBREW",90,110,150);
}

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)!=0){ SDL_Log("init %s",SDL_GetError()); return 1; }
    SDL_Window*win=SDL_CreateWindow("STANDSTILL",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer*ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!win||!ren){ SDL_Log("create %s",SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren,WIN_W,WIN_H); SDL_ShowCursor(SDL_DISABLE);
    SDL_Texture*fbtex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,FB_W,FB_H);
    genTextures(); audio_init(); mapRng=(uint32_t)SDL_GetPerformanceCounter()|1u; resetGame();
    gstate=ST_BOOT; stateTime=0;
    Uint64 prev=SDL_GetPerformanceCounter(); double freq=(double)SDL_GetPerformanceFrequency();
    uint32_t shakeRng=12345; int running=1; int padFire=0;
    SDL_GameController*pad=NULL;
    for(int i=0;i<SDL_NumJoysticks();i++) if(SDL_IsGameController(i)){ pad=SDL_GameControllerOpen(i); break; }
    while(running){
        Uint64 now=SDL_GetPerformanceCounter(); double dt=(now-prev)/freq; prev=now; if(dt>0.1)dt=0.1;
        stateTime+=dt;
        double mouseTurn=0;
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT)running=0;
            if(e.type==SDL_MOUSEMOTION && gstate==ST_PLAY) mouseTurn+=e.motion.xrel*0.0022;
            if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && gstate==ST_PLAY) playerShoot();
            if(e.type==SDL_CONTROLLERDEVICEADDED && !pad) pad=SDL_GameControllerOpen(e.cdevice.which);
            if(e.type==SDL_CONTROLLERDEVICEREMOVED){ if(pad)SDL_GameControllerClose(pad); pad=NULL; }
            if(e.type==SDL_CONTROLLERBUTTONDOWN){ int cb=e.cbutton.button, cs=(cb==SDL_CONTROLLER_BUTTON_A);
                if(cb==SDL_CONTROLLER_BUTTON_START){ if(gstate==ST_PLAY){gstate=ST_PAUSE;SDL_SetRelativeMouseMode(SDL_FALSE);} else if(gstate==ST_PAUSE){gstate=ST_PLAY;SDL_SetRelativeMouseMode(SDL_TRUE);} }
                if(gstate==ST_TITLE&&cs){ resetGame(); gstate=ST_PLAY; stateTime=0; SDL_SetRelativeMouseMode(SDL_TRUE); audio_whoosh(); }
                else if(gstate==ST_PLAY&&cs) playerShoot();
                else if(gstate==ST_PAUSE&&cs){ gstate=ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if((gstate==ST_WIN||gstate==ST_LOSE)&&cs){ gstate=ST_TITLE; stateTime=0; }
                if(gstate==ST_PLAY){ if(cb==SDL_CONTROLLER_BUTTON_Y)weaponSwitch(); if(cb==SDL_CONTROLLER_BUTTON_X)playerMelee(); if(cb==SDL_CONTROLLER_BUTTON_B)playerDash(); }
            }
            if(e.type==SDL_KEYDOWN){ SDL_Keycode k=e.key.keysym.sym;
                int start=(k==SDLK_SPACE||k==SDLK_RETURN);
                if(k==SDLK_ESCAPE){ if(gstate==ST_PLAY){gstate=ST_PAUSE;SDL_SetRelativeMouseMode(SDL_FALSE);} else if(gstate==ST_PAUSE){gstate=ST_PLAY;SDL_SetRelativeMouseMode(SDL_TRUE);} else running=0; }
                if(gstate==ST_TITLE&&start){ resetGame(); gstate=ST_PLAY; stateTime=0; SDL_SetRelativeMouseMode(SDL_TRUE); audio_whoosh(); }
                else if(gstate==ST_PLAY&&start){ playerShoot(); }
                else if(gstate==ST_PAUSE&&start){ gstate=ST_PLAY; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if((gstate==ST_WIN||gstate==ST_LOSE)&&start){ gstate=ST_TITLE; stateTime=0; }
                if(gstate==ST_PLAY){ if(k==SDLK_e)playerMelee(); if(k==SDLK_f)weaponSwitch(); if(k==SDLK_LSHIFT)playerDash(); }
                if(k==SDLK_q&&(gstate==ST_PAUSE||gstate==ST_TITLE)) running=0;
            }
        }
        const Uint8*ks=SDL_GetKeyboardState(NULL);
        inFwd=inStrafe=inTurn=0;
        if(gstate==ST_PLAY){
            inFwd=(ks[SDL_SCANCODE_W]?1:0)-(ks[SDL_SCANCODE_S]?1:0);
            inStrafe=(ks[SDL_SCANCODE_D]?1:0)-(ks[SDL_SCANCODE_A]?1:0);
            inTurn=(ks[SDL_SCANCODE_RIGHT]?1:0)-(ks[SDL_SCANCODE_LEFT]?1:0);
            if(pad){ double dz=0.22;
                double lx=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX)/32767.0;
                double ly=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY)/32767.0;
                double rx=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX)/32767.0;
                if(fabs(ly)>dz)inFwd-=ly; if(fabs(lx)>dz)inStrafe+=lx; if(fabs(rx)>dz)inTurn+=rx*1.4;
                if(inFwd>1)inFwd=1; if(inFwd<-1)inFwd=-1; if(inStrafe>1)inStrafe=1; if(inStrafe<-1)inStrafe=-1;
                int r2=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>16000;
                if(r2&&!padFire)playerShoot(); padFire=r2;
            }
            double mv=3.0*dt, rot=2.2*dt;
            double nx=px+(dirX*inFwd+planeX*inStrafe)*mv, ny=py+(dirY*inFwd+planeY*inStrafe)*mv;
            if(!isWall(nx,py))px=nx; if(!isWall(px,ny))py=ny;
            double turn=inTurn*rot+mouseTurn; if(mouseTurn!=0)inTurn=mouseTurn>0?1:-1;
            if(turn!=0){ double a=turn, od=dirX; dirX=dirX*cos(a)-dirY*sin(a); dirY=od*sin(a)+dirY*cos(a);
                double op=planeX; planeX=planeX*cos(a)-planeY*sin(a); planeY=op*sin(a)+planeY*cos(a); }
            bob = sin(stateTime*10.0)*(fabs(inFwd)+fabs(inStrafe));
            if(dashT>0){ double ds=11.0*dt; double dnx=px+dirX*ds, dny=py+dirY*ds; if(!isWall(dnx,py))px=dnx; if(!isWall(px,dny))py=dny; dashT-=dt; }
        }
        double moveMag=fabs(inFwd)+fabs(inStrafe)+fabs(inTurn); if(dashT>0)moveMag=1;
        double target=moveMag>0?1.0:0.03; timeScale+=(target-timeScale)*fmin(1.0,dt*12.0);
        audio_set_time((float)timeScale);
        if(muzzle>0){muzzle-=dt;if(muzzle<0)muzzle=0;} if(shake>0){shake-=dt*2.5;if(shake<0)shake=0;}
        if(flMelee>0)flMelee-=dt; if(flSwitch>0)flSwitch-=dt; if(flDash>0)flDash-=dt; if(dashCd>0)dashCd-=dt;
        double wdt=dt*timeScale;
        if(gstate==ST_PLAY){
            int alive=0;
            for(int i=0;i<N_ENEMIES;i++){ if(!enemies[i].alive)continue; alive++;
                int et=enemies[i].type; double spd = et==1?0.8 : et==2?1.7 : 1.2;
                if(enemies[i].hurt>0)enemies[i].hurt-=dt;
                double dx=px-enemies[i].x,dy=py-enemies[i].y,l=sqrt(dx*dx+dy*dy); if(l<0.01)l=0.01;
                double sx=enemies[i].x+dx/l*spd*wdt, sy=enemies[i].y+dy/l*spd*wdt;
                if(l>1.3){ if(!isWall(sx,enemies[i].y))enemies[i].x=sx; if(!isWall(enemies[i].x,sy))enemies[i].y=sy; }
                enemies[i].cooldown-=wdt;
                if(enemies[i].cooldown<=0){
                    if(et==2){ for(int b=-1;b<=1;b++){ double ca=cos(b*0.26),sa=sin(b*0.26); spawnBullet(enemies[i].x,enemies[i].y,(dx*ca-dy*sa)/l*3.0,(dx*sa+dy*ca)/l*3.0,0);} enemies[i].cooldown=2.6; }
                    else { spawnBullet(enemies[i].x,enemies[i].y,dx/l*3.0,dy/l*3.0,0); enemies[i].cooldown=et==1?2.8:2.2; }
                }
            }
            if(alive==0){ gstate=ST_WIN; stateTime=0; SDL_SetRelativeMouseMode(SDL_FALSE); }
            for(int i=0;i<MAX_BULLETS;i++){ if(!bullets[i].alive)continue;
                bullets[i].x+=bullets[i].vx*wdt; bullets[i].y+=bullets[i].vy*wdt;
                if(isWall(bullets[i].x,bullets[i].y)){bullets[i].alive=0;continue;}
                if(!bullets[i].fromPlayer){ double bx=bullets[i].x-px,by=bullets[i].y-py;
                    if(bx*bx+by*by<0.09&&dashT<=0){ bullets[i].alive=0; health-=3; shake=fmax(shake,0.5); spawnParts(px,py,rgb(220,60,60),6,1.5);
                        if(health<=0){health=0;gstate=ST_LOSE;stateTime=0;SDL_SetRelativeMouseMode(SDL_FALSE);} } }
            }
            for(int i=0;i<MAX_PARTS;i++){ if(parts[i].life<=0)continue; parts[i].x+=parts[i].vx*wdt; parts[i].y+=parts[i].vy*wdt; parts[i].life-=dt; }
            if(comboTimer>0){ comboTimer-=dt; if(comboTimer<=0)combo=0; }
        }
        // ----- render -----
        SDL_SetRenderDrawColor(ren,6,8,14,255); SDL_RenderClear(ren);
        if(gstate==ST_PLAY||gstate==ST_PAUSE||gstate==ST_WIN||gstate==ST_LOSE){
            renderWorld();
            SDL_UpdateTexture(fbtex,NULL,fb,FB_W*sizeof(uint32_t));
            int sx=0,sy=0; if(shake>0){ sx=(int)((frand01(&shakeRng)*2-1)*shake*18); sy=(int)((frand01(&shakeRng)*2-1)*shake*18); }
            SDL_Rect dst={sx-6,sy-6,WIN_W+12,WIN_H+12}; SDL_RenderCopy(ren,fbtex,NULL,&dst);
            drawHUD(ren);
            if(gstate==ST_PAUSE){ sc(ren,0,0,0,150); SDL_Rect f={0,0,WIN_W,WIN_H}; SDL_RenderFillRect(ren,&f);
                textC(ren,WIN_W/2,360,10,"PAUSED",235,235,245);
                glyphCross(ren,WIN_W/2-170,520,22,1); textC(ren,WIN_W/2+30,500,5,"RESUME",200,220,255);
                textC(ren,WIN_W/2,630,4,"Q  QUIT",170,170,185); }
            if(gstate==ST_WIN){ sc(ren,10,40,20,150); SDL_Rect f={0,0,WIN_W,WIN_H}; SDL_RenderFillRect(ren,&f);
                textC(ren,WIN_W/2,340,12,"VICTORY",90,230,120); char b[48]; snprintf(b,sizeof b,"SCORE %d",score); textC(ren,WIN_W/2,500,5,b,235,235,245);
                textC(ren,WIN_W/2,600,4,"PRESS  X  FOR  TITLE",150,180,210); }
            if(gstate==ST_LOSE){ sc(ren,50,10,10,160); SDL_Rect f={0,0,WIN_W,WIN_H}; SDL_RenderFillRect(ren,&f);
                textC(ren,WIN_W/2,360,12,"YOU DIED",230,70,70); textC(ren,WIN_W/2,560,4,"PRESS  X  TO  RETRY",210,150,150); }
        } else if(gstate==ST_BOOT){
            psBackground(ren,stateTime);
            textC(ren,WIN_W/2,WIN_H/2-60,10,"STANDSTILL",200,222,255);
            textC(ren,WIN_W/2,WIN_H/2+70,3,"A BOOMER SHOOTER",90,120,170);
            if(stateTime>1.6){ gstate=ST_TITLE; stateTime=0; }
        } else { // ST_TITLE
            psBackground(ren,stateTime);
            textC(ren,WIN_W/2,330,13,"STANDSTILL",235,235,250);
            textC(ren,WIN_W/2,510,4,"TIME MOVES ONLY WHEN YOU DO",120,170,210);
            if(((int)(stateTime*2))&1){ glyphCross(ren,WIN_W/2-190,650,24,1); textC(ren,WIN_W/2+30,632,6,"START",210,225,245); }
            textC(ren,WIN_W/2,744,3,"WASD MOVE    MOUSE LOOK    SPACE FIRE    E MELEE    F WEAPON    SHIFT DASH",90,120,150);
        }
        SDL_RenderPresent(ren);
    }
    audio_shutdown(); SDL_DestroyTexture(fbtex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
