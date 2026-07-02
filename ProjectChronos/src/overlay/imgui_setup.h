#pragma once
#include <windows.h>
#include <d3d11.h>

// Initialize ImGui for DX11 overlay
bool ImGui_Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
void ImGui_Shutdown();
void ImGui_NewFrame();
void ImGui_EndFrame();

// Render text using ImGui's draw list
void ImGui_DrawText(const char* text, float x, float y, float r, float g, float b, float a, bool centered = false);