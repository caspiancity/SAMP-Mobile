#include "main.h"
#include "game/game.h"
#include "game/RW/RenderWare.h"
#include "net/netgame.h"
#include "gui/gui.h"
#include "playertags.h"
#include "util/CUtil.h"
#include "game/World.h"
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <algorithm>

extern CGame* pGame;
extern CNetGame* pNetGame;

CPlayerTags::CPlayerTags()
{
	FLog("Loading AFK icon..");
	m_pAFKIconTexture = CUtil::LoadTextureFromDB("samp", "afk_icon");
    FLog("Loading AFK icon1..");
	m_pMicroIconTexture = CUtil::LoadTextureFromDB("samp", "afk_icon");
    FLog("Loading AFK icon2..");
}

CPlayerTags::~CPlayerTags() {}

void CPlayerTags::Render(ImGuiRenderer* renderer)
{
	CVector vecPos;
	RwMatrix matLocal, matPlayer;
	int dwHitEntity;
	char szNickBuf[64];

    static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + (VER_x32 ? 0x00951FA8 : 0xBBA8D0));

	if (pNetGame && pNetGame->m_pNetSet->bShowNameTags)
	{
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
        matLocal = pGame->FindPlayerPed()->m_pPed->GetMatrix().ToRwMatrix();

		for (PLAYERID playerId = 0; playerId < MAX_PLAYERS; playerId++)
		{
			if (pPlayerPool->GetSlotState(playerId) == true)
			{
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(playerId);

				if (pRemotePlayer && pRemotePlayer->IsActive() && pRemotePlayer->m_bShowNameTag && !pRemotePlayer->IsNPC())
				{
					CPlayerPed* pPlayerPed = pRemotePlayer->GetPlayerPed();

					if (pPlayerPed && pPlayerPed->m_pPed->GetDistanceFromCamera() <= pNetGame->m_pNetSet->fNameTagDrawDistance)
					{
						/*if (pRemotePlayer->GetState() == PLAYER_STATE_DRIVER &&
							pRemotePlayer->m_pCurrentVehicle &&
							pRemotePlayer->m_pCurrentVehicle->IsRCVehicle())
						{
							pRemotePlayer->m_pCurrentVehicle->GetMatrix(&matPlayer);
							vecPos.x = matPlayer.pos.x;
							vecPos.y = matPlayer.pos.y;
							vecPos.z = matPlayer.pos.z;
						}
						else
						{*/
							if (!pPlayerPed->m_pPed->IsAdded()) continue;
							vecPos.x = 0.0f;
							vecPos.y = 0.0f;
							vecPos.z = 0.0f;
							pPlayerPed->GetBonePosition(8, &vecPos);
						//}

						CAMERA_AIM* pCam = GameGetInternalAim();
						dwHitEntity = 0;

						if (pNetGame->m_pNetSet->bNameTagLOS)
						{
							dwHitEntity = CWorld::GetIsLineOfSightClear(vecPos, TheCamera.GetPosition(), true, false, false, true, false, false, false);
						}

						if (!pNetGame->m_pNetSet->bNameTagLOS || dwHitEntity && !pRemotePlayer->IsNPC())
						{
							sprintf(szNickBuf, "%s (%d)", pPlayerPool->GetPlayerName(playerId), playerId);
							this->Draw(renderer,&vecPos, szNickBuf,
								pRemotePlayer->GetPlayerColor(),
								pPlayerPed->m_pPed->GetDistanceFromCamera(),
								pRemotePlayer->m_fReportedHealth,
								pRemotePlayer->m_fReportedArmour,
								pRemotePlayer->m_bIsAFK,
								false);
						}
					}
				}
			}
		}
	}
}

// ===== AZFLAG_PATCH9 =====
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

// ===== PLAYERTAG_V5: MTA stilinde yigcam nametag =====
ImFont* g_pNickFont = nullptr;   // imguiwrapper.cpp doldurur (nick.ttf varsa)

static inline float PtClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static ImU32 PtHealthColor(float f, int a)
{
	// 1.0 yasil -> 0.5 sari -> 0.0 qirmizi
	int r, g, b;
	if (f > 0.5f) {
		const float t = (f - 0.5f) * 2.0f;
		r = (int)(255.0f + (88.0f - 255.0f) * t);
		g = (int)(204.0f + (214.0f - 204.0f) * t);
		b = (int)(64.0f + (112.0f - 64.0f) * t);
	} else {
		const float t = f * 2.0f;
		r = (int)(235.0f + (255.0f - 235.0f) * t);
		g = (int)(77.0f + (204.0f - 77.0f) * t);
		b = (int)(77.0f + (64.0f - 77.0f) * t);
	}
	return IM_COL32(r, g, b, a);
}

void CPlayerTags::Draw(ImGuiRenderer* renderer, CVector* vec, const char* szNick, uint32_t dwColor, float fDist, float fHealth, float fArmour, bool bAfk, bool bMicro)
{
	CVector vecTagPos;
	vecTagPos.x = vec->x;
	vecTagPos.y = vec->y;
	vecTagPos.z = vec->z;
	vecTagPos.z += 0.25f + (fDist * 0.0475f);

	CVector vecOut;
	// CSprite::CalcScreenCoors
	((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + (VER_x32 ? 0x005C57E8 + 1 : 0x6E9DF8)))(&vecTagPos, &vecOut, 0, 0, 0, 0);

	if (vecOut.z < 1.0f) return;

	// mesafeye gore seffaflig ve olcu
	const float maxD = (pNetGame && pNetGame->m_pNetSet) ? pNetGame->m_pNetSet->fNameTagDrawDistance : 70.0f;
	const float fadeFrom = maxD * 0.5f;
	float fade = 1.0f;
	if (fDist > fadeFrom) fade = 1.0f - 0.6f * (fDist - fadeFrom) / ((maxD - fadeFrom) > 1.0f ? (maxD - fadeFrom) : 1.0f);
	fade = PtClamp(fade, 0.35f, 1.0f);
	const float scale = PtClamp(1.15f - fDist / (maxD * 1.3f), 0.65f, 1.0f);

	// "Ad (12)" -> "Ad" + "(12)"
	char name[48];
	char idtxt[16] = "";
	snprintf(name, sizeof(name), "%s", szNick);
	{
		char* p = strrchr(name, '(');
		if (p && p > name && p[-1] == ' ') {
			snprintf(idtxt, sizeof(idtxt), "(%d)", atoi(p + 1));
			p[-1] = '\0';
		}
	}

	ImFont* font = g_pNickFont ? g_pNickFont : ImGui::GetFont();
	const float fs  = PtClamp(UISettings::fontSize() * 0.30f * scale, 11.0f, 30.0f);
	const float fs2 = fs * 0.80f;
	const ImVec2 nsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
	const ImVec2 isz = idtxt[0] ? font->CalcTextSizeA(fs2, FLT_MAX, 0.0f, idtxt) : ImVec2(0.0f, 0.0f);

	const float gap   = fs * 0.28f;
	const float flagH = fs * 0.62f;
	const float flagW = flagH * 1.5f;
	float w = flagW + gap + nsz.x;
	if (idtxt[0]) w += gap + isz.x;

	const float x0 = vecOut.x - w * 0.5f;
	const float y0 = vecOut.y;
	const int   A  = (int)(255.0f * fade);

	ImDrawList* dl = ImGui::GetBackgroundDrawList();

	auto outlined = [&](float x, float y, float size, ImU32 col, const char* txt)
	{
		const ImU32 bk = IM_COL32(0, 0, 0, (int)(230.0f * fade));
		dl->AddText(font, size, ImVec2(x - 1.0f, y), bk, txt);
		dl->AddText(font, size, ImVec2(x + 1.0f, y), bk, txt);
		dl->AddText(font, size, ImVec2(x, y - 1.0f), bk, txt);
		dl->AddText(font, size, ImVec2(x, y + 1.0f), bk, txt);
		dl->AddText(font, size, ImVec2(x, y), col, txt);
	};

	// bayraq (kicik) + ad + (id)
	float cx = x0;
	DrawAzFlag(dl, ImVec2(cx, y0 + (nsz.y - flagH) * 0.5f), flagW, flagH);
	cx += flagW + gap;

	const ImU32 base = __builtin_bswap32(dwColor | 0x000000FF);
	const ImU32 nameCol = (base & 0x00FFFFFFu) | ((ImU32)A << 24);
	outlined(cx, y0, fs, nameCol, name);
	cx += nsz.x;
	if (idtxt[0]) {
		cx += gap;
		outlined(cx, y0 + (nsz.y - isz.y) * 0.5f, fs2, IM_COL32(190, 194, 208, A), idtxt);
		cx += isz.x;
	}

	// AFK / mikrofon (kicik)
	if (bAfk || bMicro) {
		const float ic = fs * 1.05f;
		if (bAfk && m_pAFKIconTexture)
			dl->AddImage((ImTextureID)m_pAFKIconTexture->raster, ImVec2(cx + gap, y0), ImVec2(cx + gap + ic, y0 + ic));
		if (bMicro && m_pMicroIconTexture)
			dl->AddImage((ImTextureID)m_pMicroIconTexture->raster, ImVec2(x0 - gap - ic, y0), ImVec2(x0 - gap, y0 + ic));
	}

	// can / zireh (nazik, MTA kimi)
	if (fHealth < 0.0f) return;

	const float bw = PtClamp(w * 0.85f, 38.0f, 96.0f);
	const float bh = PtClamp(fs * 0.30f, 3.0f, 6.0f);
	const float bx = vecOut.x - bw * 0.5f;
	float by = y0 + nsz.y + fs * 0.16f;

	auto bar = [&](float y, float frac, ImU32 fill)
	{
		dl->AddRectFilled(ImVec2(bx - 1.0f, y - 1.0f), ImVec2(bx + bw + 1.0f, y + bh + 1.0f), IM_COL32(0, 0, 0, (int)(210.0f * fade)), 1.5f);
		dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, y + bh), IM_COL32(50, 50, 56, (int)(200.0f * fade)), 1.0f);
		if (frac > 0.0f) dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw * frac, y + bh), fill, 1.0f);
	};

	{
		const float hp = PtClamp(fHealth / 100.0f, 0.0f, 1.0f);
		bar(by, hp, PtHealthColor(hp, A));
		by += bh + 2.0f;
	}
	if (fArmour > 0.0f)
		bar(by, PtClamp(fArmour / 100.0f, 0.0f, 1.0f), IM_COL32(215, 220, 235, A));
}
