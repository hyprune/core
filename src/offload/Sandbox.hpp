#pragma once
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace hyprune::offload {
// Applied before driver initialization, parsing the world or accepting
// frame requests. Existing private sockets/DRM FDs stay usable; no new IPC peers.
inline void sandbox(const std::filesystem::path &package) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        throw std::runtime_error("no-new-privileges");
    const int abi = syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 3)
        throw std::runtime_error("Landlock ABI 3 required for offload");
    constexpr uint64_t read = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_EXECUTE;
    constexpr uint64_t write = LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_TRUNCATE;
    landlock_ruleset_attr rules{};
    rules.handled_access_fs = read | write | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
                              LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                              LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO |
                              LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER;
    int fd = syscall(SYS_landlock_create_ruleset, &rules, sizeof(rules), 0);
    if (fd < 0)
        throw std::runtime_error("Landlock ruleset");
    auto allow = [&](const std::filesystem::path &path, bool writable = false) {
        int p = open(path.c_str(), O_PATH | O_CLOEXEC);
        if (p < 0)
            return;
        struct stat st{};
        fstat(p, &st);
        landlock_path_beneath_attr entry{};
        entry.parent_fd = p;
        entry.allowed_access =
            (S_ISDIR(st.st_mode) ? read : read & ~LANDLOCK_ACCESS_FS_READ_DIR) | (writable ? write : 0);
        int result = syscall(SYS_landlock_add_rule, fd, LANDLOCK_RULE_PATH_BENEATH, &entry, 0);
        close(p);
        if (result) {
            close(fd);
            throw std::runtime_error("Landlock path rule");
        }
    };
    for (auto path : {"/usr", "/lib", "/lib64", "/etc/ld.so.cache", "/etc/fonts", "/etc/vulkan", "/etc/glvnd",
                      "/etc/egl", "/var/cache/fontconfig", "/sys", "/proc/self", "/proc/driver/nvidia", "/proc/sys"})
        allow(path);
    allow(std::filesystem::canonical(package));
    allow("/dev/dri", true);
    for (auto &entry : std::filesystem::directory_iterator("/dev"))
        if (entry.path().filename().string().starts_with("nvidia"))
            allow(entry.path(), true);
    allow("/dev/null", true);
    allow("/dev/urandom");
    allow("/dev/random");
    int result = syscall(SYS_landlock_restrict_self, fd, 0);
    close(fd);
    if (result)
        throw std::runtime_error("Landlock restrict");
    sock_filter filter[] = {BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
                            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
                            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
                            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
                            BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
                            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#define DENY_SYSCALL(n)                                                                                                \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, n, 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)
                            DENY_SYSCALL(SYS_connect),
                            DENY_SYSCALL(SYS_bind),
                            DENY_SYSCALL(SYS_listen),
                            DENY_SYSCALL(SYS_accept),
                            DENY_SYSCALL(SYS_accept4),
                            DENY_SYSCALL(SYS_execve),
                            DENY_SYSCALL(SYS_execveat),
                            DENY_SYSCALL(SYS_ptrace),
                            DENY_SYSCALL(SYS_process_vm_readv),
                            DENY_SYSCALL(SYS_process_vm_writev),
                            DENY_SYSCALL(SYS_pidfd_getfd),
#undef DENY_SYSCALL
                            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter};
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program))
        throw std::runtime_error("renderer seccomp");
}
} // namespace hyprune::offload
