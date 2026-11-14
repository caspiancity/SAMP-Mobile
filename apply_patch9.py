#!/usr/bin/env python3
# apply_patch9.py - nametag: hamida AZ bayragi + temiz ad paneli
import os, re, shutil
ROOT = os.getcwd()
P = "app/src/main/cpp/samp/playertags.cpp"
full = os.path.join(ROOT, P)
s = open(full, encoding="utf-8").read()
if "AZFLAG_PATCH9" in s:
    print("[KECDI] artiq tetbiq olunub"); raise SystemExit

HELPER = r'''// ===== AZFLAG_PATCH9 =====
static void DrawAzFlag(ImDrawList* dl, ImVec2 p, float w, float h)
{
	float s = h / 3.0f;
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + s), IM_COL32(0, 181, 226, 255));
	dl->AddRectFilled(ImVec2(p.x, p.y + s), ImVec2(p.x + w, p.y + 2 * s), IM_COL32(239, 51, 64, 255));
	dl->AddRectFilled(ImVec2(p.x, p.y + 2 * s), ImVec2(p.x + w, p.y + h), IM_COL32(80, 158, 47, 255));
	ImVec2 c(p.x + w * 0.44f, p.y + h * 0.5f);
	float r = h * 0.2f;
	dl->AddCircleFilled(c, r, IM_COL32(255, 255, 255, 255), 14);
	dl->AddCircleFilled(ImVec2(c.x + r * 0.35f, c.y), r * 0.8f, IM_COL32(239, 51, 64, 255), 14);
	dl->AddCircleFilled(ImVec2(c.x + r * 1.55f, c.y), r * 0.28f, IM_COL32(255, 255, 255, 255), 8);
	dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 150));
}

'''

NEWNAME = r'''// AZFLAG_PATCH9: bayraq + ad, tund yarimseffaf panel icinde
	{
		const float fs = UISettings::fontSize() / 2;
		const ImVec2 ts = renderer->calculateTextSize(szNick, fs);
		const float flagH = fs * 0.95f;
		const float flagW = flagH * 1.5f;
		const float gap = fs * 0.35f;
		const float pad = fs * 0.3f;
		const float totalW = flagW + gap + ts.x;
		const float x0 = pos.x - totalW / 2;

		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		dl->AddRectFilled(ImVec2(x0 - pad, pos.y - 1.0f), ImVec2(x0 + totalW + pad, pos.y + ts.y + 1.0f),
		                  IM_COL32(0, 0, 0, 110), fs * 0.4f);
		DrawAzFlag(dl, ImVec2(x0, pos.y + (ts.y - flagH) / 2), flagW, flagH);
		renderer->drawText(ImVec2(x0 + flagW + gap, pos.y), __builtin_bswap32(dwColor | (0x000000FF)),
		                   std::string(szNick), true, fs);
	}
'''

pat = re.compile(r'^[ \t]*pos\.x -= renderer->calculateTextSize\(szNick, UISettings::fontSize\(\) / 2\)\.x / 2;[ \t]*\n'
                 r'[ \t]*renderer->drawText\(pos, __builtin_bswap32\(dwColor \| \(0x000000FF\)\), std::string\(szNick\), true, UISettings::fontSize\(\) / 2\);[ \t]*\n', re.M)
m = pat.search(s)
d = re.search(r'^void CPlayerTags::Draw\(', s, re.M)
if not m or not d:
    print("[KECDI] anker tapilmadi (playertags.cpp deyisib?)"); raise SystemExit
s = s[:m.start()] + "\t" + NEWNAME + s[m.end():]
d = re.search(r'^void CPlayerTags::Draw\(', s, re.M)
s = s[:d.start()] + HELPER + s[d.start():]
shutil.copy(full, full + ".bak9")
open(full, "w", encoding="utf-8").write(s)
print("[OK] playertags.cpp: AZ bayragi + yeni ad paneli. Geri qaytarmaq: playertags.cpp.bak9")
