#pragma once

#include <clap/clap.h>

/*
 * Translating Steinberg::Linux::IRunLoop file-descriptor readiness into the
 * flags of clap_plugin_posix_fd_support::on_fd.
 *
 * registerEventHandler takes a descriptor and nothing else, and onFDIsSet
 * carries no revents: the run loop says only "this descriptor is ready". The
 * registration mask is therefore all the wrapper knows, and replaying it
 * whole would report CLAP_POSIX_FD_ERROR on every ordinary wakeup -- a plugin
 * that asked to hear about errors (as a Wayland client must, so a compositor
 * hangup is not polled forever) would be told its connection had died on the
 * first readable byte. READ and WRITE are claims about readiness, which is
 * exactly what the run loop reported; ERROR is a claim about failure, which it
 * never reports, so it is the one bit that must be dropped.
 */
inline clap_posix_fd_flags_t clap_wrapper_runloop_fd_events(clap_posix_fd_flags_t registered)
{
  return registered & ~(clap_posix_fd_flags_t)CLAP_POSIX_FD_ERROR;
}
