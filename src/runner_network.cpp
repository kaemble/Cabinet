#include "runner_network.h"
#ifdef __linux__
#include <cerrno>
#include <cstddef>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <system_error>
#include <unistd.h>
#endif

void restrict_runner_network()
{
#ifdef __linux__
#if defined(__x86_64__) && !defined(__ILP32__)
    constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#else
#error Runner network filtering requires a supported Linux syscall architecture
#endif
    sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#ifdef __x86_64__
        // x32 shares the audit architecture but uses different syscall numbers.
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
        // io_uring can create sockets without passing through the socket syscall.
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_io_uring_setup, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_io_uring_enter, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_io_uring_register, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socketpair, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])),
        // UNIX sockets serve display/audio; netlink serves local device discovery.
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_NETLINK, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
    {
        throw std::system_error(errno, std::generic_category(), "Cannot restrict runner privileges");
    }
    const auto result = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &program);
    if (result != 0)
    {
        throw std::system_error(result == -1 ? errno : EBUSY, std::generic_category(),
                                "Cannot restrict runner networking");
    }
#endif
}
