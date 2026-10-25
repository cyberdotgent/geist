// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "browser_host.hpp"

#include <exdispid.h>
#include <mshtmdid.h>
#include <ocidl.h>
#include <olectl.h>

namespace geist_reader {
namespace {

// MinGW-w64 headers may predate these documented values.
constexpr DWORD kDocHostDpiAware = 0x40000000;   // DOCHOSTUIFLAG_DPI_AWARE
constexpr DISPID kAmbientDownloadControl = -5512; // DISPID_AMBIENT_DLCONTROL

// The event's argument `index` counted from the first declared parameter.
VARIANT* argument(DISPPARAMS* params, UINT index) {
  if (!params || index >= params->cArgs) return nullptr;
  return &params->rgvarg[params->cArgs - 1 - index];
}

} // namespace

BrowserHost* BrowserHost::create(HWND parent, BrowserListener* listener, Options options) {
  auto* host = new BrowserHost(parent, listener, options);
  GetClientRect(parent, &host->bounds_);
  if (FAILED(CoCreateInstance(CLSID_WebBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_IOleObject,
                              host->object_.put_void())) ||
      !host->object_) {
    host->Release();
    return nullptr;
  }
  host->object_->SetClientSite(static_cast<IOleClientSite*>(host));
  OleSetContainedObject(host->object_.get(), TRUE);
  if (FAILED(host->object_->DoVerb(OLEIVERB_INPLACEACTIVATE, nullptr, static_cast<IOleClientSite*>(host), 0,
                                   parent, &host->bounds_))) {
    host->destroy();
    return nullptr;
  }
  host->browser_ = host->object_.as<IWebBrowser2>(IID_IWebBrowser2);
  if (!host->browser_) {
    host->destroy();
    return nullptr;
  }
  host->browser_->put_Silent(VARIANT_TRUE);
  host->browser_->put_RegisterAsDropTarget(VARIANT_FALSE);
  auto container = host->browser_.as<IConnectionPointContainer>(IID_IConnectionPointContainer);
  ComPtr<IConnectionPoint> point;
  if (container && SUCCEEDED(container->FindConnectionPoint(DIID_DWebBrowserEvents2, point.put())) && point)
    point->Advise(static_cast<IDispatch*>(host), &host->cookie_);
  return host;
}

void BrowserHost::destroy() {
  if (browser_ && cookie_) {
    auto container = browser_.as<IConnectionPointContainer>(IID_IConnectionPointContainer);
    ComPtr<IConnectionPoint> point;
    if (container && SUCCEEDED(container->FindConnectionPoint(DIID_DWebBrowserEvents2, point.put())) && point)
      point->Unadvise(cookie_);
    cookie_ = 0;
  }
  if (browser_) browser_->Stop();
  browser_.reset();
  if (object_) {
    object_->Close(OLECLOSE_NOSAVE);
    object_->SetClientSite(nullptr);
    object_.reset();
  }
  listener_ = nullptr;
  Release();
}

void BrowserHost::set_bounds(const RECT& bounds) {
  bounds_ = bounds;
  if (!browser_) return;
  if (auto in_place = browser_.as<IOleInPlaceObject>(IID_IOleInPlaceObject))
    in_place->SetObjectRects(&bounds_, &bounds_);
}

void BrowserHost::navigate(const std::wstring& url) {
  if (!browser_) return;
  Variant address(url);
  Variant empty;
  browser_->Navigate2(address.get(), empty.get(), empty.get(), empty.get(), empty.get());
}

void BrowserHost::go_back() { if (browser_ && can_back_) browser_->GoBack(); }
void BrowserHost::go_forward() { if (browser_ && can_forward_) browser_->GoForward(); }

void BrowserHost::exec(OLECMDID command, OLECMDEXECOPT option, VARIANT* in, VARIANT* out) {
  if (browser_) browser_->ExecWB(command, option, in, out);
}

void BrowserHost::focus() {
  if (!browser_) return;
  if (auto document = this->document()) {
    ComPtr<IHTMLWindow2> window;
    if (SUCCEEDED(document->get_parentWindow(window.put())) && window) {
      window->focus();
      return;
    }
  }
  if (HWND handle = window()) SetFocus(handle);
}

bool BrowserHost::translate_accelerator(MSG* message) {
  if (!browser_) return false;
  auto active = browser_.as<IOleInPlaceActiveObject>(IID_IOleInPlaceActiveObject);
  return active && active->TranslateAccelerator(message) == S_OK;
}

HWND BrowserHost::window() const {
  HWND handle = nullptr;
  if (browser_)
    if (auto in_place = browser_.as<IOleInPlaceObject>(IID_IOleInPlaceObject)) in_place->GetWindow(&handle);
  return handle;
}

ComPtr<IHTMLDocument2> BrowserHost::document() const {
  ComPtr<IHTMLDocument2> document;
  if (!browser_) return document;
  ComPtr<IDispatch> dispatch;
  if (SUCCEEDED(browser_->get_Document(dispatch.put())) && dispatch)
    document = dispatch.as<IHTMLDocument2>(IID_IHTMLDocument2);
  return document;
}

HRESULT BrowserHost::QueryInterface(REFIID iid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IOleClientSite))
    *out = static_cast<IOleClientSite*>(this);
  else if (IsEqualIID(iid, IID_IOleWindow) || IsEqualIID(iid, IID_IOleInPlaceSite))
    *out = static_cast<IOleInPlaceSite*>(this);
  else if (IsEqualIID(iid, IID_IOleInPlaceUIWindow) || IsEqualIID(iid, IID_IOleInPlaceFrame))
    *out = static_cast<IOleInPlaceFrame*>(this);
  else if (IsEqualIID(iid, IID_IDocHostUIHandler))
    *out = static_cast<IDocHostUIHandler*>(this);
  else if (IsEqualIID(iid, IID_IDispatch) || IsEqualIID(iid, DIID_DWebBrowserEvents2))
    *out = static_cast<IDispatch*>(this);
  if (!*out) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

ULONG BrowserHost::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

ULONG BrowserHost::Release() {
  const LONG count = InterlockedDecrement(&refs_);
  if (!count) delete this;
  return static_cast<ULONG>(count);
}

HRESULT BrowserHost::GetWindow(HWND* out) {
  if (!out) return E_POINTER;
  *out = parent_;
  return S_OK;
}

HRESULT BrowserHost::GetWindowContext(IOleInPlaceFrame** frame, IOleInPlaceUIWindow** document,
                                      LPRECT position, LPRECT clip, LPOLEINPLACEFRAMEINFO info) {
  if (frame) {
    *frame = static_cast<IOleInPlaceFrame*>(this);
    AddRef();
  }
  if (document) *document = nullptr;
  if (position) *position = bounds_;
  if (clip) *clip = bounds_;
  if (info) {
    info->fMDIApp = FALSE;
    info->hwndFrame = GetAncestor(parent_, GA_ROOT);
    info->haccel = nullptr;
    info->cAccelEntries = 0;
  }
  return S_OK;
}

HRESULT BrowserHost::GetHostInfo(DOCHOSTUIINFO* info) {
  if (!info) return E_POINTER;
  info->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_ENABLE_INPLACE_NAVIGATION;
  if (options_.dpi_aware) info->dwFlags |= kDocHostDpiAware;
  info->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
  return S_OK;
}

HRESULT BrowserHost::Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT* result, EXCEPINFO*,
                            UINT*) {
  switch (id) {
  case kAmbientDownloadControl:
    if (result) {
      VariantInit(result);
      result->vt = VT_I4;
      result->lVal = DLCTL_DLIMAGES | DLCTL_VIDEOS | DLCTL_NO_JAVA | DLCTL_NO_DLACTIVEXCTLS |
                     DLCTL_NO_RUNACTIVEXCTLS | DLCTL_NO_BEHAVIORS |
                     (options_.allow_scripts ? 0 : DLCTL_NO_SCRIPTS);
    }
    return S_OK;
  case DISPID_BEFORENAVIGATE2: {
    VARIANT* url = argument(params, 1);
    VARIANT* cancel = argument(params, 6);
    if (url && cancel && cancel->vt == (VT_BYREF | VT_BOOL) && listener_ &&
        listener_->before_navigate(variant_string(*url)))
      *cancel->pboolVal = VARIANT_TRUE;
    return S_OK;
  }
  case DISPID_NEWWINDOW3: {
    // A link that asks for a new window opens in this one (or outside).
    VARIANT* cancel = argument(params, 1);
    VARIANT* url = argument(params, 4);
    if (cancel && cancel->vt == (VT_BYREF | VT_BOOL)) *cancel->pboolVal = VARIANT_TRUE;
    if (url && listener_) {
      const std::wstring address = variant_string(*url);
      if (!listener_->before_navigate(address)) navigate(address);
    }
    return S_OK;
  }
  case DISPID_NAVIGATECOMPLETE2:
    if (VARIANT* url = argument(params, 1))
      if (listener_) listener_->navigate_complete(variant_string(*url));
    return S_OK;
  case DISPID_DOCUMENTCOMPLETE: {
    VARIANT* source = argument(params, 0);
    VARIANT* url = argument(params, 1);
    // Only the top-level document, not its frames.
    bool top = true;
    if (source && source->vt == VT_DISPATCH && source->pdispVal && browser_) {
      ComPtr<IUnknown> theirs, ours;
      source->pdispVal->QueryInterface(IID_IUnknown, theirs.put_void());
      browser_->QueryInterface(IID_IUnknown, ours.put_void());
      top = theirs.get() == ours.get();
    }
    if (top && url && listener_) listener_->document_complete(variant_string(*url));
    return S_OK;
  }
  case DISPID_COMMANDSTATECHANGE: {
    VARIANT* command = argument(params, 0);
    VARIANT* enable = argument(params, 1);
    if (!command || !enable) return S_OK;
    const long which = command->vt == VT_I4 ? command->lVal : 0;
    const bool on = enable->vt == VT_BOOL && enable->boolVal != VARIANT_FALSE;
    if (which == CSC_NAVIGATEBACK) can_back_ = on;
    else if (which == CSC_NAVIGATEFORWARD) can_forward_ = on;
    else return S_OK;
    if (listener_) listener_->history_changed();
    return S_OK;
  }
  default:
    return DISP_E_MEMBERNOTFOUND;
  }
}

} // namespace geist_reader
