#pragma once

class MyMesh;
class DataStore;

bool coreUiBegin(MyMesh &mesh, DataStore &store);
void coreUiStartNetwork();
void coreUiSetReady(bool ready, const char *status);
void coreUiLoop();
bool coreUiCopyPixels(unsigned offset, unsigned length, unsigned char *out);
bool coreUiTouch(unsigned x, unsigned y, bool pressed);
bool coreUiSetText(const char *text);
unsigned coreUiInputDiagnostics(char *out, unsigned capacity);
bool coreUiPanelPixel(unsigned x, unsigned y, unsigned short &rendered, unsigned short &panel);
bool coreUiSetTime(unsigned epoch);
bool coreUiManagementAllowed(unsigned operation);
unsigned coreUiClockPreview(unsigned epoch, char *out, unsigned capacity);
