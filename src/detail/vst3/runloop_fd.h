#pragma once

#include <clap/clap.h>

/*
 * What Steinberg::Linux::IRunLoop can do for clap_host_posix_fd_support.
 *
 * registerEventHandler takes a descriptor and nothing else, and the host calls
 * onFDIsSet "when the file descriptor is marked readable", with no revents.
 * Readability is therefore the one interest the wrapper can serve:
 *
 * - register_fd and modify_fd refuse WRITE rather than accept it and never
 *   signal it, so the plugin knows to cover writability some other way (its
 *   own timer, say) instead of waiting for a wakeup that cannot come;
 * - a wakeup reports READ, and only to a registration that asked for it.
 *   ERROR is a claim about failure, which the run loop never makes: replaying
 *   the registration mask whole would tell a plugin that asked to hear about
 *   errors (as a Wayland client must, so a compositor hangup is not polled
 *   forever) that its connection had died on the first readable byte.
 */
inline bool clap_wrapper_runloop_fd_supported(clap_posix_fd_flags_t flags)
{
  return (flags & CLAP_POSIX_FD_WRITE) == 0;
}

inline clap_posix_fd_flags_t clap_wrapper_runloop_fd_events(clap_posix_fd_flags_t registered)
{
  return registered & CLAP_POSIX_FD_READ;
}
