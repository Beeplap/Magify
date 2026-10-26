#ifndef MAGIFY_UI_OVERLAY_H
#define MAGIFY_UI_OVERLAY_H

#include "magify.h"

HWND Magify_CreateOverlayWindow(HINSTANCE hInst, int width, int height, POINT cursor);
void Magify_ShowOverlay(MagifyContext *ctx);
void Magify_CloseOverlay(MagifyContext *ctx);
void Magify_RedrawOverlay(MagifyContext *ctx);
int Magify_HitTestRadial(float mouseX, float mouseY, float centerX, float centerY,
                         float innerR, float outerR, int sliceCount, float rotationOffsetDeg);

#endif // MAGIFY_UI_OVERLAY_H
