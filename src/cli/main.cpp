#include "perflens/process_runner.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: perflens PROGRAM [ARG ...]\n";
        return 2;
    }
    try {
        const auto outcome = perflens::ProcessRunner{}.run(std::vector<std::string>{argv + 1, argv + argc});
        std::cout << nlohmann::json(outcome.result).dump(2) << '\n';
        return outcome.succeeded() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "perflens: " << error.what() << '\n';
        return 2;
    }
}
