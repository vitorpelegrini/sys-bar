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
        ComPtr<IUIAutomationCacheRequest> cache;
        ComPtr<IUIAutomationElementArray> found;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) {
            // A hung Explorer must not keep this thread forever.
            ComPtr<IUIAutomation2> uia2;
            if (SUCCEEDED(uia.As(&uia2))) {
                uia2->put_ConnectionTimeout(2000);
                uia2->put_TransactionTimeout(3000);
            }
        }
        // One cross-process round trip for the whole tree: rectangles and owners come back cached.
        if (uia && SUCCEEDED(uia->CreateCacheRequest(&cache)) &&
            SUCCEEDED(cache->AddProperty(UIA_BoundingRectanglePropertyId)) &&
            SUCCEEDED(cache->AddProperty(UIA_ProcessIdPropertyId)) && SUCCEEDED(uia->ElementFromHandle(tray, &root)) &&
            root && SUCCEEDED(uia->CreateTrueCondition(&all)) &&
            SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, all.Get(), cache.Get(), &found)) && found) {
            int count = 0;
            found->get_Length(&count);
            const int self = static_cast<int>(GetCurrentProcessId());
            for (int i = 0; i < count && i < 2000; ++i) {
                ComPtr<IUIAutomationElement> element;
                RECT r{};
                int process = 0;
                if (SUCCEEDED(found->GetElement(i, &element)) && element &&
                    SUCCEEDED(element->get_CachedBoundingRectangle(&r)) &&
                    SUCCEEDED(element->get_CachedProcessId(&process)) && process != self) {  // never our own strip
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
