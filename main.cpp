#include <iostream>
#include "hash_file_storage.hpp"
#include <functional>
#include <algorithm>
#include <string>
#include <iterator>
#include <random>
#include <vector>
#include <unordered_map>

#include <wheels/stopwatch.h++>

std::random_device rd;
std::default_random_engine rng(rd());

template <size_t StringLength>
std::vector<std::string> gen_strings(const size_t count) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::uniform_int_distribution<> dist(0,sizeof(alphabet)/sizeof(*alphabet)-2);

    std::vector<std::string> strs;
    strs.reserve(count);
    std::generate_n(std::back_inserter(strs), strs.capacity(),
        [&] { std::string str;
              str.reserve(StringLength+1);
              std::generate_n(std::back_inserter(str), StringLength,
                   [&] { return alphabet[dist(rng)];});
              str += "\0";
              return str; });
    return strs;
}

template <typename Ty>
double to_mcs(Ty t) {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t).count()
    );
}

template <typename Ty>
double to_ms(Ty t) {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t).count()
    );
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_insertion(std::ostream &out, const size_t N, HashedFileTy &hfile, KeysTy &keys, ValuesTy &values) {
    auto insertion_duration = wheels::time_execution(
        [&] { for (size_t i = 0; i < N; ++i) {
                  hfile.insert(keys[i], values[i]);
            }});

    out << "load factor: " << hfile.get_load_factor() << std::endl;
    out << "ins avg: " << to_mcs(insertion_duration) / N << " mcs" << std::endl;
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_search(std::ostream &out, const size_t N, HashedFileTy &hfile, KeysTy &keys, ValuesTy &values) {
    std::uniform_int_distribution<> dist(0, N-1);
    size_t found = 0;
    auto search_duration = wheels::time_execution(
        [&] { for (size_t i = 0; i < N; ++i) {
                  if (i % 2) {
                      if (hfile.has(keys[dist(rng)])) found++;
                  }
                  else {
                      if (hfile.has(values[dist(rng)])) found++;
                  }
              }});
    out << "search avg: " << to_mcs(search_duration) / N << " mcs (" << found << " found)" << std::endl;
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_deletion(std::ostream &out, const size_t N, HashedFileTy &hfile, KeysTy &keys, ValuesTy &) {
    std::uniform_int_distribution<> dist(0, N-1);
    auto deletion_duration = wheels::time_execution(
        [&] { for (size_t i = 0; i < N; ++i) {
                  if (i % 2) {
                      hfile.erase(keys[dist(rng)]);
                  }
              }});
    out << "del avg: " << to_mcs(deletion_duration) / N << " mcs" << std::endl;
}

template <size_t M>
void test(std::ostream &out, const size_t N) {
    out << "N: " << N << " M: " << M << std::endl;
    using hashed_file = HashedFile<std::string, std::string, M>;
    hashed_file hfile("./", true);
    std::vector<std::string> keys;
    std::vector<std::string> values;

    auto gen_duration = wheels::time_execution(
        [&] { keys = gen_strings<10>(N);
              values = gen_strings<10>(N); });

    out << "gen: " << to_ms(gen_duration) << " ms;\n";
    out << "page size: " << sizeof(typename hashed_file::index_t::Page) << " bytes" << std::endl;

    test_insertion(out, N, hfile, keys, values);
    test_search(out, N, hfile, keys, values);
    test_deletion(out, N, hfile, keys, values);

    out << "\n\n";
}

template <size_t MHead>
void tests_M(std::ostream &out, const size_t N) {
    test<MHead>(out, N);
}

template <size_t MHead, size_t ...MTail>
auto tests_M(std::ostream &out, const size_t N)
    -> typename std::enable_if<sizeof...(MTail) != 0, void>::type {
    test<MHead>(out, N);
    tests_M<MTail...>(out, N);
}

template <size_t ...Ms>
void tests(std::ostream &out, const std::vector<size_t> Ns) {
    out << std::boolalpha;
    for (auto N: Ns) {
        tests_M<Ms...>(out, N);
    }
}

int main() {
    tests<10, 100, 1000>(std::cout, {1000, 10000, 100000, 1000000});
    return 0;
}
