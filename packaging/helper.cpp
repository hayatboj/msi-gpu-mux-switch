// The only privileged GUI entry point. No shell, environment overrides, or
// backend safety bypasses are accepted here.
#if !defined(_FORTIFY_SOURCE) && defined(__OPTIMIZE__) && __OPTIMIZE__ > 0
#define _FORTIFY_SOURCE 2
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
constexpr char backend[] = "/usr/bin/msi-mux-switch";

int fail(const char *message, int code) {
    std::fprintf(stderr, "msi-mux-helper: %s\n", message);
    return code;
}

bool trusted(const char *path, bool directory) {
    struct stat st {};
    if (::lstat(path, &st) != 0 || st.st_uid != 0 || (st.st_mode & 0022) != 0)
        return false;
    if (directory)
        return S_ISDIR(st.st_mode);
    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR) != 0 &&
           (st.st_mode & (S_ISUID | S_ISGID)) == 0 && st.st_nlink == 1;
}
} // namespace

int main(int argc, char **argv) {
    const char *mode = nullptr;
    bool diagnose = argc == 2 && std::strcmp(argv[1], "diagnose") == 0;
    if (argc == 3 && std::strcmp(argv[1], "apply") == 0 &&
        (std::strcmp(argv[2], "mshybrid") == 0 ||
         std::strcmp(argv[2], "discrete") == 0 ||
         std::strcmp(argv[2], "integrated") == 0))
        mode = argv[2];
    if (!diagnose && mode == nullptr)
        return fail("expected diagnose or apply {mshybrid|discrete|integrated}", 64);
    if (::geteuid() != 0 || ::getuid() != 0)
        return fail("administrator authorization is required", 77);
    for (const char *path : {"/", "/usr", "/usr/bin"}) {
        if (!trusted(path, true))
            return fail("backend directory is not trusted", 78);
    }
    if (!trusted(backend, false))
        return fail("backend must be a root-owned executable regular file without writable permissions or links", 78);
    ::umask(0077);
    if (::chdir("/") != 0)
        return fail("cannot enter a trusted working directory", 71);
    // Do not carry caller-controlled descriptors into the root backend.
    if (::syscall(SYS_close_range, 3U, ~0U, 0U) != 0)
        return fail("cannot close inherited descriptors", 71);
    char pathEnv[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
    char langEnv[] = "LANG=C.UTF-8";
    char localeEnv[] = "LC_ALL=C.UTF-8";
    char *const environment[] = {pathEnv, langEnv, localeEnv, nullptr};
    char json[] = "--json";
    char debug[] = "--debug";
    char *const arguments[] = {const_cast<char *>(backend),
                              diagnose ? debug : const_cast<char *>(mode),
                              json, nullptr};
    ::execve(backend, arguments, environment);
    return fail("cannot execute the installed backend", errno == ENOENT ? 69 : 71);
}
