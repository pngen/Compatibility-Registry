#include "compat/net.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <process.h>
#include <string>

int main(int argc, char** argv) {
    std::uint16_t port = 19731;
    std::string recoverPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (a == "--recover" && i + 1 < argc) recoverPath = argv[++i];
    }
    std::unique_ptr<compat::CompatibilityRegistry> seed;
    if (!recoverPath.empty()) {
        seed = compat::CompatibilityRegistry::load(recoverPath);
        std::printf("coordinator recovered from %s\n", recoverPath.c_str());
    }
    compat::CoordinatorServer server(port, std::move(seed));
    std::printf("coordinator listening on port %u pid=%d\n", static_cast<unsigned>(port), static_cast<int>(_getpid()));
    std::fflush(stdout);
    server.run();
    return 0;
}
