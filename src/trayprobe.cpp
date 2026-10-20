#include "trayprobe.h"

#include "logic.h"

#include <objbase.h>
#include <uiautomation.h>

#include <vector>

namespace trayprobe {

int MeasureRightControls(HWND tray) {
    RECT trayRect{};
    if (!IsWindow(tray) || !GetWindowRect(tray, &trayRect)) return -1;

    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<RECT> elements;
    {
        ComPtr<IUIAutomation> uia;
        ComPtr<IUIAutomationElement> root;
        ComPtr<IUIAutomationCondition> all;
        ComPtr<IUIAutomationElementArray> found;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) &&
            SUCCEEDED(uia->ElementFromHandle(tray, &root)) && root && SUCCEEDED(uia->CreateTrueCondition(&all)) &&
            SUCCEEDED(root->FindAll(TreeScope_Descendants, all.Get(), &found)) && found) {
            int count = 0;
            found->get_Length(&count);
            for (int i = 0; i < count && i < 2000; ++i) {
                ComPtr<IUIAutomationElement> element;
                RECT r{};
                if (SUCCEEDED(found->GetElement(i, &element)) && element &&
                    SUCCEEDED(element->get_CurrentBoundingRectangle(&r))) {
                    elements.push_back(r);
                }
            }
        }
    }
    if (SUCCEEDED(init)) CoUninitialize();

    const UINT dpi = GetDpiForWindow(tray);
    const int maxGap = MulDiv(logic::kTrayClusterGapDip, static_cast<int>(dpi ? dpi : 96), 96);
    return logic::RightControlsWidth(trayRect, elements, maxGap);
}

}  // namespace trayprobe
