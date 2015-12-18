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
#define WHEELS_HAS_FEATURE_CXX_ALIGNED_UNION
#include <wheels/scope.h++>

#include "binstreamwrap.hpp"


namespace details {
    namespace fs = boost::filesystem;

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

    class IncompatableFormat : public std::exception {
    public:
        virtual const char *what() const noexcept override {
            return "this table has incompatible format!";
        }
    };

    namespace seg_state {
        constexpr char dead = 'd';
        constexpr char alive = 'a';
        constexpr char empty = 'e';
    }

    namespace flags {
        constexpr auto bin_io = std::ios::in | std::ios::out | std::ios::binary;
        constexpr auto bin_io_overwrite = bin_io | std::ios::trunc;
        constexpr auto bin_io_reopen = bin_io;
    }

    inline void try_to_open(const std::string &path, std::fstream &file, bool overwrite) {
        if (overwrite) {
            file.open(path, flags::bin_io_overwrite);
        }
        else {
            file.open(path, flags::bin_io_reopen);
        }

        if (!file) {
            throw CannotOpenFile(path);
        }
    }

    template <typename Key, typename Value, uint64_t PageLength>
    class FileHashIndex {
        static_assert(
            std::is_trivially_copyable<Value>::value,
            "Value have to be trivially copyable"
        );

        static_assert(PageLength >= 1, "page length cannot be less than 1");

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
        //    ^ i need it to process both new records and old ones (which already in table)

    public:
        struct Page {
            struct Segment {
                state_t state;
                hash_t hash;
                pos_t key_adress;
                data_t value;

                constexpr static Segment get_default() {
                    return { seg_state::empty, {}, {}, {} };
                }
            };

            Segment segs[PageLength];
            uint64_t seg_count;
            pos_t next_page_pos;

            constexpr static Page get_empty() {
                return { {}, 0, 0 };
            }
        };

        using Segment = typename Page::Segment;

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
                // it's important: save structure's state before exit
                m_table.goto_begin();
                m_table << m_bucket_count << m_size << PageLength;
            }
        }

        FileHashIndex(FileHashIndex &&) = default;
        FileHashIndex &operator =(FileHashIndex &&) = default;

        FileHashIndex(const FileHashIndex &) = delete;
        FileHashIndex &operator =(const FileHashIndex &) = delete;

        opt_data_t get(const key_t &key) const {
            return inspect(
                key,
                m_hasher(key),
                [] (auto *data) -> opt_data_t {
                    if (data) { return opt_data_t(data->value); }
                    else { return boost::none; }
                }
            );
        }

        // this (unlike the next one) for usual case
        bool insert(const key_t &key, const data_t &data) {
            return insert(key, [&]() { return data; });
        }

        // useful if you don't want to give me any data when unable to insert it
        bool insert(const key_t &key, std::function<data_t ()> get_data) {
            if (insert(key, get_data, seg_state::alive)) {
                m_size++;
                return true;
            }
            return false;
        }

        bool erase(const key_t &key) {
            auto hash = m_hasher(key);
            // to erase just turn `state` to `dead` and decrease counter
            return inspect(
                key,
                hash,
                [this](Segment *seg) {
                    if (seg) {
                        seg->state = seg_state::dead;
                        m_size--;
                        return true;
                    }
                    return false;
                }
            );
        }

        bool has(const key_t &key) const {
            auto hash = m_hasher(key);
            // if `inspect` gave us any segment, it obviously exists
            return inspect(key, hash, [](auto *seg) { return seg != nullptr; });
        }

        uint64_t bucket_count() const {
            return m_bucket_count;
        }

        uint64_t size() const {
            return m_size;
        }

        float load_factor() const {
            auto pseudo_size = std::max(size(), uint64_t(1)); // cause i don't want to get 0
            return float(pseudo_size) / bucket_count();
        }

        float max_load_factor() const {
            return m_load_factor_threshold;
        }

        void set_max_load_factor(const float val) {
            assert(val >= 1.0f);
            m_load_factor_threshold = val;
        }

        bool rehash_if_need() {
            constexpr bool bad_case = sizeof(data_t) < sizeof(hash_t);
            bool bad_cond = false;
            if (bad_case) bad_cond =
                    (bucket_count() >> (sizeof(data_t) * 4)) >> (sizeof(data_t) * 4); // it's ok
            if (load_factor() >= max_load_factor() && !bad_cond) {
                rehash(bucket_count() * 2);
                return true;
            }

            return false;
        }

        void shrink_to_fit() {
            auto pseudo_size = std::max(size(), uint64_t(1)); // cause i don't want to get 0
            rehash(uint64_t( // to exactly fill the new storage
                std::ceil(float(pseudo_size)) / m_load_factor_threshold
            ));
        }

        void rehash(const uint64_t new_bucket_count) {
            assert(new_bucket_count > 0u);

            if (!m_table_file.is_open()) { return; } // there is nothing to do here

            // close current table, rename it to old, open it, create fresh table to replace old one
            m_table_file.close();
            auto old_table_path = m_table_path;
            old_table_path += "_old";
            fs::rename(m_table_path, old_table_path);

            init_table(new_bucket_count, true);

            std::fstream old_table_file(old_table_path, flags::bin_io_reopen);
            if (!old_table_file) { throw CannotOpenFile(old_table_path); }
            bin_stream_t old_table(old_table_file);
            // ^ done

            // don't care about old table's parameters
            old_table.skip_n<uint64_t, uint64_t, uint64_t>();

            try {
                Page current_page;
                while (true) {
                    old_table >> current_page;
                    assert(current_page.seg_count <= PageLength); // smth wrong!
                    for (size_t i = 0; i < current_page.seg_count; ++i) {
                        Segment &seg = current_page.segs[i];
                        bool insertion_succeed = insert(
                            std::make_pair(seg.hash, seg.key_adress),
                            [&]() { return seg.value; },
                            seg.state
                        );
                        if (!insertion_succeed) {
                            assert(!"Shit happense");
                        }
                    }
                }
            } // it's strange to use exceptions as eof check, but it's simpliest way here
            catch (const fcl::ReadingAtEOF &) { // it's ok, end of file successfully reached
                old_table_file.close();
                fs::remove(old_table_path);
            }
        }

    private:
        std::hash<key_t> m_hasher{};
        std::string m_table_path;
        std::string m_keys_path;

        // they mutable 'cause i want to make get(...) and has(...) const
        mutable std::fstream m_table_file;
        mutable std::fstream m_keys_file;
        mutable bin_stream_t m_table{ m_table_file };
        mutable bin_stream_t m_keys{ m_keys_file };

         // bad for speed, but good for memory (~80mb against 3.5+ gb on the last test!)
        float m_load_factor_threshold = float(PageLength) * 0.75f;

        uint64_t m_size = 0;
        uint64_t m_bucket_count = 0;

    private:
        // in the name of fun and performance
        struct get_key_pos_visitor : boost::static_visitor<pos_t> {
            bin_stream_t &m_keys;
            get_key_pos_visitor(bin_stream_t &keys) : m_keys(keys) {}
            pos_t operator()(key_info_t val) { return val.second; }
            pos_t operator()(key_t key) { return m_keys.append(key); }
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
            auto page_pos = get_bucket_pos(hash);
            Page current_page;
            while (true) {
                m_table.set_pos(page_pos);
                m_table >> current_page;

                assert(current_page.seg_count <= PageLength);
                for (size_t i = 0; i < current_page.seg_count; ++i) {
                    Segment &seg = current_page.segs[i];
                    if (seg.hash == hash) {
                        auto keys_eq = cmp_keys_visitor(seg.key_adress, m_keys);
                        // if we already have one with such key, let's resurrect it
                        if (boost::apply_visitor(keys_eq, key)) {
                            if (seg.state == seg_state::dead) { // resurrection
                                // other data are the same
                                seg.value = value();
                                seg.state = initial_state;
                                m_table.write_at(page_pos, current_page);
                                return true;
                            }
                            return false;
                        }
                    }
                }

                // ok, it isn't the end of page, we can go on
                if (current_page.seg_count != PageLength) {
                    Segment &seg = current_page.segs[current_page.seg_count];
                    auto get_key_pos = get_key_pos_visitor(m_keys);
                    seg.hash = hash;
                    seg.key_adress = boost::apply_visitor(get_key_pos, key);
                    seg.value = value();
                    seg.state = initial_state;
                    current_page.seg_count++;
                    m_table.write_at(page_pos, current_page);
                    return true;
                }
                else {
                    if (current_page.next_page_pos != 0) {
                        page_pos = current_page.next_page_pos;
                    }
                    else {
                        // next page need to know where it can put it's adress
                        pos_t pos_to_connect = page_pos + offsetof(Page, next_page_pos);
                        page_pos = m_table.append(Page::get_empty());
                        m_table.write_at(pos_to_connect, page_pos);
                    }
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
                m_table << m_bucket_count << m_size << PageLength;

                // init a number of empty buckets
                for (uint64_t i = 0; i < initial_bucket_count; ++i) {
                    m_table << Page::get_empty();
                }
            }
            else {
                m_table.goto_begin();
                m_table >> m_bucket_count >> m_size;
                auto pageLength = fcl::read_val<uint64_t>(m_table);
                if (pageLength != PageLength) {
                    throw IncompatableFormat();
                }
            }
        }

        void init_keys(const bool overwrite) {
            try_to_open(m_keys_path, m_keys_file, overwrite);
        }

        // this one different from `const` version in: it's remembers any modifications in segment
        template <typename F> // Functor: Fn<auto (data_t *rec)>
        auto inspect(const key_t &key, const hash_t &hash, F f) {
            auto page_pos = get_bucket_pos(hash);
            Page current_page;
            auto nothing = [&f] () { return f(static_cast<Segment *>(nullptr)); };
            while (true) {
                m_table.set_pos(page_pos);
                m_table >> current_page;
                assert(current_page.seg_count <= PageLength);
                for (size_t i = 0; i < current_page.seg_count; ++i) {
                    Segment &seg = current_page.segs[i];
                    // if it's not alive, just continue searching
                    if (seg.state != seg_state::alive) { continue; }
                    if (seg.hash == hash) {
                        if (get_key(seg.key_adress) == key) {
                            auto write_at_exit = wheels::finally( // to remember any modifications
                                [&]() { m_table.write_at(page_pos, current_page); }
                            );
                            return f(&seg);
                        }
                    }
                }
                // this is the end, my only friend. the end…
                if (current_page.next_page_pos == 0) { return nothing(); }
                page_pos = current_page.next_page_pos;
            }
            assert(!"unreaceble code!");
            return nothing();
        }

        // same as above, but faster 'cause it can't remember what you have done
        template <typename F> // Functor: Fn<auto (const Segment *rec)>
        auto inspect(const key_t &key, const hash_t &hash, F f) const {
            auto page_pos = get_bucket_pos(hash);
            auto nothing = [&f] () { return f(static_cast<const Segment *>(nullptr)); };
            Page current_page;
            while (true) {
                m_table.set_pos(page_pos);
                m_table >> current_page;

                assert(current_page.seg_count <= PageLength);
                for (size_t i = 0; i < current_page.seg_count; ++i) {
                    const Segment &seg = current_page.segs[i];
                    if (seg.state != seg_state::alive) continue;
                    if (seg.hash == hash) {
                        if (get_key(seg.key_adress) == key) { return f(&seg); }
                    }
                }
                if (current_page.next_page_pos == 0) { return nothing(); }
                page_pos = current_page.next_page_pos;
            }
            assert(!"unreaceble code!");
            return nothing();
        }

        pos_t get_bucket_pos(const hash_t hash) const {
            auto number = calc_bucket_number(hash);
            auto header_size = sizeof(m_bucket_count) + sizeof(m_size) + sizeof(PageLength);
            auto bucket_pos = header_size + sizeof(Page) * number;
            return bucket_pos;
        }

        uint64_t calc_bucket_number(const hash_t hash) const {
            assert(bucket_count() != 0);
            return hash % bucket_count(); // simpliest way to do it
        }

        key_t get_key(pos_t key_pos) const {
            return m_keys.read_at<key_t>(key_pos);
        }
    };

    template <typename Value>
    class FileStorage { // simple class, but in the name of SOLID i need it
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
            return m_storage.append(val);
        }

    private:
        mutable std::fstream m_storage_file;
        mutable bin_stream_t m_storage{ m_storage_file };
    };
}

template <typename Key, typename Value, uint64_t PageLength>
class HashedFile {
    using value_t = Value;
    using opt_value_t = boost::optional<value_t>;
    using key_t = Key;
    using pos_t = std::streamoff;

public:
    using index_t = details::FileHashIndex<key_t, pos_t, PageLength>;
    using storage_t = details::FileStorage<value_t>;

public:
    HashedFile(const details::fs::path &working_dir, bool overwrite)
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
        auto pos_opt = m_index.get(key); // return value only if hash-table said 'yes'
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

    bool has(const key_t &key) const {
        return m_index.has(key);
    }

    size_t size() const {
        return m_index.size();
    }

    bool empty() const {
        return size() == 0;
    }

    const index_t &idxs() const {
        return m_index;
    }

    void set_load_factor_threshold(float new_threshold) {
        m_index.set_max_load_factor(new_threshold);
    }

    float get_load_factor() const {
        return m_index.load_factor();
    }

private:
    index_t m_index;
    storage_t m_storage;
};
