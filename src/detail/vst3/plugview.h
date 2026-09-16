#pragma once

/*
    Copyright (c) 2022 Timo Kaluza (defiantnerd)

    This file is part of the clap-wrappers project which is released under MIT License.
    See file LICENSE or go to https://github.com/free-audio/clap-wrapper for full license details.

*/

#include "base/source/fobject.h"
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <clap/clap.h>
#include <functional>

// VST3 3.8 WaylandSurfaceID embedding (see include/clapwrapper/wayland.h).
// Opt-in: define CLAP_WRAPPER_WAYLAND_EMBED=1 on the vst3 wrapper lib. Off by
// default so the wrapper builds unchanged against pre-3.8 SDKs.
#if LIN && CLAP_WRAPPER_WAYLAND_EMBED
#define CLAP_WRAPPER_VST3_WAYLAND 1
#include <pluginterfaces/gui/iwaylandframe.h>
#include "clapwrapper/wayland.h"
#else
#define CLAP_WRAPPER_VST3_WAYLAND 0
#endif

using namespace Steinberg;

class WrappedView : public Steinberg::IPlugView,
                    public Steinberg::IPlugViewContentScaleSupport,
                    public Steinberg::FObject
{
 public:
  WrappedView(const clap_plugin_t *plugin, const clap_plugin_gui_t *gui,
              std::function<void()> onReleaseAdditionalReferences, std::function<void(bool)> onDestroy,
              std::function<void()> onRunLoopAvailable);
  ~WrappedView();

  // IPlugView interface
  tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override;

  /** The parent window of the view has been created, the (platform) representation of the view
		should now be created as well.
			Note that the parent is owned by the caller and you are not allowed to alter it in any way
		other than adding your own views.
			Note that in this call the plug-in could call a IPlugFrame::resizeView ()!
			\param parent : platform handle of the parent window or view
			\param type : \ref platformUIType which should be created */
  tresult PLUGIN_API attached(void *parent, FIDString type) override;

  /** The parent window of the view is about to be destroyed.
			You have to remove all your own views from the parent window or view. */
  tresult PLUGIN_API removed() override;

  /** Handling of mouse wheel. */
  tresult PLUGIN_API onWheel(float distance) override;

  /** Handling of keyboard events : Key Down.
			\param key : unicode code of key
			\param keyCode : virtual keycode for non ascii keys - see \ref VirtualKeyCodes in keycodes.h
			\param modifiers : any combination of modifiers - see \ref KeyModifier in keycodes.h
			\return kResultTrue if the key is handled, otherwise kResultFalse. \n
							<b> Please note that kResultTrue must only be returned if the key has really been
		 handled. </b> Otherwise key command handling of the host might be blocked! */
  tresult PLUGIN_API onKeyDown(char16 key, int16 keyCode, int16 modifiers) override;

  /** Handling of keyboard events : Key Up.
			\param key : unicode code of key
			\param keyCode : virtual keycode for non ascii keys - see \ref VirtualKeyCodes in keycodes.h
			\param modifiers : any combination of KeyModifier - see \ref KeyModifier in keycodes.h
			\return kResultTrue if the key is handled, otherwise return kResultFalse. */
  tresult PLUGIN_API onKeyUp(char16 key, int16 keyCode, int16 modifiers) override;

  /** Returns the size of the platform representation of the view. */
  tresult PLUGIN_API getSize(ViewRect *size) override;

  /** Resizes the platform representation of the view to the given rect. Note that if the plug-in
	 *	requests a resize (IPlugFrame::resizeView ()) onSize has to be called afterward. */
  tresult PLUGIN_API onSize(ViewRect *newSize) override;

  /** Focus changed message. */
  tresult PLUGIN_API onFocus(TBool state) override;

  /** Sets IPlugFrame object to allow the plug-in to inform the host about resizing. */
  tresult PLUGIN_API setFrame(IPlugFrame *frame) override;

  /** Is view sizable by user. */
  tresult PLUGIN_API canResize() override;

  /** On live resize this is called to check if the view can be resized to the given rect, if not
	 *	adjust the rect to the allowed size. */
  tresult PLUGIN_API checkSizeConstraint(ViewRect *rect) override;

  tresult setContentScaleFactor(ScaleFactor factor) override;

  //---Interface------
  OBJ_METHODS(WrappedView, FObject)
  DEFINE_INTERFACES
  DEF_INTERFACE(IPlugView)
  DEF_INTERFACE(IPlugViewContentScaleSupport)
  END_DEFINE_INTERFACES(FObject)
  REFCOUNT_METHODS(FObject)

  // wrapper needed interfaces
  bool request_resize(uint32_t width, uint32_t height);

 private:
  // The CLAP window api the platform attaches with (X11/Win32/Cocoa).
  static const char *platform_api();
  // The api the GUI is created with before the host has attached: Wayland
  // when this wrapper can serve it, otherwise the platform api.
  const char *preferred_api();
  void ensure_ui();
  void ensure_ui(const char *api);
  void destroy_ui();
  void drop_ui();
  void releaseAdditionalReferences();
  void apply_attached_size();
  const clap_plugin_t *_plugin = nullptr;
  const clap_plugin_gui_t *_extgui = nullptr;
  std::function<void()> _onReleaseAdditionalReferences = nullptr, _onRunLoopAvailable = nullptr;
  std::function<void(bool)> _onDestroy = nullptr;
  clap_window_t _window = {nullptr, {nullptr}};
  IPlugFrame *_plugFrame = nullptr;
  ViewRect _rect = {0, 0, 0, 0};
  bool _created = false;
  bool _createOk = false;
  bool _everCreated = false;  // _onDestroy(true) owes the wrapper a run-loop detach
  const char *_createdApi = nullptr;
  bool _attached = false;
  // Geometry policy on attach: a host-chosen onSize pins the size and is
  // re-applied after set_parent; otherwise the plugin's post-parent size wins
  // and the frame is told. Only an onSize delivered synchronously inside our
  // own resizeView call is recognised as an echo; an asynchronous echo (a
  // compositor configure after resizeView returned) is indistinguishable from
  // a host choice and pins the size for the life of this view.
  bool _explicitSize = false;
  bool _inRequestResize = false;  // inside our resizeView: onSize is an echo
  bool _reportCachedSize = false; // inside the attach-time resizeView: getSize
                                  // answers the pre-attach rect so a host that
                                  // compares getSize against the request sees
                                  // a real change and resizes

#if LIN
 public:
  Steinberg::Linux::IRunLoop *getRunLoop()
  {
    return _runLoop;
  }

 private:
  Steinberg::Linux::IRunLoop *_runLoop = nullptr;
#endif

#if CLAP_WRAPPER_VST3_WAYLAND
 public:
  // Owned by the wrapper (ClapAsVst3) and outlives the view. nullptr when the
  // host offers no VST3 3.8 IWaylandHost.
  void setWaylandHost(Steinberg::IWaylandHost *host)
  {
    _waylandHost = host;
  }
  // The wrapper's CLAP_HOST_WAYLAND_EMBED answer for an instance whose VST3
  // host provided `host`; nullptr for any other id or without IWaylandHost.
  static const void *waylandHostExtension(Steinberg::IWaylandHost *host, const char *extension);

 private:
  Steinberg::IWaylandHost *_waylandHost = nullptr;
  Steinberg::IPtr<Steinberg::IWaylandFrame> _waylandFrame;
  wl_display *_wlDisplay = nullptr;
  clap_wayland_embed_t _waylandEmbed{};
  bool attachWayland(void *parent);
  void detachWayland();
#endif
};
