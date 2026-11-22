import re, shutil
p = "app/src/main/cpp/samp/game/hooks.cpp"
s = open(p, encoding="utf-8", errors="surrogateescape", newline="").read()
if "CAMDIAG" in s: raise SystemExit("artıq var")
LOG = r'''
	{   // CAMDIAG
		static int cnt = 0;
		if ((++cnt % 90) == 0) {
			CCamera* c = reinterpret_cast<CCamera*>(thiz);
			CCam& a = c->m_aCams[c->m_nActiveCam];
			FLog("CAMDIAG act=%d mode=%d dist=%.2f min=%.2f max=%.2f tz1=%.2f tz2=%.2f tz3=%.2f pedZoom=%d carZoom=%d fov=%.1f alpha=%.2f beta=%.2f fixBeta=%d",
				(int)c->m_nActiveCam, (int)a.m_nMode, a.Distance, a.CA_MIN_DISTANCE, a.CA_MAX_DISTANCE,
				a.m_fTargetZoomGroundOne, a.m_fTargetZoomGroundTwo, a.m_fTargetZoomGroundThree,
				(int)c->m_nPedZoom, (int)c->m_nCarZoom, a.FOV, a.Alpha, a.m_fHorizontalAngle, (int)a.m_bFixingBeta);
		}
	}
'''
pat = r"(void CCamera__Process_hook\(uintptr_t thiz\)\s*\{)"
assert len(re.findall(pat, s)) == 1, "hook tapılmadı"
shutil.copy(p, p + ".bak_diag")
open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(re.sub(pat, lambda m: m.group(1) + LOG, s))
print("OK")
