#include "plugview.h"
#include <clap/clap.h>
#include <cassert>
#include <cstring>
#include <iostream>

#if CLAP_WRAPPER_VST3_WAYLAND
// The SDK ships only the declarations of the 3.8 Wayland interfaces; every
// user defines the IIDs once (as the SDK's own editorhost sample does).
DEF_CLASS_IID(Steinberg::IWaylandHost)
DEF_CLASS_IID(Steinberg::IWaylandFrame)
#endif

WrappedView::WrappedView(const clap_plugin_t *plugin, const clap_plugin_gui_t *gui,
                         std::function<void()> onReleaseAdditionalReferences,
                         std::function<void(bool)> onDestroy, std::function<void()> onRunLoopAvailable)
  : IPlugView()
  , FObject()
  , _plugin(plugin)
  , _extgui(gui)
  , _onReleaseAdditionalReferences(onReleaseAdditionalReferences)
  , _onRunLoopAvailable(onRunLoopAvailable)
  , _onDestroy(onDestroy)
{
}

WrappedView::~WrappedView()
{
  drop_ui();
}

const char *WrappedView::platform_api()
{
#if MAC
  return CLAP_WINDOW_API_COCOA;
#elif WIN
  return CLAP_WINDOW_API_WIN32;
#else
  return CLAP_WINDOW_API_X11;
#endif
}

const char *WrappedView::preferred_api()
{
#if CLAP_WRAPPER_VST3_WAYLAND
  if (_waylandHost && _extgui->is_api_supported(_plugin, CLAP_WINDOW_API_WAYLAND, false))
    return CLAP_WINDOW_API_WAYLAND;
#endif
  return platform_api();
}

void WrappedView::ensure_ui()
{
  ensure_ui(_createdApi ? _createdApi : preferred_api());
}

void WrappedView::ensure_ui(const char *api)
{
  // A GUI created for one transport (e.g. a pre-attach getSize on X11) is
  // recreated when the host attaches with another.
  if (_created && _createdApi && strcmp(_createdApi, api) != 0)
  {
    destroy_ui();
  }
  if (!_created)
  {
    _createdApi = api;
    _createOk = false;
    if (_extgui->is_api_supported(_plugin, api, false))
    {
      _createOk = _extgui->create(_plugin, api, false);
    }
    _created = true;
    _everCreated = true;
  }
}

// Destroys the plugin GUI but keeps this view alive for a later attach.
void WrappedView::destroy_ui()
{
  if (!_created) return;
  releaseAdditionalReferences();
  if (_createOk)
  {
    if (_attached) _extgui->hide(_plugin);
    _extgui->destroy(_plugin);
  }
  _attached = false;
  _created = false;
  _createOk = false;
  _createdApi = nullptr;
  _window.ptr = nullptr;
}

void WrappedView::drop_ui()
{
  destroy_ui();
  if (_onDestroy)
  {
    // true: the wrapper attached its run-loop handlers for this view and must
    // detach them (independent of whether the GUI is still alive right now).
    _onDestroy(_everCreated);
  }
#if CLAP_WRAPPER_VST3_WAYLAND
  detachWayland();
#endif
}

void WrappedView::releaseAdditionalReferences()
{
  // releases things like IContextMenu when the wrapper provided entries, but the user chose
  // a plugin-owned entry.

  if (_onReleaseAdditionalReferences)
  {
    _onReleaseAdditionalReferences();
  }
}

tresult PLUGIN_API WrappedView::isPlatformTypeSupported(FIDString type)
{
  static struct vst3_and_clap_match_types_t
  {
    const char *VST3;
    const char *CLAP;
  } platformTypeMatches[] = {{kPlatformTypeHWND, CLAP_WINDOW_API_WIN32},
                             {kPlatformTypeNSView, CLAP_WINDOW_API_COCOA},
                             {kPlatformTypeX11EmbedWindowID, CLAP_WINDOW_API_X11},
#if CLAP_WRAPPER_VST3_WAYLAND
                             {kPlatformTypeWaylandSurfaceID, CLAP_WINDOW_API_WAYLAND},
#endif
                             {nullptr, nullptr}};
  auto *n = platformTypeMatches;
  while (n->VST3 && n->CLAP)
  {
    if (!strcmp(type, n->VST3))
    {
#if CLAP_WRAPPER_VST3_WAYLAND
      // Wayland embedding needs the host's IWaylandHost connection service.
      if (!strcmp(n->CLAP, CLAP_WINDOW_API_WAYLAND) && !_waylandHost) return kResultFalse;
#endif
      if (_extgui->is_api_supported(_plugin, n->CLAP, false))
      {
        return kResultOk;
      }
    }
    ++n;
  }

  return kResultFalse;
}

tresult PLUGIN_API WrappedView::attached(void *parent, FIDString type)
{
  const char *api = platform_api();
  void *ptr = parent;
  bool wayland = false;
  if (type && !strcmp(type, kPlatformTypeWaylandSurfaceID))
  {
#if CLAP_WRAPPER_VST3_WAYLAND
    // VST3 3.8: the parent surface is reached on a host-served connection
    // (IWaylandHost); IWaylandFrame supplies the connection-local proxy, with
    // the raw `parent` argument as the documented fallback.
    if (!attachWayland(parent)) return kResultFalse;
    api = CLAP_WINDOW_API_WAYLAND;
    ptr = &_waylandEmbed;
    wayland = true;
#else
    return kResultFalse;
#endif
  }

  // ensure_ui() may destroy a GUI created for another transport, and
  // destroy_ui() clears _window.ptr -- so create first, then publish the
  // parent payload, or set_parent() would be handed a null pointer.
  ensure_ui(api);
  _window = {api, {ptr}};
  bool ok = _createOk && _extgui->set_parent(_plugin, &_window);
  if (ok)
  {
    _attached = true;
    apply_attached_size();
    // Only the Wayland path treats a failed show as fatal: there it means the
    // plugin could not secure a host timer/fd to drive its connection.
    if (!_extgui->show(_plugin) && wayland) ok = false;
  }
  if (!ok)
  {
    _window.ptr = nullptr;
#if CLAP_WRAPPER_VST3_WAYLAND
    if (wayland)
    {
      destroy_ui();
      detachWayland();
    }
#endif
    _attached = false;
    return kResultFalse;
  }
  return kResultOk;
}

// Geometry after set_parent. A host-supplied onSize is authoritative and is
// re-applied; otherwise the plugin's post-parent size wins (its realize step
// may have rescaled for the parent monitor's DPI, e.g. 1280 -> 1920 on a
// mixed-DPI Win32 desktop) and the frame is told through resizeView.
void WrappedView::apply_attached_size()
{
  if (_explicitSize)
  {
    if (_extgui->can_resize(_plugin))
    {
      uint32_t w = _rect.getWidth();
      uint32_t h = _rect.getHeight();
      if (_extgui->adjust_size(_plugin, &w, &h))
      {
        _rect.right = _rect.left + w + 1;
        _rect.bottom = _rect.top + h + 1;
      }
      _extgui->set_size(_plugin, w, h);
    }
    return;
  }
  uint32_t w, h;
  if (!_extgui->get_size(_plugin, &w, &h)) return;
  if ((int32)w == _rect.getWidth() && (int32)h == _rect.getHeight()) return;
  ViewRect fresh = _rect;
  fresh.right = fresh.left + (int32)w;
  fresh.bottom = fresh.top + (int32)h;
  if (_plugFrame)
  {
    // Hosts following the SDK reference (editorhost) compare getSize against
    // the requested rect and skip the window resize when they already match;
    // keep reporting the pre-attach rect until the request has been made.
    _inRequestResize = true;
    _reportCachedSize = true;
    const bool ok = _plugFrame->resizeView(this, &fresh) == kResultOk;
    _reportCachedSize = false;
    _inRequestResize = false;
    // A refused request must not be recorded as the size in force: _rect is
    // what the next attach compares against, so keeping `fresh` here would
    // make the retry look unnecessary and strand the host window.
    if (!ok) return;
  }
  _rect = fresh;
}

tresult PLUGIN_API WrappedView::removed()
{
  releaseAdditionalReferences();
#if CLAP_WRAPPER_VST3_WAYLAND
  if (_wlDisplay)
  {
    // The connection closes with the attachment; nothing can outlive it.
    destroy_ui();  // hides while still attached, then destroys
    detachWayland();
  }
#endif
  _attached = false;
  _window.ptr = nullptr;
  return kResultOk;
}

#if CLAP_WRAPPER_VST3_WAYLAND
const void *WrappedView::waylandHostExtension(Steinberg::IWaylandHost *host, const char *extension)
{
  static const clap_host_wayland_embed_t ext = {sizeof(clap_host_wayland_embed_t)};
  if (host && extension && !strcmp(extension, CLAP_HOST_WAYLAND_EMBED)) return &ext;
  return nullptr;
}

bool WrappedView::attachWayland(void *parent)
{
  if (!_waylandHost || _wlDisplay) return false;
  _wlDisplay = _waylandHost->openWaylandConnection();
  if (!_wlDisplay) return false;
  Steinberg::IWaylandFrame *frame = nullptr;
  if (_plugFrame &&
      _plugFrame->queryInterface(Steinberg::IWaylandFrame::iid, (void **)&frame) == kResultOk && frame)
  {
    _waylandFrame = Steinberg::owned(frame);
  }
  wl_surface *parentSurface = _waylandFrame ? _waylandFrame->getWaylandSurface(_wlDisplay) : nullptr;
  if (!parentSurface) parentSurface = static_cast<wl_surface *>(parent);
  if (!parentSurface)
  {
    detachWayland();
    return false;
  }
  _waylandEmbed = clap_wayland_embed_t{};
  _waylandEmbed.size = sizeof(_waylandEmbed);
  _waylandEmbed.display = _wlDisplay;
  _waylandEmbed.parent = parentSurface;
  return true;
}

void WrappedView::detachWayland()
{
  _waylandEmbed = clap_wayland_embed_t{};
  _waylandFrame = nullptr;
  if (_wlDisplay)
  {
    if (_waylandHost) _waylandHost->closeWaylandConnection(_wlDisplay);
    _wlDisplay = nullptr;
  }
}
#endif

tresult PLUGIN_API WrappedView::onWheel(float /*distance*/)
{
  return kResultFalse;
}

tresult PLUGIN_API WrappedView::onKeyDown(char16 /*key*/, int16 /*keyCode*/, int16 /*modifiers*/)
{
  return kResultFalse;
}

tresult PLUGIN_API WrappedView::onKeyUp(char16 /*key*/, int16 /*keyCode*/, int16 /*modifiers*/)
{
  return kResultFalse;
}

tresult PLUGIN_API WrappedView::getSize(ViewRect *size)
{
  if (!size) return kInvalidArgument;
  if (_reportCachedSize)
  {
    *size = _rect;
    return kResultOk;
  }
  ensure_ui();
  uint32_t w, h;
  if (_extgui->get_size(_plugin, &w, &h))
  {
    size->right = size->left + w;
    size->bottom = size->top + h;
    _rect = *size;
    return kResultOk;
  }
  return kResultFalse;
}

tresult PLUGIN_API WrappedView::onSize(ViewRect *newSize)
{
  // TODO: discussion took place if this call should be ignored completely
  // since it seems not to match the CLAP UI scheme.
  // for now, it is left in and might be removed in the future.
  if (!newSize) return kResultFalse;

  _rect = *newSize;
  if (!_inRequestResize) _explicitSize = true;
  if (_created && _attached)
  {
    if (_extgui->can_resize(_plugin))
    {
      uint32_t w = _rect.getWidth();
      uint32_t h = _rect.getHeight();
      if (_extgui->adjust_size(_plugin, &w, &h))
      {
        _rect.right = _rect.left + w;
        _rect.bottom = _rect.top + h;
      }
      if (_extgui->set_size(_plugin, w, h))
      {
        return kResultOk;
      }
    }
    else
    {
      return kResultFalse;
    }
  }
  return kResultOk;
}

tresult PLUGIN_API WrappedView::onFocus(TBool state)
{
  // TODO: this might be something for the wrapperhost API
  // to notify the plugin about a focus change
  if (state == false)
  {
    releaseAdditionalReferences();
  }
  return kResultOk;
}

tresult PLUGIN_API WrappedView::setFrame(IPlugFrame *frame)
{
  releaseAdditionalReferences();

  _plugFrame = frame;

#if LIN
  if (_plugFrame)
  {
    if (_plugFrame->queryInterface(Steinberg::Linux::IRunLoop::iid, (void **)&_runLoop) ==
            Steinberg::kResultOk &&
        _onRunLoopAvailable)
    {
      _onRunLoopAvailable();
    }
  }
#endif
  return kResultOk;
}

tresult PLUGIN_API WrappedView::canResize()
{
  ensure_ui();
  return _extgui->can_resize(_plugin) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API WrappedView::checkSizeConstraint(ViewRect *rect)
{
  ensure_ui();
  uint32_t w = rect->getWidth();
  uint32_t h = rect->getHeight();
  if (_extgui->adjust_size(_plugin, &w, &h))
  {
    rect->right = rect->left + w;
    rect->bottom = rect->top + h;
    return kResultOk;
  }
  return kResultFalse;
}

bool WrappedView::request_resize(uint32_t width, uint32_t height)
{
  // Request through a copy: hosts call getSize() from inside resizeView and
  // getSize writes _rect, so passing &_rect would let that call overwrite the
  // request it is being compared against.
  ViewRect req = _rect;
  req.right = req.left + (int32)width;
  req.bottom = req.top + (int32)height;

  if (_plugFrame)
  {
    _inRequestResize = true;
    const bool ok = _plugFrame->resizeView(this, &req) == kResultOk;
    _inRequestResize = false;
    if (!ok) return false;
  }
  _rect = req;
  return true;
}
tresult WrappedView::setContentScaleFactor(IPlugViewContentScaleSupport::ScaleFactor factor)
{
  ensure_ui();
  if (_extgui->set_scale(_plugin, factor))
  {
    return kResultOk;
  }
  return kResultFalse;
}
