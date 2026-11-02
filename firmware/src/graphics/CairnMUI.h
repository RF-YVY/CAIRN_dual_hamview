#pragma once
unsigned cairnMuiSignalSamples();

#if defined(CAIRN_MUI)
void cairnMuiInstall();
bool cairnMuiReady();
void cairnMuiSetNavigationGuard(bool (*guard)(void *), void *context);
extern "C" void hamview_mui_open();
#endif
