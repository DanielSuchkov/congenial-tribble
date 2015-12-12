#pragma once
#include <string>
#include <fstream>
#include <tuple>
#include <utility>
#include <memory>
#include <cstdint>
#include <algorithm>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/variant.hpp>

#include "binstreamwrap.hpp"


namespace details {
    namespace fs = boost::filesystem;

    class CorruptedFile : public std::exception {
    public:
        virtual const char *what() const noexcept override {
            return "i/o file corrupted!";
        }
    };

    class CannotOpenFile : public std::exception {
    public:
        CannotOpenFile(const std::string &filename)
            : m_message("cannot open file: [" + filename + "]") {}

        virtual const char *what() const noexcept override {
            return m_message.c_str();
        }
    private:
        const std::string m_message;
    };

    namespace cell_state {
        constexpr char dead = 'd';
        constexpr char alive = 'a';
        constexpr char empty = 'e';
    }

    namespace flags {
        constexpr auto bin_io = std::ios::in | std::ios::out | std::ios::binary;
        constexpr auto bin_io_overwrite = bin_io | std::ios::trunc;
        constexpr auto bin_io_reopen = bin_io | std::ios::app;
    }

    inline void try_to_open(const std::string &path, std::fstream &file, bool overwrite) {
        if (overwrite) {
            file.open(path, flags::bin_io_overwrite);
        }
        else {
            file.open(path, flags::bin_io_reopen);
            if (!file) {
                file.open(path, flags::bin_io_overwrite);
            }
        }

        if (!file) {
            throw CannotOpenFile(path);
        }
    }

    template <typename Key, typename Value>
    class FileHashIndex {
        static_assert(
            std::is_trivially_copyable<Value>::value,
            "Value have to be trivially copyable"
        );

        using key_t = Key;
        using hash_t = uint64_t;
        using data_t = Value;
        using hasher_t = std::hash<key_t>;
        using opt_data_t = boost::optional<data_t>;
        using pos_t = int64_t;
        using state_t = char;
        using bin_stream_t = fcl::BinIOStreamWrap<std::fstream>;
        using key_info_t = std::pair<hash_t, pos_t>;
        using key_variant_t = boost::variant<key_t, key_info_t>;
        // bucket cell structure:
        // [state_t(state e/a/d) | hash_t(key hash) | pos_t(key adress) | pos_t(next) | data_t(value)]

        static constexpr uint64_t bucket_cell_size
            = sizeof(state_t) + sizeof(hash_t) + sizeof(pos_t) + sizeof(pos_t) + sizeof(data_t);

    public:
        FileHashIndex(
                const fs::path &table_path,
                const fs::path &keys_path,
                const bool overwrite)
            : m_table_path(table_path.string())
            , m_keys_path(keys_path.string()) {
            init_keys(overwrite);
            init_table(2, overwrite);
        }

        ~FileHashIndex() {
            if (m_table_file) {
                m_table.goto_begin();
                m_table << m_bucket_count;
            }
        }

        FileHashIndex(FileHashIndex &&) = default;
        FileHashIndex &operator =(FileHashIndex &&) = default;

        FileHashIndex(const FileHashIndex &) = delete;
        FileHashIndex &operator =(const FileHashIndex &) = delete;

        opt_data_t get(const key_t &key) const {
            return inspect(
                [] (auto *data) -> opt_data_t {
                    if (data) {
                        return opt_data_t(*data);
                    }
                    else {
                        return boost::none;
                    }
                },
                key,
                m_hasher(key)
            );
        }

        bool insert(const key_t &key, const data_t &data) {
            return insert(key, [&]() { return data; }, cell_state::alive);
        }

        bool insert(const key_t &key, std::function<data_t ()> get_data) {
            return insert(key, get_data, cell_state::alive);
        }

        bool erase(const key_t &key) {
            auto hash = m_hasher(key);

            auto cell_pos = get_bucket_pos(hash);
            while (true) {
                auto state = m_table.read_at<state_t>(cell_pos);
                if (state == cell_state::empty) {
                    return false;
                }
                if (state != cell_state::dead && state != cell_state::alive) {
                    throw CorruptedFile();
                }

                auto cell_hash = fcl::read_val<hash_t>(m_table);
                if (hash == cell_hash) {
                    auto cell_key_pos = fcl::read_val<pos_t>(m_table);
                    if (key == get_key(cell_key_pos)) {
                        m_table.write_at(cell_pos, cell_state::dead);
                        m_size--;
                        return true;
                    }
                }
                else {
                    m_table.skip<pos_t>();
                    auto next_pos = fcl::read_val<pos_t>(m_table);
                    if (next_pos == pos_t(0)) {
                        return false;
                    }
                    cell_pos = next_pos;
                }
            }
            return false;
        }

        bool has(const key_t &key) const {
            auto hash = m_hasher(key);
            return has(key, hash);
        }

        uint64_t bucket_count() const {
            return m_bucket_count;
        }

        uint64_t size() const {
            return m_size;
        }

        float load_factor() const {
            return float(std::max(size(), uint64_t(1))) / bucket_count();
        }

        float max_load_factor() const {
            return m_rehash_threshold;
        }

        void set_max_load_factor(const float val) {
            assert(val >= 1.0f);
            m_rehash_threshold = val;
        }

        bool rehash_if_need() {
            constexpr bool bad_case = sizeof(data_t) < sizeof(hash_t);
            constexpr bool bad_cond = bad_case ? (bucket_count() / (1 << (sizeof(data_t) * 8))) : false;
            if (load_factor() >= max_load_factor() && !bad_cond) {
                rehash(bucket_count() * 2);
                return true;
            }

            return false;
        }

        void shrink_to_fit() {
            rehash(uint64_t(std::ceil(float(size()) / m_rehash_threshold)));
        }

        void rehash(const uint64_t new_bucket_count) {
            assert(new_bucket_count > 0u);

            if (!m_table_file.is_open()) {
                return; // there is nothing to do here
            }

            m_table_file.close();
            auto old_table_path = m_table_path;
            old_table_path += "_old";
            fs::rename(m_table_path, old_table_path);

            init_table(new_bucket_count, true);

            std::fstream old_table_file(old_table_path, flags::bin_io_reopen);
            if (!old_table_file) {
                throw CannotOpenFile(old_table_path);
            }
            bin_stream_t old_table(old_table_file);

            old_table.skip(sizeof(m_bucket_count));

            try {
                while (true) {
                    auto state = fcl::read_val<state_t>(old_table);
                    if (state == cell_state::empty) {
                        old_table.skip_n<hash_t, pos_t, pos_t, data_t>();
                        continue;
                    }

                    if (state != cell_state::dead && state != cell_state::alive) {
                        throw CorruptedFile();
                    }
                    auto hash = fcl::read_val<hash_t>(old_table);
                    auto key_adress = fcl::read_val<pos_t>(old_table);
                    old_table.skip<pos_t>(); // i'm don't care about old structure
                    auto value = fcl::read_val<data_t>(old_table);
                    if (!insert(std::make_pair(hash, key_adress), [&]() { return value; }, state)) {
                        assert(!"Shit happense");
                    }
                }
            }
            catch (const fcl::ReadingAtEOF &) {
                old_table_file.close();
                fs::remove(old_table_path);
                // it's ok, end of file successfully reached
            }
        }

    private:
        // bucket cell structure:
        // [state_t(state e/a/d) | hash_t(key hash) | pos_t(key adress) | pos_t(next) | data_t(value)]
        const std::hash<key_t> m_hasher{};
        const std::string m_table_path;
        const std::string m_keys_path;
        mutable std::fstream m_table_file;
        mutable std::fstream m_keys_file;
        mutable bin_stream_t m_table{ m_table_file };
        mutable bin_stream_t m_keys{ m_keys_file };
        float m_rehash_threshold = 2.0f;
        uint64_t m_size = 0;
        uint64_t m_bucket_count = 0;

    private:
        struct get_key_pos_visitor : boost::static_visitor<pos_t> {
            bin_stream_t &m_keys;
            get_key_pos_visitor(bin_stream_t &keys) : m_keys(keys) {}
            pos_t operator()(key_info_t val) { return val.second; }
            pos_t operator()(key_t key) { return m_keys.write(key); }
        };

        struct cmp_keys_visitor : boost::static_visitor<bool> {
            pos_t m_key_pos;
            bin_stream_t &m_keys;
            cmp_keys_visitor(pos_t key_pos, bin_stream_t &keys) : m_key_pos(key_pos), m_keys(keys) {}
            bool operator()(key_t key) { return key == m_keys.read_at<key_t>(m_key_pos); }
            bool operator()(key_info_t p) { return p.second == m_key_pos; }
        };

        struct get_hash_visitor : boost::static_visitor<hash_t> {
            hash_t operator()(key_t key) const { return hasher_t()(key); }
            hash_t operator()(key_info_t p) const { return p.first; }
        };

        bool insert(
                key_variant_t key,
                std::function<data_t ()> value,
                state_t initial_state) {
            rehash_if_need();

            hash_t hash = boost::apply_visitor(get_hash_visitor(), key);

            pos_t put_next_pos = 0;
            auto cell_pos = get_bucket_pos(hash);

            auto write_here = [&] () {
                get_key_pos_visitor get_key_pos(m_keys);
                auto key_pos = boost::apply_visitor(get_key_pos, key);
                auto new_pos = m_table.get_pos();
                m_table << initial_state << hash << key_pos << pos_t(0) << value();
                if (put_next_pos) {
                    m_table.write_at(put_next_pos, new_pos);
                }
                m_size++;
            };

            while (true) {
                auto state = m_table.read_at<state_t>(cell_pos);

                if (state == cell_state::empty) {
                    m_table.set_pos(cell_pos);
                    write_here();
                    return true;
                }
                else if (state == cell_state::dead || state == cell_state::alive) {
                    auto cell_hash = fcl::read_val<hash_t>(m_table);
                    if (hash == cell_hash) {
                        return false;
                    }
                    auto cell_key_pos = fcl::read_val<pos_t>(m_table);
                    cmp_keys_visitor cmp_keys{ cell_key_pos, m_keys };
                    if (boost::apply_visitor(cmp_keys, key)) { // keys are equal
                        return false;
                    }
                    put_next_pos = m_table.get_pos();
                    if (auto next_pos = fcl::read_val<pos_t>(m_table)) {
                        cell_pos = next_pos;
                    }
                    else {
                        m_table.goto_end();
                        write_here();
                        return true;
                    }
                }
                else {
                    throw CorruptedFile();
                }
            }
            assert(!"unreachable code!");
            return false;
        }

        void init_table(uint64_t initial_bucket_count, const bool overwrite) {
            try_to_open(m_table_path, m_table_file, overwrite);

            if (overwrite) {
                m_bucket_count = initial_bucket_count;
                m_table.goto_begin();
                m_table << m_bucket_count;
                for (uint64_t i = 0; i < initial_bucket_count; ++i) {
                    m_table << cell_state::empty << hash_t{} << pos_t{} << pos_t{0} << data_t{};
                }
            }
            else {
                m_table.goto_begin();
                m_bucket_count = fcl::read_val<uint64_t>(m_table);
            }
        }

        void init_keys(const bool overwrite) {
            try_to_open(m_keys_path, m_keys_file, overwrite);
        }

        template< typename F > // Functor: Fn<auto (data_t *)>
        auto inspect(F f, const key_t &key, const hash_t &hash) {
            return const_cast<const decltype(this)>(this)->inspect(f, key, hash);
        }

        template< typename F > // Functor: Fn<auto (data_t *rec)>
        auto inspect(F f, const key_t &key, const hash_t &hash) const {
            m_table.set_pos(get_bucket_pos(hash));
            auto nothing = [&f] () { return f(static_cast<const data_t *>(nullptr)); };
            while (true) {
                auto state = fcl::read_val<state_t>(m_table);
                if (state == cell_state::empty) {
                    return nothing();
                }
                auto cell_hash = fcl::read_val<hash_t>(m_table);
                if (cell_hash == hash) {
                    auto cell_key_pos = fcl::read_val<pos_t>(m_table);
                    if (get_key(cell_key_pos) == key) { // to avoid hash-collisions
                        m_table.skip<pos_t>(); // skip pos of next cell
                        auto cell_value = fcl::read_val<data_t>(m_table);
                        return f(&cell_value);
                    }
                }
                m_table.skip<pos_t>(); // we don't need no key
                if (auto next_pos = fcl::read_val<pos_t>(m_table)) {
                    m_table.set_pos(next_pos);
                }
                else {
                    return nothing();
                }
            }
            assert(!"unreaceble code!");
            return nothing();
        }

        bool has(const key_t &key, const hash_t &hash) const {
            return inspect([](auto *rec) { return rec != nullptr; }, key, hash);
        }

        pos_t get_bucket_pos(const hash_t hash) const {
            auto number = calc_bucket_number(hash);
            auto bucket_pos = bucket_cell_size * number + sizeof(m_bucket_count);
            return bucket_pos;
        }

        uint64_t calc_bucket_number(const hash_t hash) const {
            assert(bucket_count() != 0);
            return hash % bucket_count();
        }

        key_t get_key(pos_t key_pos) const {
            return m_keys.read_at<key_t>(key_pos);
        }
    };

    template <typename Value>
    class FileStorage {
    public:
        using value_t = Value;
        using pos_t = std::streamoff;
        using bin_stream_t = fcl::BinIOStreamWrap<std::fstream>;

        FileStorage(const fs::path &storage_path, const bool overwrite) {
            try_to_open(storage_path.string(), m_storage_file, overwrite);
        }

        ~FileStorage() = default;

        FileStorage(FileStorage &&) = default;
        FileStorage &operator =(FileStorage &&) = default;

        FileStorage(const FileStorage &) = delete;
        FileStorage &operator =(const FileStorage &) = delete;

        value_t get(const pos_t pos) const {
            return m_storage.read_at<value_t>(pos);
        }

        pos_t insert(const value_t &val) {
            return m_storage.write(val);
        }

    private:
        mutable std::fstream m_storage_file;
        mutable bin_stream_t m_storage{ m_storage_file };
    };
}

template <typename Key, typename Value>
class HashedFile {
    using value_t = Value;
    using opt_value_t = boost::optional<value_t>;
    using key_t = Key;
    using pos_t = std::streamoff;
    using index_t = details::FileHashIndex<key_t, pos_t>;
    using storage_t = details::FileStorage<value_t>;

public:
    HashedFile(const details::fs::path &working_dir, bool overwrite = true)
        : m_index(working_dir/"hash_idx", working_dir/"keys_idx", overwrite)
        , m_storage(working_dir/"data", overwrite) {}

    ~HashedFile() = default;

    HashedFile(HashedFile &&) = default;
    HashedFile &operator =(HashedFile &&) = default;

    HashedFile(const HashedFile &) = delete;
    HashedFile &operator =(const HashedFile &) = delete;

    bool insert(const key_t &key, const value_t &val) {
        return m_index.insert(key, std::bind(&storage_t::insert, &m_storage, val));
    }

    opt_value_t get(const key_t &key) const {
        auto pos_opt = m_index.get(key);
        if (pos_opt) {
            return m_storage.get(pos_opt.get());
        }
        else {
            return boost::none;
        }
    }

    bool erase(const key_t &key) {
        return m_index.erase(key);
    }

    bool has(const key_t &key) {
        return m_index.has(key);
    }

private:
    index_t m_index;
    storage_t m_storage;
};
