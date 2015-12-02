#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <list>
#include <tuple>
#include <utility>
#include <limits>
#include <functional>
#include <algorithm>
#include <boost/optional.hpp>


namespace details {
    template <typename Key>
    class hash_index {
        using key_type = Key;
        using hash_type = size_t;
        using pos_type = /*std::streampos*/size_t;
        using opt_pos_type = boost::optional<pos_type>;
        using record_type = std::tuple<key_type, pos_type>;
        using index_type = std::tuple<hash_type, record_type>;
        using bucket_type = std::list<index_type>;
        using bucket_storage_type = std::vector<bucket_type>;

    public:
        opt_pos_type get(const key_type &key) const {
            return inspect(
                [] (auto *rec) -> opt_pos_type {
                    if (rec) {
                        return opt_pos_type(std::get<1>(*rec));
                    }
                    else {
                        return boost::none;
                    }
                },
                key,
                m_hasher(key)
            );
        }

        bool insert(const key_type &key, const pos_type &pos) {
            return insert(std::make_tuple(key, pos));
        }

        bool insert(const record_type &rec) {
            rehash_if_need();
            const auto &key = std::get<0>(rec);
            auto hash = m_hasher(key);
            auto &bucket = bucket_by_hash(hash);
            if (has(key, hash)) {
                return false;
            }

            bucket.push_back(std::make_tuple(hash, rec));
            m_size++;
            return true;
        }

        bool has(const key_type &key) const {
            auto hash = m_hasher(key);
            return has(key, hash);
        }

        size_t bucket_count() const {
            return m_buckets.size();
        }

        size_t bucket_size(size_t n) const {
            assert(n < bucket_count());
            return m_buckets[n].size();
        }

        size_t size() const {
            return m_size;
        }

        float load_factor() const {
            return float(std::max(size(), size_t(1))) / bucket_count();
        }

        float max_load_factor() const {
            return m_rehash_threshold;
        }

        void set_max_load_factor(const float val) {
            assert(val >= 1.0f);
            m_rehash_threshold = val;
        }

        bool rehash_if_need() {
            if (load_factor() >= max_load_factor()) {
                rehash(bucket_count() * 2);
                return true;
            }

            return false;
        }

        void rehash(const size_t new_bucket_count) {
            assert(new_bucket_count > 0u);
            auto old_buckets = std::exchange(m_buckets, bucket_storage_type(new_bucket_count));
            for (auto &bucket: old_buckets) {
                for (auto &index: bucket) {
                    bucket_by_hash(std::get<0>(index)).push_back(std::move(index));
                }
            }
        }

    private:
        const std::hash<key_type> m_hasher = {};
        const hash_type max_hash = std::numeric_limits<hash_type>::max();
        bucket_storage_type m_buckets = bucket_storage_type(1);
        float m_rehash_threshold = 2.0f;
        size_t m_size;

    private:
        template< typename F > // Functor: Fn<auto (record_type *rec)>
        auto inspect(F f, const key_type &key, const hash_type &hash) {
            return const_cast<const decltype(this)>(this)->inspect(f, key, hash);
        }

        template< typename F > // Functor: Fn<auto (const record_type *rec)>
        auto inspect(F f, const key_type &key, const hash_type &hash) const {
            auto &bucket = bucket_by_hash(hash);
            for (const auto &index: bucket) {
                auto &idx_hash = std::get<0>(index);
                if (idx_hash == hash) {
                    auto &idx_record = std::get<1>(index);
                    auto &idx_key = std::get<0>(idx_record);
                    if (idx_key == key) { // to avoid hash-collisions
                        return f(&idx_record);
                    }
                }
            }

            return f(static_cast<const record_type *>(nullptr));
        }

        bool has(const key_type &key, const hash_type &hash) const {
            return inspect([](auto *rec) { return rec != nullptr; }, key, hash);
        }

        bucket_type &bucket_by_key(const key_type &key) {
            auto hash = m_hasher(key);
            return bucket_by_hash(hash);
        }

        bucket_type &bucket_by_hash(const hash_type hash) {
            return m_buckets[calc_bucket_number(hash)];
        }

        const bucket_type &bucket_by_hash(const hash_type hash) const {
            return m_buckets[calc_bucket_number(hash)];
        }

        size_t calc_bucket_number(const hash_type hash) const {
            assert(bucket_count() != 0);
            auto bucket_coefficient = static_cast<double>(bucket_count()) / max_hash;
            auto bucket_number = static_cast<size_t>(hash * bucket_coefficient);
            assert(bucket_number < bucket_count());
            return bucket_number;
        }
    };

    template <typename Value>
    class fval_storage {
    public:
        using value_type = Value;
        using pos_type = std::streampos;

        value_type get_value(const pos_type &) const {
            return {};
        }
    };
}

template <typename Value>
class hashed_file {
public:
    using iterator = bool;
    using value_type = Value;
    using key_type = std::string;
    using record_type = std::pair<key_type, value_type>;

    bool insert(const record_type &) {
        return {};
    }

    value_type find(const key_type &) const {
        return {};
    }

    bool erase(const key_type &) {
        return {};
    }

private:
};
