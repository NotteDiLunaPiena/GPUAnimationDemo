#pragma once

// ƒwƒ‹ƒp[ŠÖ”
template<typename T>
void SafeRelease(T*& p)
{
    if (p) { p->Release(); p = nullptr; }
}

template<typename T>
void SafeDeleteArray(T*& p)
{
    if (p) { delete[] p; p = nullptr; }
}

