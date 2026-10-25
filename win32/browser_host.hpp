// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// An in-place OLE container for the system WebBrowser control (MSHTML).
// Plain COM, without ATL or MFC, so that MinGW-w64 builds it as well as
// MSVC. It reports navigation to a listener and leaves every decision about
// what a URL means to it.

#pragma once

#include "win32_util.hpp"

#include <exdisp.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <oleidl.h>

#include <string>

namespace geist_reader {

class BrowserListener {
public:
  virtual ~BrowserListener() = default;
  // Return true to cancel the navigation.
  virtual bool before_navigate(const std::wstring& url) = 0;
  virtual void navigate_complete(const std::wstring& url) { (void)url; }
  virtual void document_complete(const std::wstring& url) { (void)url; }
  virtual void history_changed() {}
};

class BrowserHost final : public IOleClientSite,
                          public IOleInPlaceSite,
                          public IOleInPlaceFrame,
                          public IDocHostUIHandler,
                          public IDispatch {
public:
  struct Options {
    bool allow_scripts = false;
    bool dpi_aware = true;
  };
  // Creates the control as a child of `parent`. Returns null on failure.
  static BrowserHost* create(HWND parent, BrowserListener* listener, Options options);
  // Disconnects and releases the control; the host is released with it.
  void destroy();

  void set_bounds(const RECT& bounds);
  void navigate(const std::wstring& url);
  void go_back();
  void go_forward();
  bool can_go_back() const { return can_back_; }
  bool can_go_forward() const { return can_forward_; }
  void exec(OLECMDID command, OLECMDEXECOPT option = OLECMDEXECOPT_DODEFAULT,
            VARIANT* in = nullptr, VARIANT* out = nullptr);
  void focus();
  // Offers a keyboard message to the control; true when it consumed it.
  bool translate_accelerator(MSG* message);
  HWND window() const;
  ComPtr<IHTMLDocument2> document() const;
  IWebBrowser2* browser() const { return browser_.get(); }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  // IOleClientSite
  HRESULT STDMETHODCALLTYPE SaveObject() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer** out) override { if (out) *out = nullptr; return E_NOINTERFACE; }
  HRESULT STDMETHODCALLTYPE ShowObject() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() override { return E_NOTIMPL; }
  // IOleWindow
  HRESULT STDMETHODCALLTYPE GetWindow(HWND* out) override;
  HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
  // IOleInPlaceSite
  HRESULT STDMETHODCALLTYPE CanInPlaceActivate() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnInPlaceActivate() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnUIActivate() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame** frame, IOleInPlaceUIWindow** document,
                                             LPRECT position, LPRECT clip,
                                             LPOLEINPLACEFRAMEINFO info) override;
  HRESULT STDMETHODCALLTYPE Scroll(SIZE) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE DiscardUndoState() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DeactivateAndUndo() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT) override { return S_OK; }
  // IOleInPlaceUIWindow / IOleInPlaceFrame
  HRESULT STDMETHODCALLTYPE GetBorder(LPRECT) override { return INPLACE_E_NOTOOLSPACE; }
  HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS) override { return INPLACE_E_NOTOOLSPACE; }
  HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject*, LPCOLESTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetMenu(HMENU, HOLEMENU, HWND) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, WORD) override { return S_FALSE; }
  // IDocHostUIHandler
  HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) override { return S_FALSE; }
  HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO* info) override;
  HRESULT STDMETHODCALLTYPE ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*, IOleInPlaceFrame*,
                                   IOleInPlaceUIWindow*) override { return S_FALSE; }
  HRESULT STDMETHODCALLTYPE HideUI() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE UpdateUI() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, const GUID*, DWORD) override { return S_FALSE; }
  HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR* key, DWORD) override { if (key) *key = nullptr; return S_FALSE; }
  HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget*, IDropTarget**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** out) override { if (out) *out = nullptr; return S_FALSE; }
  HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD, LPWSTR, LPWSTR* out) override { if (out) *out = nullptr; return S_FALSE; }
  HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject*, IDataObject** out) override { if (out) *out = nullptr; return S_FALSE; }
  // IDispatch: DWebBrowserEvents2 and the download-control ambient property.
  HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (count) *count = 0; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT* result,
                                   EXCEPINFO*, UINT*) override;

private:
  BrowserHost(HWND parent, BrowserListener* listener, Options options)
      : parent_(parent), listener_(listener), options_(options) {}
  ~BrowserHost() = default;

  LONG refs_ = 1;
  HWND parent_;
  BrowserListener* listener_;
  Options options_;
  RECT bounds_{};
  ComPtr<IOleObject> object_;
  ComPtr<IWebBrowser2> browser_;
  DWORD cookie_ = 0;
  bool can_back_ = false;
  bool can_forward_ = false;
};

} // namespace geist_reader
