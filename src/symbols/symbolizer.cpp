#include "perflens/symbolizer.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace perflens {
namespace {

struct AddressReference {
    std::string module;
    std::uint64_t address{};
};

std::string trimLeft(std::string value) {
    const std::size_t first = value.find_first_not_of(' ');
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::string stripDeletedSuffix(std::string path) {
    static constexpr std::string_view suffix = " (deleted)";
    if (path.ends_with(suffix)) {
        path.resize(path.size() - suffix.size());
    }
    return path;
}

std::optional<AddressReference> resolveAddress(const std::uint64_t instructionPointer,
                                               const std::vector<MemoryMapping>& mappings) {
    const auto iterator =
        std::find_if(mappings.begin(), mappings.end(), [instructionPointer](const MemoryMapping& mapping) {
            return mapping.executable && instructionPointer >= mapping.start &&
                   instructionPointer < mapping.end && mapping.path.starts_with('/');
        });
    if (iterator == mappings.end()) {
        return std::nullopt;
    }
    return AddressReference{stripDeletedSuffix(iterator->path),
                            instructionPointer - iterator->start + iterator->fileOffset};
}

std::vector<std::string> runAddr2line(const std::string& module,
                                      const std::vector<std::uint64_t>& addresses) {
    std::array<int, 2> pipeDescriptors{};
    if (pipe2(pipeDescriptors.data(), O_CLOEXEC) == -1) {
        throw std::system_error(errno, std::generic_category(), "pipe2 addr2line");
    }

    const pid_t child = fork();
    if (child == -1) {
        const int forkError = errno;
        close(pipeDescriptors[0]);
        close(pipeDescriptors[1]);
        throw std::system_error(forkError, std::generic_category(), "fork addr2line");
    }
    if (child == 0) {
        close(pipeDescriptors[0]);
        dup2(pipeDescriptors[1], STDOUT_FILENO);
        close(pipeDescriptors[1]);

        std::vector<std::string> arguments{"addr2line", "-C", "-f", "-e", module};
        arguments.reserve(5 + addresses.size());
        for (const std::uint64_t address : addresses) {
            std::ostringstream formatted;
            formatted << "0x" << std::hex << address;
            arguments.push_back(formatted.str());
        }
        std::vector<char*> raw;
        raw.reserve(arguments.size() + 1);
        for (std::string& argument : arguments) {
            raw.push_back(argument.data());
        }
        raw.push_back(nullptr);
        execvp(raw.front(), raw.data());
        _exit(127);
    }

    close(pipeDescriptors[1]);
    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = read(pipeDescriptors[0], buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            const int readError = errno;
            close(pipeDescriptors[0]);
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            throw std::system_error(readError, std::generic_category(), "read addr2line output");
        }
    }
    close(pipeDescriptors[0]);

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "waitpid addr2line");
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::vector<std::string>(addresses.size(), "??");
    }

    std::istringstream lines{output};
    std::vector<std::string> functions;
    functions.reserve(addresses.size());
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        std::string function;
        std::string location;
        if (!std::getline(lines, function) || !std::getline(lines, location)) {
            function = "??";
        }
        functions.push_back(std::move(function));
    }
    return functions;
}

}

std::vector<MemoryMapping> parseProcMaps(const std::string& contents) {
    std::vector<MemoryMapping> mappings;
    std::istringstream input{contents};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields{line};
        std::string range;
        std::string permissions;
        std::string offset;
        std::string device;
        std::uint64_t inode = 0;
        if (!(fields >> range >> permissions >> offset >> device >> inode)) {
            continue;
        }
        const std::size_t separator = range.find('-');
        if (separator == std::string::npos) {
            continue;
        }

        MemoryMapping mapping;
        mapping.start = std::stoull(range.substr(0, separator), nullptr, 16);
        mapping.end = std::stoull(range.substr(separator + 1), nullptr, 16);
        mapping.fileOffset = std::stoull(offset, nullptr, 16);
        mapping.executable = permissions.size() > 2 && permissions[2] == 'x';
        std::getline(fields, mapping.path);
        mapping.path = trimLeft(mapping.path);
        mappings.push_back(std::move(mapping));
    }
    return mappings;
}

std::vector<MemoryMapping> readProcMaps(const pid_t target) {
    const std::string path =
        target == getpid() ? "/proc/self/maps" : "/proc/" + std::to_string(target) + "/maps";
    std::ifstream input{path};
    if (!input) {
        return {};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return parseProcMaps(contents.str());
}

std::vector<FunctionSample> symbolizeSamples(const std::vector<CpuSample>& samples,
                                             const std::vector<MemoryMapping>& mappings) {
    std::map<std::string, std::map<std::uint64_t, std::uint64_t>> moduleAddresses;
    std::uint64_t unresolved = 0;
    for (const CpuSample& sample : samples) {
        const std::optional<AddressReference> reference = resolveAddress(sample.instructionPointer, mappings);
        if (reference) {
            ++moduleAddresses[reference->module][reference->address];
        } else {
            ++unresolved;
        }
    }

    std::map<std::pair<std::string, std::string>, std::uint64_t> functions;
    for (const auto& [module, addressCounts] : moduleAddresses) {
        std::vector<std::uint64_t> addresses;
        addresses.reserve(addressCounts.size());
        for (const auto& [address, count] : addressCounts) {
            static_cast<void>(count);
            addresses.push_back(address);
        }
        const std::vector<std::string> names = runAddr2line(module, addresses);
        const std::string moduleName = std::filesystem::path{module}.filename().string();
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            const std::string function =
                names[index] == "??" || names[index].empty() ? "[unknown]" : names[index];
            functions[{moduleName, function}] += addressCounts.at(addresses[index]);
        }
    }
    if (unresolved != 0) {
        functions[{"[unknown]", "[unknown]"}] += unresolved;
    }

    std::vector<FunctionSample> result;
    result.reserve(functions.size());
    for (const auto& [key, count] : functions) {
        result.push_back({key.first, key.second, count});
    }
    std::sort(result.begin(), result.end(), [](const FunctionSample& left, const FunctionSample& right) {
        return left.samples > right.samples;
    });
    return result;
}

}
