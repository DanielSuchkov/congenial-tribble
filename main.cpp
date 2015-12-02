#include <iostream>
#include "fhash_map.hpp"
#include <cstdint>

int main()
{
    details::hash_index<std::string> map;
    map.insert("a", 10);
    map.insert("b", 12);

    std::cout << std::boolalpha;
    std::cout << map.has("a") << std::endl;
    std::cout << map.has("b") << std::endl;
    std::cout << map.has("c") << std::endl;
    int i = map.get("a").get();
    std::cout << i << std::endl;
    std::cout << map.get("asdasd").is_initialized() << std::endl;
    return 0;
}

