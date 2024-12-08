/***
  This file is part of systemd.

  Copyright 2016 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <linux/netlink.h>
#include <sys/capability.h>
#include <sys/types.h>

#ifdef HAVE_SECCOMP
#include <seccomp.h>
#endif

#include "alloc-util.h"
#include "log.h"
#include "nspawn-seccomp.h"
#ifdef HAVE_SECCOMP
#include "seccomp-util.h"
#endif
#include "string-util.h"

#ifdef HAVE_SECCOMP

static int seccomp_add_default_syscall_filter(
                scmp_filter_ctx ctx,
                uint32_t arch,
                uint64_t cap_list_retain) {

        static const struct {
                uint64_t capability;
                int syscall_num;
        } blacklist[] = {
                { 0,              SCMP_SYS(_sysctl)             }, /* obsolete syscall */
                { 0,              SCMP_SYS(add_key)             }, /* keyring is not namespaced */
                { 0,              SCMP_SYS(afs_syscall)         }, /* obsolete syscall */
                { 0,              SCMP_SYS(bdflush)             },
#ifdef __NR_bpf
                { 0,              SCMP_SYS(bpf)                 },
#endif
                { 0,              SCMP_SYS(break)               }, /* obsolete syscall */
                { 0,              SCMP_SYS(create_module)       }, /* obsolete syscall */
                { 0,              SCMP_SYS(ftime)               }, /* obsolete syscall */
                { 0,              SCMP_SYS(get_kernel_syms)     }, /* obsolete syscall */
                { 0,              SCMP_SYS(getpmsg)             }, /* obsolete syscall */
                { 0,              SCMP_SYS(gtty)                }, /* obsolete syscall */
#ifdef __NR_kexec_file_load
                { 0,              SCMP_SYS(kexec_file_load)     },
#endif
                { 0,              SCMP_SYS(kexec_load)          },
                { 0,              SCMP_SYS(keyctl)              }, /* keyring is not namespaced */
                { 0,              SCMP_SYS(lock)                }, /* obsolete syscall */
                { 0,              SCMP_SYS(lookup_dcookie)      },
                { 0,              SCMP_SYS(mpx)                 }, /* obsolete syscall */
                { 0,              SCMP_SYS(nfsservctl)          }, /* obsolete syscall */
                { 0,              SCMP_SYS(open_by_handle_at)   },
                { 0,              SCMP_SYS(perf_event_open)     },
                { 0,              SCMP_SYS(prof)                }, /* obsolete syscall */
                { 0,              SCMP_SYS(profil)              }, /* obsolete syscall */
                { 0,              SCMP_SYS(putpmsg)             }, /* obsolete syscall */
                { 0,              SCMP_SYS(query_module)        }, /* obsolete syscall */
                { 0,              SCMP_SYS(quotactl)            },
                { 0,              SCMP_SYS(request_key)         }, /* keyring is not namespaced */
                { 0,              SCMP_SYS(security)            }, /* obsolete syscall */
                { 0,              SCMP_SYS(sgetmask)            }, /* obsolete syscall */
                { 0,              SCMP_SYS(ssetmask)            }, /* obsolete syscall */
                { 0,              SCMP_SYS(stty)                }, /* obsolete syscall */
                { 0,              SCMP_SYS(swapoff)             },
                { 0,              SCMP_SYS(swapon)              },
                { 0,              SCMP_SYS(sysfs)               }, /* obsolete syscall */
                { 0,              SCMP_SYS(tuxcall)             }, /* obsolete syscall */
                { 0,              SCMP_SYS(ulimit)              }, /* obsolete syscall */
                { 0,              SCMP_SYS(uselib)              }, /* obsolete syscall */
                { 0,              SCMP_SYS(ustat)               }, /* obsolete syscall */
                { 0,              SCMP_SYS(vserver)             }, /* obsolete syscall */
                { CAP_SYSLOG,     SCMP_SYS(syslog)              },
                { CAP_SYS_MODULE, SCMP_SYS(delete_module)       },
                { CAP_SYS_MODULE, SCMP_SYS(finit_module)        },
                { CAP_SYS_MODULE, SCMP_SYS(init_module)         },
                { CAP_SYS_PACCT,  SCMP_SYS(acct)                },
                { CAP_SYS_PTRACE, SCMP_SYS(process_vm_readv)    },
                { CAP_SYS_PTRACE, SCMP_SYS(process_vm_writev)   },
                { CAP_SYS_PTRACE, SCMP_SYS(ptrace)              },
                { CAP_SYS_RAWIO,  SCMP_SYS(ioperm)              },
                { CAP_SYS_RAWIO,  SCMP_SYS(iopl)                },
                { CAP_SYS_RAWIO,  SCMP_SYS(pciconfig_iobase)    },
                { CAP_SYS_RAWIO,  SCMP_SYS(pciconfig_read)      },
                { CAP_SYS_RAWIO,  SCMP_SYS(pciconfig_write)     },
#ifdef __NR_s390_pci_mmio_read
                { CAP_SYS_RAWIO,  SCMP_SYS(s390_pci_mmio_read)  },
#endif
#ifdef __NR_s390_pci_mmio_write
                { CAP_SYS_RAWIO,  SCMP_SYS(s390_pci_mmio_write) },
#endif
                { CAP_SYS_TIME,   SCMP_SYS(adjtimex)            },
                { CAP_SYS_TIME,   SCMP_SYS(clock_adjtime)       },
                { CAP_SYS_TIME,   SCMP_SYS(clock_settime)       },
                { CAP_SYS_TIME,   SCMP_SYS(settimeofday)        },
                { CAP_SYS_TIME,   SCMP_SYS(stime)               },
        };
        unsigned i;
        int r, c = 0;

        for (i = 0; i < ELEMENTSOF(blacklist); i++) {
                if (blacklist[i].capability != 0 && (cap_list_retain & (1ULL << blacklist[i].capability)))
                        continue;

                r = seccomp_rule_add_exact(ctx, SCMP_ACT_ERRNO(EPERM), blacklist[i].syscall_num, 0);
                if (r < 0) {
                        /* If the system call is not known on this architecture, then that's fine, let's ignore it */
                        _cleanup_free_ char *n = NULL;

                        n = seccomp_syscall_resolve_num_arch(arch, blacklist[i].syscall_num);
                        log_debug_errno(r, "Failed to add rule for system call %s, ignoring: %m", strna(n));
                } else
                        c++;
        }

        return c;
}

int setup_seccomp(uint64_t cap_list_retain) {
        uint32_t arch;
        int r;

        if (!is_seccomp_available()) {
                log_debug("SECCOMP features not detected in the kernel, disabling SECCOMP audit filter");
                return 0;
        }

        SECCOMP_FOREACH_LOCAL_ARCH(arch) {
                _cleanup_(seccomp_releasep) scmp_filter_ctx seccomp = NULL;
                int n;

                log_debug("Operating on architecture: %s", seccomp_arch_to_string(arch));

                r = seccomp_init_for_arch(&seccomp, arch, SCMP_ACT_ALLOW);
                if (r < 0)
                        return log_error_errno(r, "Failed to allocate seccomp object: %m");

                n = seccomp_add_default_syscall_filter(seccomp, arch, cap_list_retain);
                if (n < 0)
                        return n;

                /*
                  Audit is broken in containers, much of the userspace audit hookup will fail if running inside a
                  container. We don't care and just turn off creation of audit sockets.

                  This will make socket(AF_NETLINK, *, NETLINK_AUDIT) fail with EAFNOSUPPORT which audit userspace uses
                  as indication that audit is disabled in the kernel.
                */

                r = seccomp_rule_add_exact(
                                seccomp,
                                SCMP_ACT_ERRNO(EAFNOSUPPORT),
                                SCMP_SYS(socket),
                                2,
                                SCMP_A0(SCMP_CMP_EQ, AF_NETLINK),
                                SCMP_A2(SCMP_CMP_EQ, NETLINK_AUDIT));
                if (r < 0)
                        log_debug_errno(r, "Failed to add audit seccomp rule, ignoring: %m");
                else
                        n++;

                if (n <= 0) /* no rule added? then skip this architecture */
                        continue;

                r = seccomp_load(seccomp);
                if (IN_SET(r, -EPERM, -EACCES))
                        return log_error_errno(r, "Failed to install seccomp audit filter: %m");
                if (r < 0)
                        log_debug_errno(r, "Failed to install filter set for architecture %s, skipping: %m", seccomp_arch_to_string(arch));
        }

        return 0;
}

#else

int setup_seccomp(uint64_t cap_list_retain) {
        return 0;
}

#endif
