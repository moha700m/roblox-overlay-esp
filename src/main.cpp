// =====================================================================
//  rbx-esp — External Overlay ESP for Roblox (educational demo)
//  Transparent click-through GDI overlay + external memory reader.
//  Boxes + Names + Distance + Health bars. No DLL, no executor.
//  Volatile offsets live in rbx.ini next to the exe.
// =====================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <algorithm>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")

using u8=unsigned char; using u32=uint32_t; using u64=uint64_t;
using i32=int32_t; using i64=int64_t; using f32=float;
static const f32 PI=3.14159265358979f;

// ------------------------------- config ------------------------------
struct Cfg {
    int64_t off_children=0x60, off_name=0x18, off_classname=0x28;
    u64 off_primitive=0xD8, off_prim_rot=0x120, off_prim_pos=0x150;
    u64 off_players_local=0x130, off_hum_hp=0x18C, off_hum_maxhp=0x190;
    u32 cam_fov_y=70, refresh_ms=12, max_studs=5000;
    bool show_names=true, show_distance=true, show_health=true, colorize=true;
} g_cfg;

static std::string trim(const std::string&s){
    size_t a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos)return "";
    size_t b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
}
static std::string ExeDir(){ wchar_t p[MAX_PATH]{}; GetModuleFileNameW(nullptr,p,MAX_PATH);
    std::wstring s(p); size_t q=s.find_last_of(L"\\/"); return q==std::wstring::npos?".":std::string(s.begin(),s.begin()+q); }
static void LoadCfg(){
    std::ifstream f(ExeDir()+"\\rbx.ini"); if(!f)return;
    std::map<std::string,u64> kv; std::string line;
    while(std::getline(f,line)){ size_t c=line.find(';'); if(c!=std::string::npos)line=line.substr(0,c);
        size_t e=line.find('='); if(e==std::string::npos)continue;
        std::string k=trim(line.substr(0,e)), v=trim(line.substr(e+1));
        if(k.empty()||v.empty())continue; kv[k]=_strtoui64(v.c_str(),nullptr,0); }
    auto U=[&](const char*k,u64* d){ auto it=kv.find(k); if(it!=kv.end()&&d) *d=it->second; };
    U("offset.children",&g_cfg.off_children); U("offset.name",&g_cfg.off_name);
    U("offset.classname",&g_cfg.off_classname); U("offset.primitive",&g_cfg.off_primitive);
    U("offset.prim_rot",&g_cfg.off_prim_rot); U("offset.prim_pos",&g_cfg.off_prim_pos);
    U("offset.players_local",&g_cfg.off_players_local); U("offset.humanoid_hp",&g_cfg.off_hum_hp);
    U("offset.humanoid_maxhp",&g_cfg.off_hum_maxhp);
    U("camera.fov_y",(u64*)&g_cfg.cam_fov_y); U("refresh_ms",(u64*)&g_cfg.refresh_ms);
    U("render.max_studs",(u64*)&g_cfg.max_studs);
    auto B=[&](const char*k,bool& d){ u64 v=(u64)d; U(k,&v); d=v!=0; };
    B("show.names",g_cfg.show_names); B("show.distance",g_cfg.show_distance);
    B("show.health",g_cfg.show_health); B("show.colorful",g_cfg.colorize);
}

// --------------------------- memory access ---------------------------
static HANDLE g_proc=nullptr; static DWORD g_pid=0; static u64 g_base=0,g_size=0;
template<class T> static T R(u64 a){ T v{}; if(g_proc&&a) ReadProcessMemory(g_proc,(LPCVOID)(uintptr_t)a,&v,sizeof v,nullptr); return v; }
static bool RD(u64 a,void*d,size_t n){ return g_proc&&a&&ReadProcessMemory(g_proc,(LPCVOID)(uintptr_t)a,d,n,nullptr); }

static DWORD FindPid(){
    const wchar_t* nm[]={L"RobloxPlayerBeta.exe",L"RobloxCsr.exe",L"Windows10Universal.exe"};
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(s==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof pe; DWORD f=0;
    if(Process32FirstW(s,&pe)) do{ for(auto n:nm) if(!_wcsicmp(pe.szExeFile,n)){f=pe.th32ProcessID;break;} }while(!f&&Process32NextW(s,&pe));
    CloseHandle(s); return f;
}
static bool MainMod(DWORD pid,u64&b,u64&sz){
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid); if(s==INVALID_HANDLE_VALUE)return false;
    MODULEENTRY32W me{}; me.dwSize=sizeof me; bool ok=false;
    if(Module32FirstW(s,&me)){ b=(u64)(uintptr_t)me.modBaseAddr; sz=me.modBaseSize; ok=true; }
    CloseHandle(s); return ok;
}
static u64 PatScan(const std::vector<u8>&p,const std::vector<bool>&w){
    if(!g_size)return 0; const u32 CH=0x200000,OV=0x4000;
    static std::vector<u8> buf; buf.assign(CH+OV,0);
    for(u64 o=0;o<g_size;o+=CH){
        u64 rd=std::min<u64>(CH+OV,g_size-o); if(rd<p.size())break;
        if(!RD(g_base+o,buf.data(),(size_t)rd))continue;
        for(u64 i=0;i+p.size()<=rd;++i){ bool ok=true;
            for(size_t j=0;j<p.size();++j) if(w[j]&&buf[i+j]!=p[j]){ok=false;break;}
            if(ok) return g_base+o+i; }
    } return 0;
}
static void ParsePat(const char*s,std::vector<u8>&p,std::vector<bool>&w){
    p.clear();w.clear();int v=0,nib=0;
    for(;*s;++s){ char c=*s;
        if(c=='?'||c=='*'){ p.push_back(0);w.push_back(false);nib=0;if(s[1]=='?')s++;continue; }
        if(c==' ')continue; int d=(c>='0'&&c<='9')?c-'0':((c|32)-'a'+10); if(d<0)continue;
        if(!nib){v=d<<4;nib=1;}else{p.push_back((u8)(v|d));w.push_back(true);nib=0;} }
}

// ------------------------- string decoding ---------------------------
static std::string RBXStr(u64 st){
    struct{u64 a,b,c;} s{}; if(!RD(st,&s,sizeof s))return {};
    auto tryD=[&](u64 dp,u64 ln)->std::string{
        if(!dp||!ln||ln>1024)return{}; std::vector<char> b((size_t)ln+1,0);
        if(!RD(dp,b.data(),(size_t)ln))return{}; int g=0;
        for(u64 i=0;i<ln;++i){unsigned c=(u8)b[(size_t)i];if(c>=32&&c<127)g++;}
        if(ln&&(f32)g/(f32)ln>=0.92f)return std::string(b.data(),(size_t)ln); return{}; };
    u32 L1=(u32)s.a,H1=(u32)(s.a>>32),L2=(u32)s.b,H2=(u32)(s.b>>32);
    std::string r;
    if(!(r=tryD(s.a,L2)).empty())return r;
    if(!(r=tryD(s.a,H1?H1:L2)).empty())return r;
    if(!(r=tryD(s.a,L1&&L1<4096?L1:H2)).empty())return r;
    if(!(r=tryD(s.b,L1)).empty())return r;
    if(!(r=tryD(s.c,L2)).empty())return r;
    return {};
}

// ---------------------------- tree walking ---------------------------
static std::map<u64,std::pair<std::string,int>> nc,cc; static int tick=1;
static std::string Nm(u64 i){ if(!i)return{}; auto&e=nc[i]; if(e.second!=tick){e={RBXStr(i+g_cfg.off_name),tick};} return e.first; }
static std::string Cl(u64 i){ if(!i)return{}; auto&e=cc[i]; if(e.second!=tick){e={RBXStr(i+g_cfg.off_classname),tick};} return e.first; }
static std::vector<u64> Kids(u64 i){
    std::vector<u64> o; if(!i)return o;
    u64 b=R<u64>(i+g_cfg.off_children), e=R<u64>(i+g_cfg.off_children+8);
    if(!b||e<=b||e-b>0x4000)return o;
    size_t n=(size_t)((e-b)/8); if(n>256)n=256;
    o.resize(n); if(!RD(b,o.data(),n*8))o.clear(); return o;
}
static u64 Child(u64 p,const std::string&nm){ for(u64 c:Kids(p)) if(Nm(c)==nm) return c; return 0; }
static u64 ChildCls(u64 p,const std::string&cl){ for(u64 c:Kids(p)) if(Cl(c)==cl) return c; return 0; }

// ------------------------------ geometry -----------------------------
struct V3{f32 x,y,z;}; static V3 S_(V3 a,V3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
static f32 D_(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;} static f32 L_(V3 a){return sqrtf(D_(a,a));}
struct Pose{V3 p; f32 r[9];};
static bool PrimPose(u64 inst,Pose&o){
    u64 pr=R<u64>(inst+g_cfg.off_primitive); if(!pr)return false;
    f32 rr[9]={},pp[3]={};
    if(!RD(pr+g_cfg.off_prim_rot,rr,sizeof rr)||!RD(pr+g_cfg.off_prim_pos,pp,sizeof pp))return false;
    memcpy(o.r,rr,sizeof rr); o.p={pp[0],pp[1],pp[2]};
    return L_({o.r[0],o.r[1],o.r[2]})>0.5f&&fabsf(pp[0])<1e7&&fabsf(pp[1])<1e7&&fabsf(pp[2])<1e7;
}
static Pose g_cam{}; static f32 g_fov=70; static int g_W=800,g_H=600;
static bool W2S(V3 w,f32&ox,f32&oy){
    V3 rl=S_(w,g_cam.p);
    f32 xv=D_(rl,{g_cam.r[0],g_cam.r[1],g_cam.r[2]});
    f32 yv=D_(rl,{g_cam.r[3],g_cam.r[4],g_cam.r[5]});
    f32 zv=D_(rl,{g_cam.r[6],g_cam.r[7],g_cam.r[8]});
    f32 dz=-zv; if(dz<0.05f)return false;
    f32 ty=tanf(g_fov*(PI/360.f)),tx=ty*((f32)g_W/(f32)g_H);
    f32 nx=(xv/dz)/tx,ny=(yv/dz)/ty;
    ox=((nx+1)*.5f)*(f32)g_W; oy=((1-ny)*.5f)*(f32)g_H;
    return ox>-160&&ox<(f32)g_W+160&&oy>-160&&oy<(f32)g_H+160;
}

// ---------------------------- data model -----------------------------
static u64 g_dm=0;
static bool FindDM(){
    std::vector<u8>p; std::vector<bool>w;
    ParsePat("48 8B 05 ? ? ? ?",p,w);            // mov rax, cs:[FakeDataModel]
    for(u32 disp=3,len=7;;){                      // standard RIP layout
        u64 h=PatScan(p,w); if(!h)break;
        i32 d=0; if(!RD(h+disp,&d,4))break;
        u64 v=R<u64>(h+len+(u64)(i64)d);
        if(v&&Nm(v)=="Roblox"){ g_dm=v; return true; }
        // some builds store DM* directly at target
        if(v){ u64 dv=R<u64>(v); if(dv&&Nm(dv)=="Roblox"){ g_dm=dv; return true; } }
        break;
    }
    return false;
}

// ----------------------------- players --------------------------------
struct PInf{ std::string name; u64 hrp,head,hum; f32 hp,maxhp; };
static std::vector<PInf> g_ps; static u64 g_ws=0,g_plrs=0;

static void Snap(){
    ++tick; g_ws=g_plrs=0;
    if(!g_dm)return;
    for(u64 c:Kids(g_dm)){ std::string k=Cl(c);
        if(k=="Workspace")g_ws=c; else if(k=="Players")g_plrs=c; }
    g_ps.clear(); if(!g_ws||!g_plrs)return;
    u64 lp=R<u64>(g_plrs+g_cfg.off_players_local);
    for(u64 pl:Kids(g_plrs)){
        if(Cl(pl)!="Player")continue; if(pl==lp)continue;
        std::string n=Nm(pl); if(n.empty())continue;
        u64 ch=Child(pl,"Character");
        if(!ch) for(u64 m:Kids(g_ws)) if(Cl(m)=="Model"&&Nm(m)==n){ch=m;break;}
        if(!ch)continue;
        u64 hd=Child(ch,"Head"),hr=Child(ch,"HumanoidRootPart"),hu=ChildCls(ch,"Humanoid");
        if(!hd||!hr)continue;
        PInf p; p.name=n;p.head=hd;p.hrp=hr;p.hum=hu;
        if(hu){p.hp=R<f32>(hu+g_cfg.off_hum_hp);p.maxhp=R<f32>(hu+g_cfg.off_hum_maxhp);
               if(p.maxhp<=0)p.maxhp=100;}
        g_ps.push_back(p);
    }
}

// ----------------------------- overlay --------------------------------
static HWND g_hwnd=nullptr; static const COLORREF KEYCLR=RGB(255,0,254);
static LRESULT CALLBACK WP(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void FitWin(){
    struct C{DWORD pid;HWND h;int w;} c{g_pid,nullptr,0};
    EnumWindows([](HWND h,LPARAM lp)->BOOL{ C*c=(C*)lp;DWORD pid=0;
        GetWindowThreadProcessId(h,&pid); if(pid!=c->pid||!IsWindowVisible(h))return TRUE;
        RECT r{}; if(!GetClientRect(h,&r))return TRUE;
        int w=r.right-r.left,ht=r.bottom-r.top;
        if(w>=400&&ht>=250&&w>c->w){c->h=h;c->w=w;} return TRUE;},(LPARAM)&c);
    if(c.h){RECT r{};GetWindowRect(c.h,&r);
        SetWindowPos(g_hwnd,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE);
        g_W=r.right-r.left; g_H=r.bottom-r.top;}
}
static int ClampC(int x){return x<0?0:x>255?255:x;}

static void Render(){
    HDC wdc=GetDC(g_hwnd); if(!wdc)return;
    HDC mc=CreateCompatibleDC(wdc);
    HBITMAP bm=CreateCompatibleBitmap(wdc,g_W,g_H);
    HGDIOBJ ob=SelectObject(mc,bm);
    HBRUSH bg=CreateSolidBrush(KEYCLR);
    RECT fr{0,0,g_W,g_H}; FillRect(mc,&fr,bg); DeleteObject(bg);
    SetBkMode(mc,TRANSPARENT);

    u64 cam=Child(g_ws,"Camera"); Pose cp{};
    bool ok=cam&&PrimPose(cam,cp);
    if(ok){
        g_cam=cp;
        f32 det=cp.r[0]*(cp.r[4]*cp.r[8]-cp.r[5]*cp.r[7])
              -cp.r[1]*(cp.r[3]*cp.r[8]-cp.r[5]*cp.r[6])
              +cp.r[2]*(cp.r[3]*cp.r[7]-cp.r[4]*cp.r[6]);
        if(det<0)for(int i=0;i<3;++i)g_cam.r[i]=-g_cam.r[i];
        g_fov=(f32)g_cfg.cam_fov_y;
    }
    if(ok){
        HFONT fn=CreateFontW(-13,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FF_SWISS,L"Segoe UI");
        HGDIOBJ of=SelectObject(mc,fn);
        for(PInf&p:g_ps){
            Pose hh,tt; if(!PrimPose(p.hrp,tt)||!PrimPose(p.head,hh))continue;
            f32 d=L_(S_(hh.p,g_cam.p)); if(d<2||(u32)d>g_cfg.max_studs)continue;
            V3 top{hh.p.x,hh.p.y+1.4f,hh.p.z},bot{tt.p.x,tt.p.y-3.2f,tt.p.z},mid=tt.p;
            f32 x0,y0,x1,y1,xm,ym;
            if(!(W2S(top,x0,y0)&&W2S(bot,x1,y1)&&W2S(mid,xm,ym)))continue;
            f32 bh=y1-y0; if(bh<8)continue;
            f32 bw=std::max(16.f,bh*0.55f);
            f32 left=xm-bw*.5f,right=xm+bw*.5f,topY=y0,botY=y1;
            u32 hs=0; for(char c:p.name)hs=hs*31+(u8)c;
            COLORREF col=g_cfg.colorize?RGB(ClampC((hs%180)+70),ClampC(((hs>>5)%180)+70),ClampC(((hs>>10)%180)+70)):RGB(0,255,120);

            // box
            HPEN pn=CreatePen(PS_SOLID,2,col); HGDIOBJ op=SelectObject(mc,pn);
            HGDIOBJ obr=SelectObject(mc,GetStockObject(HOLLOW_BRUSH));
            Rectangle(mc,(int)left,(int)topY,(int)right,(int)botY);
            SelectObject(mc,op); SelectObject(mc,obr); DeleteObject(pn);

            // name
            if(g_cfg.show_names){
                SetTextColor(mc,RGB(255,255,255));
                SIZE ts{}; std::wstring wn(p.name.begin(),p.name.end());
                GetTextExtentPoint32W(mc,wn.c_str(),(int)wn.size(),&ts);
                TextOutW(mc,(int)(xm-ts.cx*.5f),(int)topY-18,wn.c_str(),(int)wn.size());
            }
            // distance
            if(g_cfg.show_distance){
                wchar_t db[32]; swprintf(db,32,L"%.0fm",d);
                SetTextColor(mc,RGB(170,220,255));
                SIZE ts{}; GetTextExtentPoint32W(mc,db,(int)wcslen(db),&ts);
                TextOutW(mc,(int)(xm-ts.cx*.5f),(int)botY+3,db,(int)wcslen(db));
            }
            // health bar
            if(g_cfg.show_health){
                f32 pct=p.hp/p.maxhp; if(pct<0)pct=0; if(pct>1)pct=1;
                float barH=bh*pct, barX=left-6.f;
                HBRUSH bk=CreateSolidBrush(RGB(30,30,30));
                RECT bb{(int)barX,(int)topY,(int)barX+4,(int)botY}; FillRect(mc,&bb,bk); DeleteObject(bk);
                HBRUSH hb=CreateSolidBrush(RGB(ClampC(int(255*(1-pct))),ClampC(int(255*pct)),40));
                RECT gb{(int)barX,(int)(botY-barH),(int)barX+4,(int)botY}; FillRect(mc,&gb,hb); DeleteObject(hb);
            }
        }
        SelectObject(mc,of); DeleteObject(fn);
    }
    BitBlt(wdc,0,0,g_W,g_H,mc,0,0,SRCCOPY);
    DeleteObject(SelectObject(mc,ob));
    DeleteDC(mc); ReleaseDC(g_hwnd,wdc);
}

// ------------------------------- main ---------------------------------
int main(){
    printf("[rbx-esp] launching...\n");
    LoadCfg();
    g_pid=FindPid();
    if(!g_pid){ puts("[!] Roblox process not found. Start Roblox first."); return 1; }
    if(!MainMod(g_pid,g_base,g_size)){ puts("[!] Cannot enumerate Roblox modules."); return 1; }
    g_proc=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,g_pid);
    if(!g_proc){ puts("[!] OpenProcess failed (try admin)."); return 1; }
    printf("[+] Attached to PID %lu base 0x%llx\n",g_pid,(unsigned long long)g_base);

    puts("[*] Scanning for DataModel...");
    for(int i=0;i<200&&!g_dm;++i){ if(FindDM())break; Sleep(250); }
    if(!g_dm){ puts("[!] DataModel signature miss — update sig/offsets in rbx.ini (see README)."); return 2; }
    printf("[+] DataModel 0x%llx\n",(unsigned long long)g_dm);

    // overlay window
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=WP; wc.hInstance=hi; wc.lpszClassName=L"RbxEspOv";
    RegisterClassW(&wc);
    g_hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
        wc.lpszClassName,L"rbx-esp",WS_POPUP,80,80,g_W,g_H,nullptr,nullptr,hi,nullptr);
    if(!g_hwnd){ puts("[!] Window creation failed."); return 3; }
    SetLayeredWindowAttributes(g_hwnd,KEYCLR,0,LWA_COLORKEY);
    ShowWindow(g_hwnd,SW_SHOWNOACTIVATE);

    MSG msg; DWORD lastFit=0,lastSnap=0,lastDraw=0;
    for(;;){
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)goto done;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        DWORD t=GetTickCount();
        if(t-lastSnap>500){ lastSnap=t; Snap(); }
        if(t-lastFit>1000){ lastFit=t; FitWin(); }
        if(t-lastDraw>(DWORD)g_cfg.refresh_ms){ lastDraw=t; Render(); }
        Sleep(2);
    }
done:
    CloseHandle(g_proc);
    return 0;
}
