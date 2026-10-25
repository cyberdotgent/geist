// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "print_job.hpp"

#include "book_source.hpp"
#include "browser_host.hpp"
#include "dialogs.hpp"
#include "print_plan.hpp"
#include "protocol.hpp"
#include "win32_util.hpp"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace geist_reader {
namespace {

const wchar_t kHostClass[] = L"GeistBOOReaderPrintHost";
constexpr UINT kContinue = WM_APP + 3;
constexpr double kCssPerPoint = 96.0 / 72.0;

// Page geometry in device units; drawing coordinates start at the printable
// area, which is offset from the paper's edge.
struct Geometry {
  int dpi_x = 96, dpi_y = 96;
  int width = 816, height = 1056;
  int offset_x = 0, offset_y = 0;
  double width_pt() const { return width * 72.0 / dpi_x; }
  double height_pt() const { return height * 72.0 / dpi_y; }
  int x(double pt) const { return static_cast<int>(std::lround(pt * dpi_x / 72.0)) - offset_x; }
  int y(double pt) const { return static_cast<int>(std::lround(pt * dpi_y / 72.0)) - offset_y; }
  int dx(double pt) const { return static_cast<int>(std::lround(pt * dpi_x / 72.0)); }
  int dy(double pt) const { return static_cast<int>(std::lround(pt * dpi_y / 72.0)); }
  Geometry rotated() const {
    Geometry out = *this;
    std::swap(out.dpi_x, out.dpi_y);
    std::swap(out.width, out.height);
    std::swap(out.offset_x, out.offset_y);
    return out;
  }
};

Geometry device_geometry(HDC dc) {
  Geometry g;
  g.dpi_x = GetDeviceCaps(dc, LOGPIXELSX);
  g.dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
  g.width = GetDeviceCaps(dc, PHYSICALWIDTH);
  g.height = GetDeviceCaps(dc, PHYSICALHEIGHT);
  g.offset_x = GetDeviceCaps(dc, PHYSICALOFFSETX);
  g.offset_y = GetDeviceCaps(dc, PHYSICALOFFSETY);
  if (g.width <= 0 || g.height <= 0) { // a display-like device
    g.width = GetDeviceCaps(dc, HORZRES);
    g.height = GetDeviceCaps(dc, VERTRES);
  }
  return g;
}

// Where the pages go: a printer, or bitmaps in a directory.
class PageSink {
public:
  virtual ~PageSink() = default;
  virtual bool begin(const std::wstring& title) = 0;
  virtual HDC begin_page(bool wide) = 0;
  virtual bool end_page() = 0;
  virtual bool end() = 0;
  virtual void abort() = 0;
  virtual Geometry geometry(bool wide) const = 0;
  virtual HDC measuring_dc() const = 0;
};

class PrinterSink final : public PageSink {
public:
  PrinterSink(HDC dc, HGLOBAL devmode, HGLOBAL devnames) : dc_(dc), devmode_handle_(devmode), devnames_(devnames) {
    if (devmode_handle_) {
      const auto* mode = static_cast<const DEVMODEW*>(GlobalLock(devmode_handle_));
      if (mode) {
        const std::size_t size = mode->dmSize + mode->dmDriverExtra;
        devmode_.assign(reinterpret_cast<const BYTE*>(mode), reinterpret_cast<const BYTE*>(mode) + size);
        GlobalUnlock(devmode_handle_);
      }
    }
    base_ = device_geometry(dc_);
    base_landscape_ = base_.width > base_.height;
  }
  ~PrinterSink() override {
    if (started_) AbortDoc(dc_);
    DeleteDC(dc_);
    if (devmode_handle_) GlobalFree(devmode_handle_);
    if (devnames_) GlobalFree(devnames_);
  }
  bool begin(const std::wstring& title) override {
    DOCINFOW info{};
    info.cbSize = sizeof info;
    info.lpszDocName = title.c_str();
    started_ = StartDocW(dc_, &info) > 0;
    return started_;
  }
  HDC begin_page(bool wide) override {
    const bool rotate = wide && !base_landscape_;
    if (rotate != rotated_ && devmode_.size() >= sizeof(DEVMODEW)) {
      std::vector<BYTE> copy = devmode_;
      auto* mode = reinterpret_cast<DEVMODEW*>(copy.data());
      mode->dmFields |= DM_ORIENTATION;
      mode->dmOrientation = rotate ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
      if (ResetDCW(dc_, mode)) rotated_ = rotate;
    }
    return StartPage(dc_) > 0 ? dc_ : nullptr;
  }
  bool end_page() override { return EndPage(dc_) > 0; }
  bool end() override {
    const bool ok = EndDoc(dc_) > 0;
    started_ = false;
    return ok;
  }
  void abort() override {
    if (started_) AbortDoc(dc_);
    started_ = false;
  }
  Geometry geometry(bool wide) const override {
    const bool rotate = wide && !base_landscape_;
    if (rotate == rotated_) return device_geometry(dc_);
    return rotated_ ? base_ : base_.rotated();
  }
  HDC measuring_dc() const override { return dc_; }

private:
  HDC dc_;
  HGLOBAL devmode_handle_;
  HGLOBAL devnames_;
  std::vector<BYTE> devmode_;
  Geometry base_;
  bool base_landscape_ = false;
  bool rotated_ = false;
  bool started_ = false;
};

// US Letter at 100 dpi, written as numbered BMP files.
class DirectorySink final : public PageSink {
public:
  explicit DirectorySink(std::wstring directory) : directory_(std::move(directory)) {
    screen_ = GetDC(nullptr);
    measure_ = CreateCompatibleDC(screen_);
  }
  ~DirectorySink() override {
    release_page();
    DeleteDC(measure_);
    ReleaseDC(nullptr, screen_);
  }
  bool begin(const std::wstring&) override {
    CreateDirectoryW(directory_.c_str(), nullptr);
    return true;
  }
  HDC begin_page(bool wide) override {
    release_page();
    page_ = geometry(wide);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = page_.width;
    info.bmiHeader.biHeight = page_.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(screen_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    dc_ = CreateCompatibleDC(screen_);
    previous_ = SelectObject(dc_, bitmap_);
    RECT all{0, 0, page_.width, page_.height};
    FillRect(dc_, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    return dc_;
  }
  bool end_page() override {
    wchar_t name[32];
    swprintf(name, 32, L"\\page-%04u.bmp", ++count_);
    const std::wstring path = directory_ + name;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof header;
    header.biWidth = page_.width;
    header.biHeight = page_.height;
    header.biPlanes = 1;
    header.biBitCount = 24;
    const DWORD stride = ((static_cast<DWORD>(page_.width) * 3 + 3) & ~3u);
    const DWORD image = stride * static_cast<DWORD>(page_.height);
    std::vector<BYTE> pixels(image);
    GdiFlush();
    GetDIBits(dc_, bitmap_, 0, static_cast<UINT>(page_.height), pixels.data(),
              reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof file + sizeof header;
    file.bfSize = file.bfOffBits + image;
    FILE* out = _wfopen(path.c_str(), L"wb");
    if (!out) return false;
    fwrite(&file, sizeof file, 1, out);
    fwrite(&header, sizeof header, 1, out);
    fwrite(pixels.data(), 1, pixels.size(), out);
    fclose(out);
    release_page();
    return true;
  }
  bool end() override { return true; }
  void abort() override {}
  Geometry geometry(bool wide) const override {
    Geometry g;
    g.dpi_x = g.dpi_y = 100;
    g.width = 850;
    g.height = 1100;
    return wide ? g.rotated() : g;
  }
  HDC measuring_dc() const override { return measure_; }

private:
  void release_page() {
    if (dc_) {
      SelectObject(dc_, previous_);
      DeleteDC(dc_);
      dc_ = nullptr;
    }
    if (bitmap_) {
      DeleteObject(bitmap_);
      bitmap_ = nullptr;
    }
  }
  std::wstring directory_;
  HDC screen_ = nullptr;
  HDC measure_ = nullptr;
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  Geometry page_;
  unsigned count_ = 0;
};

HFONT point_font(HDC dc, double points, int weight, const wchar_t* face = L"Segoe UI") {
  LOGFONTW font{};
  font.lfHeight = -static_cast<LONG>(std::lround(points * GetDeviceCaps(dc, LOGPIXELSY) / 72.0));
  font.lfWeight = weight;
  font.lfCharSet = DEFAULT_CHARSET;
  font.lfQuality = ANTIALIASED_QUALITY;
  lstrcpynW(font.lfFaceName, face, LF_FACESIZE);
  return CreateFontIndirectW(&font);
}

void draw_text(HDC dc, const std::wstring& text, RECT box, HFONT font, COLORREF color, UINT format) {
  HGDIOBJ previous = SelectObject(dc, font);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, text.c_str(), -1, &box, format | DT_NOPREFIX);
  SelectObject(dc, previous);
}

// The measurement script, run in the hidden print browser once a topic has
// loaded and after every resize: the content's extent and the vertical
// positions of block starts and line starts, in CSS pixels.
const char kMeasureScript[] =
    "<style>html,body{overflow:hidden !important;background:#fff !important;}"
    "body{position:relative;}"
    // Drawn rows print whole: a wide one widens the page instead of
    // scrolling inside its box, so the topic is measured as wide.
    ".geist-preformatted,.geist-opaque{overflow:visible !important;}</style><script>"
    "function geistMeasure(){"
    "var b=document.body,h=document.documentElement;"
    "var sy=window.pageYOffset||h.scrollTop||0;"
    "var blocks=[],lines=[];"
    "var all=b.getElementsByTagName('*');"
    "for(var i=0;i<all.length;i++){var e=all[i];"
    "var s=window.getComputedStyle?getComputedStyle(e,null):e.currentStyle;"
    "if(!s)continue;var d=s.display;"
    "if(d=='block'||d=='table'||d=='table-row'||d=='list-item'||e.tagName=='PRE')"
    "blocks.push(Math.round(e.getBoundingClientRect().top+sy));}"
    "if(document.createRange){"
    "var walk=function(n){for(var c=n.firstChild;c;c=c.nextSibling){"
    "if(c.nodeType==3){if(/\\S/.test(c.nodeValue)){var r=document.createRange();"
    "r.selectNodeContents(c);var rs=r.getClientRects();"
    "for(var k=0;k<rs.length;k++)if(rs[k].bottom>rs[k].top)lines.push(Math.round(rs[k].top+sy));}}"
    "else if(c.nodeType==1)walk(c);}};walk(b);}"
    "var w=Math.max(b.scrollWidth,h.scrollWidth),ht=b.scrollHeight;"
    "b.setAttribute('data-geist-measure',h.clientWidth+' '+w+' '+ht+'|'+blocks.join(',')+'|'+lines.join(','));"
    "}</script>";

struct Measurement {
  double client_width = 0, width = 0, height = 0;
  std::vector<double> blocks, lines;
};

std::vector<double> numbers(const std::string& text) {
  std::vector<double> out;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ','))
    if (!item.empty()) out.push_back(std::atof(item.c_str()));
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

struct TopicLayout {
  bool wide = false;
  double layout_width = 0; // CSS px
  double scale = 1;
  std::vector<PageSlice> slices;
};

class Job final : public BookPrintJob, public BrowserListener {
public:
  Job(HWND owner, std::unique_ptr<PageSink> sink, std::shared_ptr<BookSource> source,
      std::function<void()> finished)
      : owner_(owner), sink_(std::move(sink)), source_(std::move(source)), finished_(std::move(finished)) {}

  ~Job() override {
    progress_.reset();
    if (browser_) browser_->destroy();
    if (host_) {
      SetWindowLongPtrW(host_, GWLP_USERDATA, 0);
      DestroyWindow(host_);
    }
    detach_source(source_.get());
  }

  bool begin() {
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"", WS_POPUP, host_origin_, host_origin_, 800,
                            1000, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!host_) return false;
    SetWindowLongPtrW(host_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    BrowserHost::Options options;
    options.allow_scripts = true;
    options.dpi_aware = false;
    browser_ = BrowserHost::create(host_, this, options);
    if (!browser_) return false;
    ShowWindow(host_, SW_SHOWNOACTIVATE);
    for (const auto& row : source_->contents())
      if (printable_topic(row)) topics_.push_back(row.id);
    progress_ = std::make_unique<ProgressDialog>(owner_, [this] { cancel(); });
    progress_->set_text(L"Rendering the book and calculating page numbers…");
    progress_->set_progress(0, topics_.size());
    PostMessageW(host_, kContinue, 0, 0);
    return true;
  }

  void cancel() override { finish(std::wstring()); }

  static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* job = reinterpret_cast<Job*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_GETMINMAXINFO) {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMaxTrackSize = {30000, 30000};
      info->ptMaxSize = {30000, 30000};
      return 0;
    }
    if (message == kContinue && job) {
      job->step();
      return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  bool before_navigate(const std::wstring& url) override {
    return url.rfind(L"geist:", 0) != 0 && url.rfind(L"about:", 0) != 0;
  }
  void document_complete(const std::wstring& url) override {
    if (!loading_.empty() && url.rfind(L"about:", 0) != 0) {
      loaded_url_ = url;
      PostMessageW(host_, kContinue, 0, 0);
    }
  }

private:
  enum class Phase { measuring, printing, finished };

  // Loads `topic`; returns true when it already is the loaded document.
  bool ensure_loaded(const std::string& topic) {
    if (loaded_ == topic && loading_.empty()) return true;
    if (loading_ == topic && !loaded_url_.empty()) {
      const auto parsed = parse_url(narrow(loaded_url_));
      loaded_url_.clear();
      loading_.clear();
      if (!parsed || parsed->id != topic) {
        fail(topic);
        return false;
      }
      loaded_ = topic;
      Variant zoom(200L);
      browser_->exec(OLECMDID_OPTICAL_ZOOM, OLECMDEXECOPT_DONTPROMPTUSER, zoom.get());
      return true;
    }
    if (loading_.empty()) {
      loading_ = topic;
      loaded_.clear();
      loaded_url_.clear();
      browser_->navigate(widen(source_->topic_url(topic)));
    }
    return false;
  }

  void fail(const std::string& topic) {
    std::wstring title = widen(topic);
    for (const auto& row : source_->contents())
      if (row.id == topic && !row.title.empty()) title = widen(row.title);
    finish(L"Could not paginate " + title + L".");
  }

  void resize(int width, int height) {
    width = std::max(width, 64);
    height = std::max(height, 64);
    SetWindowPos(host_, nullptr, host_origin_, host_origin_, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT bounds{0, 0, width, height};
    browser_->set_bounds(bounds);
  }

  bool call_measure() {
    auto document = browser_->document();
    if (!document) return false;
    ComPtr<IHTMLWindow2> window;
    if (FAILED(document->get_parentWindow(window.put())) || !window) return false;
    auto dispatch = window.as<IDispatch>(IID_IDispatch);
    DISPID id = 0;
    wchar_t name[] = L"geistMeasure";
    LPOLESTR names[] = {name};
    if (dispatch && SUCCEEDED(dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id))) {
      DISPPARAMS none{};
      Variant result;
      if (SUCCEEDED(dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &none, result.get(),
                                     nullptr, nullptr)))
        return true;
    }
    Bstr code(L"geistMeasure()"), language(L"JavaScript");
    Variant result;
    return SUCCEEDED(window->execScript(code.get(), language.get(), result.get()));
  }

  // Lays the loaded topic out `css_width` CSS pixels wide and measures it.
  bool measure(double css_width, double css_height, Measurement& out) {
    resize(static_cast<int>(std::lround(css_width * ratio_)), static_cast<int>(std::lround(css_height * ratio_)));
    auto document = browser_->document();
    ComPtr<IHTMLElement> body;
    if (!document || FAILED(document->get_body(body.put())) || !body) return false;
    if (call_measure()) {
      Bstr attribute(L"data-geist-measure");
      Variant value;
      if (SUCCEEDED(body->getAttribute(attribute.get(), 0, value.get()))) {
        const std::string text = narrow(variant_string(*value.get()));
        const auto bar = text.find('|');
        const auto second = text.find('|', bar == std::string::npos ? 0 : bar + 1);
        if (bar != std::string::npos && second != std::string::npos) {
          std::sscanf(text.c_str(), "%lf %lf %lf", &out.client_width, &out.width, &out.height);
          out.blocks = numbers(text.substr(bar + 1, second - bar - 1));
          out.lines = numbers(text.substr(second + 1));
          return out.client_width > 0;
        }
      }
    }
    // Without script: the extent only, from the DOM.
    auto body2 = body.as<IHTMLElement2>(IID_IHTMLElement2);
    long width = 0, height = 0, client = 0;
    if (!body2) return false;
    body2->get_scrollWidth(&width);
    body2->get_scrollHeight(&height);
    body2->get_clientWidth(&client);
    out.client_width = client ? client : css_width;
    out.width = width;
    out.height = height;
    out.blocks.clear();
    out.lines.clear();
    return true;
  }

  // Device pixels per CSS pixel in the hidden browser, from its own report.
  void calibrate(double css_width) {
    ratio_ = 1;
    Measurement probe;
    if (measure(css_width, 400, probe) && probe.client_width > 0) {
      RECT client{};
      GetClientRect(host_, &client);
      ratio_ = std::max(1.0, client.right / probe.client_width);
    }
  }

  double printable_width(const Geometry& g) const { return (g.width_pt() - 128) * kCssPerPoint; }
  double printable_height(const Geometry& g) const { return (g.height_pt() - 132) * kCssPerPoint; }

  void measure_topic(const std::string& topic) {
    const Geometry base = sink_->geometry(false);
    if (!calibrated_) {
      calibrate(printable_width(base));
      calibrated_ = true;
    }
    TopicLayout layout;
    layout.layout_width = printable_width(base);
    Measurement m;
    if (!measure(layout.layout_width, printable_height(base), m)) return fail(topic);
    Geometry page = base;
    if (m.width > layout.layout_width + 2) {
      layout.wide = true;
      page = sink_->geometry(true);
      layout.layout_width = printable_width(page);
      if (!measure(layout.layout_width, printable_height(page), m)) return fail(topic);
      if (m.width > layout.layout_width + 2) {
        const double fit = layout.layout_width;
        layout.layout_width = std::min(m.width, fit * 4);
        layout.scale = fit / layout.layout_width;
        if (!measure(layout.layout_width, printable_height(page) / layout.scale, m)) return fail(topic);
      }
    }
    layout.slices = paginate(m.height, printable_height(page) / layout.scale, m.blocks, m.lines);
    layouts_[topic] = layout;
  }

  void step() {
    if (phase_ == Phase::finished) return;
    if (phase_ == Phase::measuring) {
      while (index_ < topics_.size()) {
        const std::string topic = topics_[index_];
        if (!ensure_loaded(topic)) return;
        measure_topic(topic);
        if (phase_ == Phase::finished) return;
        ++index_;
        if (progress_) progress_->set_progress(index_, topics_.size());
      }
      plan_pages();
      if (phase_ == Phase::finished) return;
      phase_ = Phase::printing;
      index_ = 0;
      if (!sink_->begin(widen(source_->title().empty() ? std::string("Book") : source_->title())))
        return finish(L"The printer did not accept the document.");
      started_ = true;
    }
    while (index_ < plan_.pages.size()) {
      const PrintPage& page = plan_.pages[index_];
      if (page.kind == PrintPage::Kind::topic) {
        if (!ensure_loaded(page.topic)) return;
        if (!print_topic_page(page)) return;
      } else if (!print_generated_page(page)) {
        return;
      }
      ++index_;
      if (progress_) {
        progress_->set_text(L"Printing page " + std::to_wstring(index_) + L" of " +
                            std::to_wstring(plan_.pages.size()) + L"…");
        progress_->set_progress(index_, plan_.pages.size());
      }
    }
    if (!sink_->end()) return finish(L"The printer did not complete the document.");
    started_ = false;
    finish(std::wstring());
  }

  // The generated contents' rows, page by page, in base orientation.
  std::vector<std::vector<std::size_t>> contents_layout(const std::vector<ContentsRow>& rows) {
    std::vector<std::vector<std::size_t>> pages(1);
    const Geometry g = sink_->geometry(false);
    HDC dc = sink_->measuring_dc();
    HFONT font = point_font(dc, 10.5, FW_NORMAL);
    HGDIOBJ previous = SelectObject(dc, font);
    double y = 112;
    row_heights_.assign(rows.size(), 19);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const double indent = std::min<std::uint32_t>(rows[i].level, 4) * 16.0;
      const std::wstring title = widen(rows[i].title.empty() ? rows[i].id : rows[i].title);
      RECT box{0, 0, std::max(1, g.dx(g.width_pt() - 144 - indent - 45)), 0};
      DrawTextW(dc, title.c_str(), -1, &box, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
      const double height = std::max(19.0, (box.bottom - box.top) * 72.0 / g.dpi_y + 5);
      row_heights_[i] = height;
      if (y + height > g.height_pt() - 84 && !pages.back().empty()) {
        pages.emplace_back();
        y = 112;
      }
      pages.back().push_back(i);
      y += height;
    }
    SelectObject(dc, previous);
    DeleteObject(font);
    return pages;
  }

  void plan_pages() {
    std::map<std::string, std::size_t> counts;
    for (const auto& entry : layouts_) counts[entry.first] = entry.second.slices.size();
    plan_ = plan_book(source_->contents(), counts, [this](const std::vector<ContentsRow>& listing) {
      contents_pages_ = contents_layout(listing);
      return contents_pages_.size();
    });
  }

  void draw_page_number(HDC dc, const Geometry& g, std::size_t number) {
    if (!number) return;
    HFONT font = point_font(dc, 10, FW_NORMAL);
    RECT box{g.x(g.width_pt() - 114), g.y(g.height_pt() - 49), g.x(g.width_pt() - 72), g.y(g.height_pt() - 31)};
    draw_text(dc, std::to_wstring(number), box, font, RGB(64, 64, 64), DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
    DeleteObject(font);
  }

  bool print_generated_page(const PrintPage& page) {
    HDC dc = sink_->begin_page(false);
    if (!dc) return finish(L"The printer did not accept a page."), false;
    const Geometry g = sink_->geometry(false);
    const COLORREF ink = RGB(0, 0, 0);
    if (page.kind == PrintPage::Kind::title) {
      const BookInformation& info = source_->information();
      std::wstring title = widen(!info.title.empty() ? info.title : info.short_title);
      if (title.empty()) title = L"Untitled Book";
      const double h = g.height_pt();
      HFONT big = point_font(dc, 28, FW_BOLD), normal = point_font(dc, 14, FW_NORMAL), small = point_font(dc, 11, FW_NORMAL);
      draw_text(dc, title, RECT{g.x(72), g.y(h * .26), g.x(g.width_pt() - 72), g.y(h * .52)}, big, ink, DT_WORDBREAK);
      draw_text(dc, widen(info.authors), RECT{g.x(72), g.y(h * .61 - 54), g.x(g.width_pt() - 72), g.y(h * .61)}, normal,
                ink, DT_WORDBREAK);
      draw_text(dc, widen(info.document_number), RECT{g.x(72), g.y(h - 128), g.x(g.width_pt() - 72), g.y(h - 98)},
                small, ink, DT_WORDBREAK);
      DeleteObject(big);
      DeleteObject(normal);
      DeleteObject(small);
    } else {
      const double w = g.width_pt();
      HFONT heading = point_font(dc, 22, FW_BOLD), label = point_font(dc, 10, FW_SEMIBOLD),
            body = point_font(dc, 10.5, FW_NORMAL);
      draw_text(dc, page.index ? L"Contents (continued)" : L"Contents", RECT{g.x(72), g.y(47), g.x(w - 72), g.y(85)},
                heading, ink, DT_SINGLELINE);
      draw_text(dc, L"Page", RECT{g.x(w - 117), g.y(91), g.x(w - 72), g.y(111)}, label, ink, DT_SINGLELINE | DT_RIGHT);
      double y = 112;
      if (page.index < contents_pages_.size())
        for (std::size_t row_index : contents_pages_[page.index]) {
          const ContentsRow& row = plan_.listing[row_index];
          const double indent = std::min<std::uint32_t>(row.level, 4) * 16.0;
          const double height = row_heights_[row_index];
          const std::wstring title = widen(row.title.empty() ? row.id : row.title);
          draw_text(dc, title, RECT{g.x(72 + indent), g.y(y), g.x(w - 72 - 45), g.y(y + height)}, body, ink,
                    DT_WORDBREAK);
          const auto number = plan_.page_numbers.find(row.id);
          if (number != plan_.page_numbers.end())
            draw_text(dc, std::to_wstring(number->second), RECT{g.x(w - 112), g.y(y), g.x(w - 72), g.y(y + height)},
                      body, ink, DT_SINGLELINE | DT_RIGHT);
          y += height;
        }
      DeleteObject(heading);
      DeleteObject(label);
      DeleteObject(body);
    }
    draw_page_number(dc, g, page.number);
    if (!sink_->end_page()) return finish(L"The printer did not accept a page."), false;
    return true;
  }

  // Renders the browser's current view into a 24-bit bitmap `height`
  // device pixels tall: through IViewObject::Draw, else PrintWindow.
  HBITMAP capture(int width, int height, void** bits, HDC& memory) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, bits, nullptr, 0);
    memory = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!bitmap || !memory) return bitmap;
    SelectObject(memory, bitmap);
    RECT all{0, 0, width, height};
    FillRect(memory, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    RECT client{};
    GetClientRect(host_, &client);
    bool drawn = false;
    if (auto document = browser_->document())
      if (auto view = document.as<IViewObject>(IID_IViewObject)) {
        RECTL bounds{0, 0, client.right, client.bottom};
        const HRESULT result = view->Draw(DVASPECT_CONTENT, -1, nullptr, nullptr, nullptr, memory, &bounds,
                                          nullptr, nullptr, 0);
        drawn = SUCCEEDED(result);
        if (!drawn) OutputDebugStringW(L"Geist BOO Reader: IViewObject::Draw failed; capturing the window\n");
      }
    // A renderer that answers Draw without drawing leaves the bitmap blank;
    // the window's own rendering (PrintWindow) is the fallback.
    auto blank = [&] {
      GdiFlush();
      const auto* bytes = static_cast<const BYTE*>(*bits);
      const std::size_t stride = (static_cast<std::size_t>(width) * 3 + 3) & ~std::size_t(3);
      for (std::size_t i = 0; i < stride * static_cast<std::size_t>(height); ++i)
        if (bytes[i] != 0xFF) return false;
      return true;
    };
    if (!drawn || blank()) {
      HWND view = browser_->window();
      // The innermost window holds the rendered document.
      HWND inner = view;
      for (HWND child = view ? GetWindow(view, GW_CHILD) : nullptr; child; child = GetWindow(child, GW_CHILD))
        inner = child;
      if (inner) PrintWindow(inner, memory, 2 /* PW_RENDERFULLCONTENT */);
    }
    GdiFlush();
    return bitmap;
  }

  bool print_topic_page(const PrintPage& page) {
    const TopicLayout& layout = layouts_[page.topic];
    if (page.index >= layout.slices.size()) return true;
    const PageSlice slice = layout.slices[page.index];
    double tallest = 1;
    for (const auto& s : layout.slices) tallest = std::max(tallest, s.bottom - s.top);
    const int width = static_cast<int>(std::lround(layout.layout_width * ratio_));
    const int height = static_cast<int>(std::lround(tallest * ratio_)) + 2;
    resize(width, height);
    // Shift the body so the slice starts at the top of the view.
    if (auto document = browser_->document()) {
      ComPtr<IHTMLElement> body;
      if (SUCCEEDED(document->get_body(body.put())) && body) {
        ComPtr<IHTMLStyle> style;
        if (SUCCEEDED(body->get_style(style.put())) && style) {
          Variant top(L"-" + std::to_wstring(static_cast<long>(std::lround(slice.top))) + L"px");
          style->put_top(*top);
        }
        long flush = 0;
        body->get_offsetHeight(&flush);
      }
    }
    const int slice_height = std::max(1, std::min(height, static_cast<int>(std::lround((slice.bottom - slice.top) * ratio_))));
    void* bits = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = capture(width, slice_height, &bits, memory);
    HDC dc = sink_->begin_page(layout.wide);
    bool ok = dc && bitmap && bits;
    if (ok) {
      const Geometry g = sink_->geometry(layout.wide);
      const double width_pt = layout.layout_width * layout.scale / kCssPerPoint;
      const double height_pt = (slice.bottom - slice.top) * layout.scale / kCssPerPoint;
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof info.bmiHeader;
      info.bmiHeader.biWidth = width;
      info.bmiHeader.biHeight = slice_height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 24;
      SetStretchBltMode(dc, HALFTONE);
      SetBrushOrgEx(dc, 0, 0, nullptr);
      StretchDIBits(dc, g.x(64), g.y(62), g.dx(width_pt), g.dy(height_pt), 0, 0, width, slice_height, bits, &info,
                    DIB_RGB_COLORS, SRCCOPY);
      draw_page_number(dc, g, page.number);
      ok = sink_->end_page();
    }
    if (memory) DeleteDC(memory);
    if (bitmap) DeleteObject(bitmap);
    if (!ok) finish(L"The printer did not accept a page.");
    return ok;
  }

  void finish(const std::wstring& error) {
    if (phase_ == Phase::finished) return;
    phase_ = Phase::finished;
    if (started_) sink_->abort();
    started_ = false;
    if (browser_) browser_->exec(OLECMDID_STOP);
    progress_.reset();
    if (!error.empty()) show_error(owner_, L"Cannot Print Book", error);
    auto finished = std::move(finished_);
    finished_ = nullptr;
    if (finished) finished();
  }

  HWND owner_;
  std::unique_ptr<PageSink> sink_;
  std::shared_ptr<BookSource> source_;
  std::function<void()> finished_;
  std::unique_ptr<ProgressDialog> progress_;
  HWND host_ = nullptr;
  const int host_origin_ = -32000; // off screen
  BrowserHost* browser_ = nullptr;
  Phase phase_ = Phase::measuring;
  std::vector<std::string> topics_;
  std::size_t index_ = 0;
  std::string loading_, loaded_;
  std::wstring loaded_url_;
  bool calibrated_ = false;
  bool started_ = false;
  double ratio_ = 1;
  std::map<std::string, TopicLayout> layouts_;
  PrintPlan plan_;
  std::vector<std::vector<std::size_t>> contents_pages_;
  std::vector<double> row_heights_;
};

std::unique_ptr<PageSink> choose_sink(HWND owner) {
  wchar_t directory[MAX_PATH] = L"";
  if (GetEnvironmentVariableW(L"GEIST_PRINT_TO_DIRECTORY", directory, MAX_PATH) > 0)
    return std::make_unique<DirectorySink>(directory);
  PRINTDLGEXW dialog{};
  dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = owner;
  dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
  dialog.nStartPage = START_PAGE_GENERAL;
  const HRESULT result = PrintDlgExW(&dialog);
  if (FAILED(result) || dialog.dwResultAction != PD_RESULT_PRINT || !dialog.hDC) {
    if (dialog.hDC) DeleteDC(dialog.hDC);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (FAILED(result)) show_error(owner, L"Cannot Print Book", L"No printer is available.");
    return nullptr;
  }
  return std::make_unique<PrinterSink>(dialog.hDC, dialog.hDevMode, dialog.hDevNames);
}

} // namespace

bool BookPrintJob::register_class(HINSTANCE instance) {
  WNDCLASSEXW window{};
  window.cbSize = sizeof window;
  window.lpfnWndProc = Job::host_proc;
  window.hInstance = instance;
  window.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  window.lpszClassName = kHostClass;
  return RegisterClassExW(&window) != 0;
}

std::unique_ptr<BookPrintJob> BookPrintJob::start(HWND owner, const std::filesystem::path& book,
                                                  const std::string& stylesheet, const std::string& host,
                                                  std::function<void()> finished) {
  auto sink = choose_sink(owner);
  if (!sink) return nullptr;
  auto source = std::make_shared<BookSource>(host);
  source->set_print_mode(true);
  source->set_stylesheet(stylesheet);
  source->set_head_extra(kMeasureScript);
  std::string error;
  if (!source->open(book, error)) {
    show_error(owner, L"Cannot Print Book", widen(error));
    return nullptr;
  }
  attach_source(source);
  auto job = std::make_unique<Job>(owner, std::move(sink), source, std::move(finished));
  if (!job->begin()) {
    show_error(owner, L"Cannot Print Book", L"The system web browser control (MSHTML) could not be started.");
    return nullptr;
  }
  return job;
}

} // namespace geist_reader
