#include <iostream>
#include "hash_file_storage.hpp"
#include <unordered_map>
#include <functional>

int main()
{
    std::cout << std::boolalpha;
    HashedFile<std::string, std::string> map("./", true);

    return 0;
}
