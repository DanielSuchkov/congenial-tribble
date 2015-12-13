#include <iostream>
#include "hash_file_storage.hpp"
#include <functional>
#include <algorithm>
#include <string>
#include <iterator>
#include <random>
#include <vector>
#include <map>
#include <functional>
#include <memory>

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

std::string read_line(std::istream &is) {
    std::string out;
    std::getline(is, out, '\n');
    return out;
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_insertion(
        std::ostream &out,
        const size_t N,
        HashedFileTy &hfile,
        KeysTy &keys,
        ValuesTy &values) {
    auto insertion_duration = wheels::time_execution(
        [&] { for (size_t i = 0; i < N; ++i) {
                  hfile.insert(keys[i], values[i]);
            }});

    out << "load factor: " << hfile.get_load_factor() << std::endl;
    out << "ins avg: " << to_mcs(insertion_duration) / N << " mcs" << std::endl;
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_search(
        std::ostream &out,
        const size_t N,
        HashedFileTy &hfile,
        KeysTy &keys,
        ValuesTy &values) {
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
    out << "search avg: " << to_mcs(search_duration) / N << " mcs "
        << "(" << found << " found)" << std::endl;
}

template <typename HashedFileTy, typename KeysTy, typename ValuesTy>
void test_deletion(
        std::ostream &out,
        const size_t N,
        HashedFileTy &hfile,
        KeysTy &keys,
        ValuesTy &) {
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
    details::fs::create_directory("./test");
    hashed_file hfile("./test", true);
    std::vector<std::string> keys;
    std::vector<std::string> values;

    auto gen_duration = wheels::time_execution(
        [&] { keys = gen_strings<10>(N);
              values = gen_strings<10>(N); });

    out << "generation: " << to_ms(gen_duration) << " ms;\n";
    out << "page size: " << sizeof(typename hashed_file::index_t::Page) << " bytes" << std::endl;

    test_insertion(out, N, hfile, keys, values);
    test_search(out, N, hfile, keys, values);
    test_deletion(out, N, hfile, keys, values);

    out << "\n\n";
    details::fs::remove_all("./test");
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

using action_t = std::function<void ()>;
using action_map_t = std::map<std::string, action_t>;
using hash_storage_t = HashedFile<std::string, std::string, 6>;
using opt_hash_storage_t = std::unique_ptr<hash_storage_t>;

class EmptyOptional : public std::exception {
public:
    EmptyOptional(const std::string &error)
        : m_message("optional is empty: [" + error + "]") {}

    virtual const char *what() const noexcept override {
        return m_message.c_str();
    }
private:
    const std::string m_message;
};

class NoSuchValue : public std::exception {
public:
    NoSuchValue(const std::string &error)
        : m_message("no such value: [" + error + "]") {}

    virtual const char *what() const noexcept override {
        return m_message.c_str();
    }
private:
    const std::string m_message;
};

class Exit : public std::exception {
public:
    virtual const char *what() const noexcept override {
        return "";
    }
};

template <typename Ty>
Ty &ref_or_err(std::unique_ptr<Ty> &opt, std::string msg) {
    if (!opt) {
        throw EmptyOptional(msg);
    }
    return *opt.get();
}

template <typename Ty, typename Exc>
Ty value_or_exc(boost::optional<Ty> &opt, Exc err) {
    if (opt.is_initialized()) {
        return opt.get();
    }
    throw err;
}

void flush_line(std::istream &is) {
    std::string tmp;
    std::getline(is, tmp);
}

int main() {
    opt_hash_storage_t hfile;
    action_map_t action_map = {
        { "run_tests", [&] { tests<10, 100, 1000>(std::cout, {1000, 10000, 100000, 1000000}); } },

        { "stats", [&] { auto &active_db = ref_or_err(hfile, "no active db found");
                         std::cout << "size: " << active_db.idxs().size() << std::endl
                            << "bucket count: " << active_db.idxs().bucket_count() << std::endl
                            << "load factor: " << active_db.idxs().load_factor() << std::endl; } },

        { "load_db", [&] { std::cout << "Enter directory to load from → ";
                           auto dir = fcl::read_val<std::string>(std::cin);
                           hfile = std::make_unique<hash_storage_t>(dir, false); } },

        { "create_db", [&] { std::cout << "Enter directory to create → ";
                             auto dir = fcl::read_val<std::string>(std::cin);
                             if (!details::fs::is_directory(dir)) {
                                 details::fs::create_directories(dir);
                             }
                             hfile = std::make_unique<hash_storage_t>(dir, true); } },

        { "insert", [&] { auto &active_db = ref_or_err(hfile, "no active db found");
                          std::cout << "Enter key ↓" << std::endl;
                          flush_line(std::cin);
                          auto key = read_line(std::cin);
                          std::cout << "Enter value ↓" << std::endl;
                          auto value = read_line(std::cin);
                          if (!active_db.insert(key, value)) {
                              std::cout << "Cannot insert: key already binded\n";
                          }
                          auto val = active_db.get(key);
                          std::cout << "pair (" << key << ", " << value_or_exc(
                              val, NoSuchValue("associated value not found")) << ")"
                          << " added" << std::endl; } },

        { "get", [&] { auto &active_db = ref_or_err(hfile, "no active db found");
                       std::cout << "Enter associated key ↓" << std::endl;
                       flush_line(std::cin);
                       auto key = read_line(std::cin);
                       auto value = active_db.get(key);
                       std::cout << value_or_exc(
                           value, NoSuchValue("associated value not found")) << std::endl; } },

        { "has", [&] { auto &active_db = ref_or_err(hfile, "no active db found");
                       std::cout << "Enter associated key ↓" << std::endl;
                       flush_line(std::cin);
                       auto key = read_line(std::cin);
                       auto has = active_db.has(key);
                       std::cout << (has ? "key exists"
                                         : "no such key") << std::endl; } },

        { "remove", [&] { auto &active_db = ref_or_err(hfile, "no active db found");
                          std::cout << "Enter associated key ↓" << std::endl;
                          flush_line(std::cin);
                          auto key = read_line(std::cin);
                          auto result = active_db.erase(key);
                          std::cout << (result ? "value successfuly removed"
                                               : "no associated values") << std::endl; } },

        { "close_db", [&] { std::cout << "Are you sure? (y/n) ";
                            auto answ = fcl::read_val<char>(std::cin);
                            if (answ == 'y') { hfile.reset(nullptr); } } },

        { "exit", [&] { throw Exit(); } },
    };

    std::cout << "What do you want to do? \"help\" to see avialable commands" << std::endl;
    while (true) {
        try {
            std::cout << " → ";
            auto command = fcl::read_val<std::string>(std::cin);
            if (command == "help") {
                for (auto &action : action_map) { std::cout << action.first << std::endl; }
                continue;
            }
            else {
                auto it = action_map.find(command);
                if (it == action_map.end()) {
                    std::cout << "No such command" << std::endl;
                    continue;
                }
                it->second();
            }
        }
        catch (const Exit &) { break; }
        catch (const NoSuchValue &err) { std::cout << "Sorry, " << err.what() << std::endl; }
        catch (const EmptyOptional &err) { std::cout << err.what() << std::endl; }
        catch (const details::CannotOpenFile &err) { std::cout << err.what() << std::endl; }
        catch (const std::exception &err) {
            std::cout << "Exception: " << err.what() << std::endl;
            return 1;
        }
        catch (...) {
            std::cout << "Caught unknown exception" << std::endl;
            return 2;
        }
    }

    return 0;
}
