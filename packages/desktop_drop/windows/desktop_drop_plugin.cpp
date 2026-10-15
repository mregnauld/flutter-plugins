#include "include/desktop_drop/desktop_drop_plugin.h"

#include <windows.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>
#include <algorithm>

namespace {

    using FlutterMethodChannel = std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>;


    using namespace std::literals::string_literals;

    std::string ws2s(const std::wstring &wstr) {
        if (wstr.empty()) {
            return {};
        }
        size_t pos;
        size_t begin = 0;
        std::string ret;

        int size;
        pos = wstr.find(static_cast<wchar_t>(0), begin);
        while (pos != std::wstring::npos && begin < wstr.length()) {
            std::wstring segment = std::wstring(&wstr[begin], pos - begin);
            size = WideCharToMultiByte(CP_UTF8,
                                       WC_ERR_INVALID_CHARS,
                                       &segment[0],
                                       (int) segment.size(),
                                       nullptr,
                                       0,
                                       nullptr,
                                       nullptr);
            std::string converted = std::string(size, 0);
            WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                &segment[0],
                                (int) segment.size(),
                                &converted[0],
                                (int) converted.size(),
                                nullptr,
                                nullptr);
            ret.append(converted);
            ret.append({0});
            begin = pos + 1;
            pos = wstr.find(static_cast<wchar_t>(0), begin);
        }
        if (begin <= wstr.length()) {
            std::wstring segment = std::wstring(&wstr[begin], wstr.length() - begin);
            size =
                    WideCharToMultiByte(CP_UTF8,
                                        WC_ERR_INVALID_CHARS,
                                        &segment[0],
                                        (int) segment.size(),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
            std::string converted = std::string(size, 0);
            WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                &segment[0],
                                (int) segment.size(),
                                &converted[0],
                                (int) converted.size(),
                                nullptr,
                                nullptr);
            ret.append(converted);
        }

        return ret;
    }

    // Paths of the files held by a drag (CF_HDROP); empty when it holds none.
    flutter::EncodableList ReadFilePaths(IDataObject *data_object) {
        flutter::EncodableList list = {};

        FORMATETC fmtetc = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stgmed;
        if (data_object->QueryGetData(&fmtetc) != S_OK || data_object->GetData(&fmtetc, &stgmed) != S_OK) {
            return list;
        }
        auto drop = reinterpret_cast<HDROP>(GlobalLock(stgmed.hGlobal));
        if (drop != nullptr) {
            auto files = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < files; ++i) {
                // Sized from the path itself, so paths longer than MAX_PATH are kept whole.
                auto length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring wide(length + 1, L'\0');
                DragQueryFileW(drop, i, &wide[0], length + 1);
                wide.resize(length);
                list.push_back(flutter::EncodableValue(ws2s(wide)));
            }
            GlobalUnlock(stgmed.hGlobal);
        }
        ReleaseStgMedium(&stgmed);
        return list;
    }


    class DesktopDropTarget : public IDropTarget {
    public:

        DesktopDropTarget(FlutterMethodChannel channel, HWND window_handle);

        HRESULT DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

        HRESULT DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

        HRESULT DragLeave() override;

        HRESULT Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

        HRESULT QueryInterface(const IID &riid, void **ppvObject) override;

        ULONG AddRef() override;

        ULONG Release() override;

        virtual ~DesktopDropTarget();

    private:
        FlutterMethodChannel channel_;
        HWND window_handle_;
        LONG ref_count_;
        bool need_revoke_ole_initialize_;
    };

    class DesktopDropPlugin : public flutter::Plugin {

    public:
        static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

        explicit DesktopDropPlugin(DesktopDropTarget *target);

        ~DesktopDropPlugin() override;

    private:

        DesktopDropTarget *target_;

    };

    DesktopDropPlugin::DesktopDropPlugin(DesktopDropTarget *target) : target_(target) {
        target_->AddRef();
    }

    DesktopDropPlugin::~DesktopDropPlugin() {
        target_->Release();
    }


    // static
    void DesktopDropPlugin::RegisterWithRegistrar(
            flutter::PluginRegistrarWindows *registrar) {
        auto channel =
                std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
                        registrar->messenger(), "desktop_drop",
                        &flutter::StandardMethodCodec::GetInstance());

        HWND hwnd = nullptr;
        if (registrar->GetView()) {
            hwnd = registrar->GetView()->GetNativeWindow();
        }

        if (hwnd == nullptr) {
            // no window, no drop.
            return;
        }

        channel->SetMethodCallHandler([](const auto &call, auto result) {
            result->NotImplemented();
        });

        auto drop_target = new DesktopDropTarget(std::move(channel), hwnd);

        auto plugin = std::make_unique<DesktopDropPlugin>(drop_target);

        registrar->AddPlugin(std::move(plugin));
    }


    DesktopDropTarget::DesktopDropTarget(FlutterMethodChannel channel, HWND window_handle) : channel_(
            std::move(channel)), window_handle_(window_handle), ref_count_(0), need_revoke_ole_initialize_(false) {
        auto ret = RegisterDragDrop(window_handle_, this);
        if (ret == E_OUTOFMEMORY) {
            OleInitialize(nullptr);
            ret = RegisterDragDrop(window_handle_, this);
            if (ret == 0) {
                need_revoke_ole_initialize_ = true;
            }
        }

        if (ret != 0) {
            std::cout << "RegisterDragDrop failed: " << ret << std::endl;
        }
    }

    HRESULT DesktopDropTarget::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
        POINT point = {pt.x, pt.y};
        ScreenToClient(window_handle_, &point);
        // Paths are known before the drop; DragOver doesn't get the data object.
        channel_->InvokeMethod("entered", std::make_unique<flutter::EncodableValue>(
                flutter::EncodableList{
                        flutter::EncodableValue(double(point.x)),
                        flutter::EncodableValue(double(point.y)),
                        flutter::EncodableValue(ReadFilePaths(pDataObj))
                }
        ));
        return 0;
    }

    HRESULT DesktopDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
        POINT point = {pt.x, pt.y};
        ScreenToClient(window_handle_, &point);
        channel_->InvokeMethod("updated", std::make_unique<flutter::EncodableValue>(
                flutter::EncodableList{
                        flutter::EncodableValue(double(point.x)),
                        flutter::EncodableValue(double(point.y))
                }
        ));
        return 0;
    }

    HRESULT DesktopDropTarget::DragLeave() {
        channel_->InvokeMethod("exited", std::make_unique<flutter::EncodableValue>());
        return 0;
    }

    HRESULT DesktopDropTarget::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
        channel_->InvokeMethod("performOperation", std::make_unique<flutter::EncodableValue>(ReadFilePaths(pDataObj)));
        return 0;
    }

    HRESULT DesktopDropTarget::QueryInterface(const IID &iid, void **ppvObject) {
        if (iid == IID_IDropTarget || iid == IID_IUnknown) {
            AddRef();
            *ppvObject = this;
            return S_OK;
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
    }

    ULONG DesktopDropTarget::AddRef() {
        return InterlockedIncrement(&ref_count_);
    }

    ULONG DesktopDropTarget::Release() {
        LONG count = InterlockedDecrement(&ref_count_);

        if (count == 0) {
            delete this;
            return 0;
        } else {
            return count;
        }
    }

    DesktopDropTarget::~DesktopDropTarget() {
        RevokeDragDrop(window_handle_);
        if (need_revoke_ole_initialize_) {
            OleUninitialize();
        }
    }

}  // namespace

void DesktopDropPluginRegisterWithRegistrar(
        FlutterDesktopPluginRegistrarRef registrar) {
    DesktopDropPlugin::RegisterWithRegistrar(
            flutter::PluginRegistrarManager::GetInstance()
                    ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));

}
