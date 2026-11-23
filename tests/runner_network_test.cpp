#include "runner_network.h"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <exception>
#include <iostream>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <linux/seccomp.h>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

static void require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}

static void check_network()
{
    for (int family : {AF_INET, AF_INET6, AF_PACKET, AF_VSOCK})
    {
        for (int type : {SOCK_STREAM, SOCK_DGRAM, SOCK_RAW})
        {
            errno = 0;
            require(syscall(SYS_socket, family, type, 0) == -1 && errno == EPERM,
                    "Network socket creation was not denied");
        }
        int pair[2];
        errno = 0;
        require(socketpair(family, SOCK_STREAM, 0, pair) == -1 && errno == EPERM,
                "Network socketpair creation was not denied");
    }
    for (long number : {SYS_io_uring_setup, SYS_io_uring_enter, SYS_io_uring_register})
    {
        errno = 0;
        require(syscall(number, 0, 0, 0, 0, 0, 0) == -1 && errno == ENOSYS,
                "io_uring was not disabled");
    }
    require(prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1, "no_new_privs was not set");
}

static void check_local_sockets(int netlink_error)
{
    int pair[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Local socketpair creation failed");
    char sent = 'x', received = 0;
    require(write(pair[0], &sent, 1) == 1 && read(pair[1], &received, 1) == 1 && received == sent,
            "Local socket communication failed");
    close(pair[0]);
    close(pair[1]);
    int local = socket(AF_UNIX, SOCK_DGRAM, 0);
    require(local >= 0, "Local socket creation failed");
    close(local);
    int netlink = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    require((netlink >= 0 ? 0 : errno) == netlink_error, "Filter changed local netlink socket access");
    if (netlink >= 0)
    {
        close(netlink);
    }
}

static void check_install_failure()
{
    pid_t child = fork();
    require(child >= 0, "fork failed");
    if (child == 0)
    {
        sock_filter filter[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_seccomp, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter};
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1 ||
            prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == -1)
        {
            _exit(2);
        }
        try
        {
            restrict_runner_network();
        }
        catch (const std::system_error& error)
        {
            _exit(error.code().value() == EPERM ? 0 : 3);
        }
        _exit(1);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "Denied filter installation did not throw");
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--inherited")
        {
            check_network();
            return 0;
        }
        int netlink = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        const int netlink_error = netlink >= 0 ? 0 : errno;
        if (netlink >= 0)
        {
            close(netlink);
        }
        const rlimit core_limit{0, 0};
        require(setrlimit(RLIMIT_CORE, &core_limit) == 0, "Cannot disable core dumps for ABI rejection test");
        check_install_failure();
        std::atomic<bool> installed = false;
        std::exception_ptr worker_failure;
        std::jthread worker([&](std::stop_token stop)
        {
            while (!installed && !stop.stop_requested())
            {
                std::this_thread::yield();
            }
            if (!installed)
            {
                return;
            }
            try
            {
                check_network();
            }
            catch (...)
            {
                worker_failure = std::current_exception();
            }
        });
        restrict_runner_network();
        installed = true;
        worker.join();
        if (worker_failure)
        {
            std::rethrow_exception(worker_failure);
        }
        check_network();
        check_local_sockets(netlink_error);
        std::thread child_thread(check_network);
        child_thread.join();

        pid_t child = fork();
        require(child >= 0, "fork failed");
        if (child == 0)
        {
            execl("/proc/self/exe", argv[0], "--inherited", nullptr);
            _exit(127);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Filter did not survive fork and exec");
#ifdef __x86_64__
        child = fork();
        require(child >= 0, "fork failed");
        if (child == 0)
        {
            syscall(SYS_getpid | 0x40000000U);
            _exit(1);
        }
        require(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS,
                "Alternate x32 syscall ABI was not rejected");
#endif
        std::cout << "Network sockets blocked; local sockets and filter inheritance passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
