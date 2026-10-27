#pragma once

void ledInit();
// r/g/b are 0..255 and are scaled down by the global brightness cap.
void ledSetColor(int r, int g, int b);
