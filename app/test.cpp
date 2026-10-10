#include <iostream>
#include <filesystem>
int main() {
    std::cout << std::filesystem::current_path() << std::endl;
    return 0;
}
