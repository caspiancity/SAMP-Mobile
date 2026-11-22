import re, shutil
R = "app/src/main/cpp/samp/game/"
def rd(n): return open(R+n, encoding="utf-8", errors="surrogateescape", newline="").read()
def wr(n, s): open(R+n, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
def one(s, pat, fn, name):
    r, n = re.subn(pat, fn, s, flags=re.M)
    if n != 1: raise SystemExit("XETA: %s tapilmadi (n=%d)" % (name, n))
    return r

h = rd("hooks.cpp")
if "CAMDIAG" in h: raise SystemExit("hooks.cpp artiq yamalidir")
DEF = r'''// CAMDIAG
#include <cstdio>
void CamDiagWrite(const char* s)
{
	if (!g_pszStorage) return;
	char p[0x100];
	snprintf(p, sizeof(p), "%sSAMP/camdiag.txt", g_pszStorage);
	FILE* f = fopen(p, "a");
	if (!f) return;
	fprintf(f, "%s\n", s);
	fclose(f);
}
'''
LOG = r'''
	{   // CAMDIAG
		static int cnt = 0;
		if ((++cnt % 30) == 0) {
			CCamera* c = reinterpret_cast<CCamera*>(thiz);
			CCam& a = c->m_aCams[c->m_nActiveCam];
			float px = 0, py = 0, pz = 0; int veh = -1;
			CPlayerPed* pp = pGame ? pGame->FindPlayerPed() : nullptr;
			if (pp && pp->m_pPed) { const CVector q = pp->m_pPed->GetPosition(); px = q.x; py = q.y; pz = q.z; veh = pp->m_pPed->IsInVehicle() ? 1 : 0; }
			char b[480];
			snprintf(b, sizeof(b), "act=%d mode=%d dist=%.2f min=%.2f max=%.2f tz1=%.2f tz2=%.2f tz3=%.2f pedZoom=%d carZoom=%d fov=%.1f alpha=%.2f beta=%.2f fixBeta=%d src=%.1f,%.1f,%.1f ped=%.1f,%.1f,%.1f veh=%d",
				(int)c->m_nActiveCam, (int)a.m_nMode, a.Distance, a.CA_MIN_DISTANCE, a.CA_MAX_DISTANCE,
				a.m_fTargetZoomGroundOne, a.m_fTargetZoomGroundTwo, a.m_fTargetZoomGroundThree,
				(int)c->m_nPedZoom, (int)c->m_nCarZoom, a.FOV, a.Alpha, a.m_fHorizontalAngle, (int)a.m_bFixingBeta,
				a.Source.x, a.Source.y, a.Source.z, px, py, pz, veh);
			CamDiagWrite(b);
		}
	}
'''
h = one(h, r"^void \(\*CCamera__Process\)\(uintptr_t thiz\);", lambda m: DEF + m.group(0), "CCamera__Process decl")
h = one(h, r"(void CCamera__Process_hook\(uintptr_t thiz\)\s*\{)", lambda m: m.group(1) + LOG, "CCamera__Process_hook")
shutil.copy(R + "hooks.cpp", R + "hooks.cpp.bak_diag")
wr("hooks.cpp", h)

p = rd("pad.cpp")
if "CAMDIAG" in p: raise SystemExit("pad.cpp artiq yamalidir")
HK = r'''// CAMDIAG: kamera ikonu yalniz log (heç nəyi dəyişmir)
#include <dlfcn.h>
void CamDiagWrite(const char* s);
uint32_t(*CPad__CycleCameraModeUp)(uintptr_t thiz);
uint32_t CPad__CycleCameraModeUp_hook(uintptr_t thiz)
{
    const uint32_t r = CPad__CycleCameraModeUp(thiz);
    if (r) CamDiagWrite("ICON: CycleCameraModeUp");
    return r;
}
uint32_t(*CPad__CycleCameraModeDown)(uintptr_t thiz);
uint32_t CPad__CycleCameraModeDown_hook(uintptr_t thiz)
{
    const uint32_t r = CPad__CycleCameraModeDown(thiz);
    if (r) CamDiagWrite("ICON: CycleCameraModeDown");
    return r;
}

'''
INST = r'''    {   // CAMDIAG
        void* hl = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        const char* sUp = "_ZN4CPad25CycleCameraModeUpJustDownEv";
        const char* sDn = "_ZN4CPad27CycleCameraModeDownJustDownEv";
        if (hl && dlsym(hl, sUp)) CHook::InlineHook(sUp, &CPad__CycleCameraModeUp_hook, &CPad__CycleCameraModeUp);
        if (hl && dlsym(hl, sDn)) CHook::InlineHook(sDn, &CPad__CycleCameraModeDown_hook, &CPad__CycleCameraModeDown);
    }
'''
p = one(p, r"^uint32_t\(\*CPad__CycleWeaponRightJustDown\)\(uintptr_t thiz\);", lambda m: HK + m.group(0), "CPad decl")
p = one(p, r'^[^\n]*CHook::InlineHook\("_ZN4CPad24CycleWeaponRightJustDownEv"[^\n]*\n', lambda m: m.group(0) + INST, "CPad install")
shutil.copy(R + "pad.cpp", R + "pad.cpp.bak_diag")
wr("pad.cpp", p)
print("OK")
