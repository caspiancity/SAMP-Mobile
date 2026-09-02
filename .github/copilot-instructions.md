SAMP Mobile C++ Client Guidelines
This repository contains an open-source SA-MP C++ Android Client built with RenderWare, RakNet, and ImGui.
General Architecture Rules:
Source Scope: All C++ code is inside app/src/main/cpp/samp/. Do NOT touch build files unless requested.
Crash Prevention:
Always perform nullptr checks before accessing pNetGame, pPlayerPool, pVehiclePool, pGame, and pGUI.
Never skip function signatures or leave incomplete methods.
Encoding Rules:
All client strings, chat logs, and ImGui overlays MUST use UTF-8 string encoding.
Support Azerbaijani alphabet characters: Ə, ə, Ğ, ğ, İ, ı, Ö, ö, Ş, ş, Ç, ç.
Rendering & RPC Integrity:
Do not break existing RenderWare/OpenGL ES hook pipelines when patching RakNet RPC handlers.