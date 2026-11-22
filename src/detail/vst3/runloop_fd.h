#pragma once

#include <clap/clap.h>

/*
 * What Steinberg::Linux::IRunLoop can do for clap_host_posix_fd_support.
 *
 * registerEventHandler takes a descriptor and nothing else, and the host calls
 * onFDIsSet "when the file descriptor is marked readable", with no revents.
 * Readability is therefore the one interest the wrapper can serve:
 *
 * - register_fd and modify_fd accept a registration only if it asks for READ
 *   and not for WRITE. Anything else would be accepted and never signalled:
 *   WRITE because the run loop cannot watch it, an ERROR-only registration
 *   because the run loop never reports failure. The refusal is the whole
 *   registration, so a plugin that needs WRITE must register READ alone and
 *   cover writability some other way (its own timer, say). The standalone
 *   host polls its descriptors itself and does serve WRITE.
 * - a wakeup reports READ, from the registration in force when the run loop
 *   fires (modify_fd may have changed it since). ERROR is a claim about
 *   failure, which the run loop never makes: replaying the registration mask
 *   whole would tell a plugin that asked to hear about errors (as a Wayland
 *   client must, so a compositor hangup is not polled forever) that its
 *   connection had died on the first readable byte.
 */
inline bool clap_wrapper_runloop_fd_supported(clap_posix_fd_flags_t flags)
{
  return (flags & CLAP_POSIX_FD_READ) != 0 && (flags & CLAP_POSIX_FD_WRITE) == 0;
}

inline clap_posix_fd_flags_t clap_wrapper_runloop_fd_events(clap_posix_fd_flags_t registered)
{
  return registered & CLAP_POSIX_FD_READ;
}

// The events on_fd gets when the run loop reports `fd` readable: from the
// registration for `fd` in `registrations` (anything with `fd` and `flags`
// members), or 0 -- report nothing -- when there is none or it wants no READ.
template <typename Registrations>
clap_posix_fd_flags_t clap_wrapper_runloop_fd_wakeup(const Registrations &registrations, int fd)
{
  for (const auto &r : registrations)
  {
    if (r.fd == fd) return clap_wrapper_runloop_fd_events(r.flags);
  }
  return 0;
}
