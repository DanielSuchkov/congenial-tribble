#pragma once

#include <istream>
#include <ostream>
#include <vector>
#include <list>
#include <string>
#include <tuple>
#include <cstdint>
#include <type_traits>
#include <exception>
#ifdef QT_VERSION
#   include <QString>
#endif // QT_VERSION
#include "binstreamwrapfwd.hpp"

namespace fcl {
#ifdef _MSC_VER
#   define noexcept
#endif


class ReadingAtEOF : public std::exception {
public:
    virtual const char *what() const noexcept override final {
        return "attempt to read at eof";
    }
};

#ifdef _MSC_VER
#   undef noexcept
#endif


enum class UseExceptions {
    yes, no
};

namespace details {
    template <typename Type, unsigned N, unsigned Last>
    struct Reader {
        template <class StreamTy, typename... Tp>
        static auto read_tuple(BinIStreamWrap<StreamTy> &is, Type &tpl)
            -> BinIStreamWrap<StreamTy> & {
            is >> std::get<N>(tpl);
            Reader<Type, N + 1, Last>::read_tuple(is, tpl);
            return is;
        }
    };

    template <typename Type, unsigned N>
    struct Reader<Type, N, N> {
        template <class StreamTy, typename... Tp>
        static auto read_tuple(BinIStreamWrap<StreamTy> &is, Type &)
            -> BinIStreamWrap<StreamTy> & {
            return is;
        }
    };
}
//namespace from {
//    struct begin_t {} begin;
//    struct end_t {} end;
//}

template <typename T, typename Source>
T read_val(Source &stream) {
    T outVal;
    stream >> outVal;
    return outVal;
}

template <class StreamTy>
class BinIStreamWrap {
public:
    /*!
     * \brief BinIStreamWrap
     * \param istr Input stream opend with std::ios::binary flag
     * \param useExceptions UseExceptions::yes if you want that this class notify you about
     * reading at eof using exceptions and UseExceptions::no if no
     */
    explicit BinIStreamWrap(
            StreamTy &istr,
            UseExceptions useExceptions = UseExceptions::yes)
        : m_istr(istr)
        , m_useExceptions(useExceptions == UseExceptions::yes) {}

    BinIStreamWrap(const BinIStreamWrap &) = delete;
    BinIStreamWrap &operator =(const BinIStreamWrap &) = delete;

    BinIStreamWrap(BinIStreamWrap &&) = default;
    BinIStreamWrap &operator =(BinIStreamWrap &&) = default;

    ~BinIStreamWrap() = default;

    int64_t get_ipos() const {
        return m_istr.tellg();
    }

    void set_ipos(int64_t pos) const {
        m_istr.seekg(pos);
    }

    void goto_iend() {
        m_istr.seekg(0, m_istr.end);
    }

    void goto_ibegin() {
        m_istr.seekg(0, m_istr.beg);
    }

    void iskip(size_t offset) {
        set_ipos(get_ipos() + offset);
    }

    template <typename Ty>
    void iskip() {
        iskip(sizeof(Ty));
    }

    template <typename Ty, typename ...Rest>
    auto iskip_n() -> typename std::enable_if<sizeof...(Rest) != 0, void>::type {
        iskip<Ty>();
        iskip_n<Rest...>();
    }

    template<typename Ty>
    void iskip_n() {
        iskip<Ty>();
    }

    template <typename Ty>
    Ty read_at(int64_t pos) {
        set_ipos(pos);
        return read_val<Ty>(*this);
    }

    template <typename T>
    friend BinIStreamWrap &operator >>(BinIStreamWrap &is, T &t) {
        static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
        is.m_istr.read(reinterpret_cast<char *>(&t), sizeof(t));
        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }

    template <typename T, uint64_t I>
    friend auto operator >>(BinIStreamWrap &is, T (&t)[I])
        -> typename std::enable_if<
            std::is_trivially_copyable<T>::value,
            BinIStreamWrap &>::type {
        is.m_istr.read(reinterpret_cast<char *>(t), sizeof(T) * I);
        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }

    template <typename T, uint64_t I>
    friend auto operator >>(BinIStreamWrap &is, T (&t)[I])
        -> typename std::enable_if<
            !std::is_trivially_copyable<T>::value,
            BinIStreamWrap &>::type {
        for (auto &el : t) {
            is >> el;
            if (is.m_istr.eof() && is.m_useExceptions) {
                throw ReadingAtEOF();
            }
        }
        return is;
    }

    template <typename T, typename Alloc>
    friend auto operator >>(BinIStreamWrap &is, std::vector<T, Alloc> &vec)
        -> typename std::enable_if<
            std::is_trivially_copyable<T>::value,
            BinIStreamWrap &>::type {
        uint64_t size;
        is >> size;
        vec.resize(static_cast<size_t>(size));
        is.m_istr.read(
            reinterpret_cast<char *>(vec.data()),
            static_cast<size_t>(size) * sizeof(T)
        );

        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }

    template <typename T, typename Alloc>
    friend auto operator >>(BinIStreamWrap &is, std::vector<T, Alloc> &vec)
        -> typename std::enable_if<
            !std::is_trivially_copyable<T>::value,
            BinIStreamWrap &>::type {
        uint64_t size;
        is >> size;
        vec.resize(static_cast<size_t>(size));

        for (auto &el : vec) {
            is >> el;
            if (is.m_istr.eof() && is.m_useExceptions) {
                throw ReadingAtEOF();
            }
        }
        return is;
    }

    template <typename T, typename Alloc>
    friend BinIStreamWrap &operator >>(BinIStreamWrap &is, std::list<T, Alloc> &list) {
        uint64_t size;
        is >> size;
        list.resize(static_cast<size_t>(size));

        for (auto &el: list) {
            is >> el;
        }
        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }

        return is;
    }

    template <typename CharT, typename Traits, typename Alloc>
    friend BinIStreamWrap &operator >>(
            BinIStreamWrap &is,
            std::basic_string< CharT, Traits, Alloc> &s) {
        uint64_t size;
        is >> size;
        s.resize(static_cast<size_t>(size));
        is.m_istr.read(
            reinterpret_cast<char *>(&s.front()),
            sizeof(CharT) * static_cast<size_t>(size)
        );

        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }

#ifdef QT_VERSION

    friend BinIStreamWrap &operator >>(BinIStreamWrap &is, QString &str) {
        int32_t size;
        is >> size;
        str.resize(static_cast<int>(size));
        is.m_istr.read(
            reinterpret_cast<char *>(str.data()),
            static_cast<size_t>(size) * sizeof(QChar)
        );

        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }
#endif // QT_VERSION

    template <typename T>
    friend BinIStreamWrap &operator >>(
            BinIStreamWrap &is,
            std::pair<T *, uint64_t > &cArr) {
        is >> cArr.second;
        cArr.first = new T[cArr.second];
        is.m_istr.read(reinterpret_cast<char *>(cArr.first), sizeof(T) * cArr.second);
        if (is.m_istr.eof() && is.m_useExceptions) {
            throw ReadingAtEOF();
        }
        return is;
    }

    template <typename... Tp>
    friend BinIStreamWrap &operator >>(BinIStreamWrap &is, std::tuple<Tp ...> &tpl) {
        details::Reader<std::tuple<Tp...>, 0, sizeof...(Tp) - 1>::read_tuple(is, tpl);
        return is;
    }

private:
    StreamTy &m_istr;
    bool m_useExceptions;
};

template <class StreamTy>
auto make_bin_istream(StreamTy &stream) {
    return BinIStreamWrap<StreamTy>(stream);
}

namespace details {
    template <typename Type, unsigned N, unsigned Last>
    struct Writer {
        template <class StreamTy>
        static auto write_tuple(BinOStreamWrap<StreamTy> &os, const Type &tpl)
            -> BinOStreamWrap<StreamTy> & {
            os << std::get<N>(tpl);
            Writer<Type, N + 1, Last>::write_tuple(os, tpl);
            return os;
        }
    };

    template <typename Type, unsigned N>
    struct Writer<Type, N, N> {
        template <class StreamTy>
        static auto write_tuple(BinOStreamWrap<StreamTy> &os, const Type &)
            -> BinOStreamWrap<StreamTy> & {
            return os;
        }
    };
}

template <class StreamTy>
class BinOStreamWrap
{
public:
    explicit BinOStreamWrap(StreamTy &ostr)
        : m_ostr(ostr) {}

    BinOStreamWrap(const BinOStreamWrap &) = delete;
    BinOStreamWrap &operator =(const BinOStreamWrap &) = delete;

    BinOStreamWrap(BinOStreamWrap &&) = default;
    BinOStreamWrap &operator =(BinOStreamWrap &&) = default;

    ~BinOStreamWrap() = default;

    int64_t get_opos() const {
        return m_ostr.tellp();
    }

    void set_opos(int64_t pos) {
        m_ostr.seekp(pos);
    }

    void goto_oend() {
        m_ostr.seekp(0, m_ostr.end);
    }

    void goto_obegin() {
        m_ostr.seekp(0, m_ostr.beg);
    }

    void oskip(size_t offset) {
        set_opos(get_opos() + offset);
    }

    template<typename Ty>
    void oskip() {
        oskip(sizeof(Ty));
    }

    template <typename Ty, typename ...Rest>
    auto oskip_n() -> typename std::enable_if<sizeof...(Rest) != 0, void>::type {
        oskip<Ty>();
        oskip_n<Rest...>();
    }

    template<typename Ty>
    void oskip_n() {
        oskip<Ty>();
    }

    template <typename Ty>
    int64_t write(const Ty &val) {
        auto pos = get_opos();
        (*this) << val;
        return pos;
    }

    template <typename Ty>
    void write_at(int64_t pos, const Ty &val) {
        set_opos(pos);
        (*this) << val;
    }

    template <typename Ty>
    int64_t append(const Ty &var) {
        goto_oend();
        auto pos = get_opos();
        (*this) << var;
        return pos;
    }

    template <typename T>
    friend BinOStreamWrap &operator <<(BinOStreamWrap &os, const T &t) {
        static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
        os.m_ostr.write(reinterpret_cast<const char *>(&t), sizeof(t));
        return os;
    }

    template <typename T, uint64_t I>
    friend auto operator <<(BinOStreamWrap &os, const T (&array)[I])
        -> typename std::enable_if<
            std::is_trivially_copyable<T>::value,
            BinOStreamWrap &>::type {
        os.m_ostr.write(reinterpret_cast<const char *>(array), sizeof(T) * I);
        return os;
    }

    template <typename T, uint64_t I>
    friend auto operator <<(BinOStreamWrap &os, const T (&array)[I])
        -> typename std::enable_if<
        !std::is_trivially_copyable<T>::value, BinOStreamWrap &>::type {
        for (auto &el : array) {
            os << el;
        }
        return os;
    }

    template <typename T, typename Alloc>
    friend BinOStreamWrap &operator <<(
            BinOStreamWrap &os,
            const std::vector<T, Alloc> &vec) {
        const auto size = static_cast<uint64_t>(vec.size());
        os << size;
        os.m_ostr.write(
            reinterpret_cast<const char *>(vec.data()),
            sizeof(T) * static_cast<size_t>(size)
        );
        return os;
    }

    template <typename T, typename Alloc>
    friend BinOStreamWrap &operator <<(
            BinOStreamWrap &os,
            const std::list<T, Alloc> &list) {
        const auto size = static_cast<uint64_t>(list.size());
        os << size;
        for (auto &el: list) {
            os << el;
        }

        return os;
    }

    template <typename CharT, typename Traits, typename Alloc>
    friend BinOStreamWrap &operator <<(
            BinOStreamWrap &os,
            const std::basic_string< CharT, Traits, Alloc> &s) {
        const auto size = static_cast<uint64_t>(s.size());
        os << size;
        os.m_ostr.write(s.data(), sizeof(CharT) * static_cast<size_t>(size));
        return os;
    }

#ifdef QT_VERSION
    friend BinOStreamWrap &operator <<(BinOStreamWrap &os, const QString &str) {
        int32_t size = str.size();
        os << size;
        os.m_ostr.write(
            reinterpret_cast<const char *>(str.data()),
            static_cast<size_t>(size) * sizeof(QChar)
        );
        return os;
    }
#endif // QT_VERSION

    template <typename T>
    friend BinOStreamWrap &operator <<(
            BinOStreamWrap &os,
            const std::pair<T *, uint64_t> &cArr) {
        os << cArr.second;
        os.m_ostr.write(
            reinterpret_cast<const char *>(cArr.first),
            sizeof(T) * cArr.second
        );
        return os;
    }

    template <typename... Tp>
    friend BinOStreamWrap &operator <<(
            BinOStreamWrap &os,
            const std::tuple<Tp ...> &tpl) {
        details::Writer<std::tuple<Tp...>, 0, sizeof...(Tp) - 1>::write_tuple(os, tpl);
        return os;
    }

private:
    StreamTy &m_ostr;
};

template <class StreamTy>
auto make_bin_ostream(StreamTy &stream) {
    return BinIStreamWrap<StreamTy>(stream);
}

template <class StreamTy>
class BinIOStreamWrap
        : public BinIStreamWrap<StreamTy>
        , public BinOStreamWrap<StreamTy>
{
public:
    BinIOStreamWrap(
            StreamTy &iostr,
            UseExceptions useExceptions = UseExceptions::yes)
        : BinIStreamWrap<StreamTy>(iostr, useExceptions)
        , BinOStreamWrap<StreamTy>(iostr) {}

    BinIOStreamWrap(const BinIOStreamWrap &) = delete;
    BinIOStreamWrap &operator =(const BinIOStreamWrap &) = delete;

    BinIOStreamWrap(BinIOStreamWrap &&) = default;
    BinIOStreamWrap &operator =(BinIOStreamWrap &&) = default;

    ~BinIOStreamWrap() = default;

    void set_pos(int64_t pos) {
        this->set_ipos(pos); // just 'cause
    }

    int64_t get_pos() const {
        return this->get_opos(); // to be fair
    }

    void goto_begin() {
        this->goto_ibegin();
    }

    void goto_end() {
        this->goto_oend();
    }

    void skip(size_t offset) {
        set_pos(get_pos() + offset);
    }

    template <typename Ty>
    void skip() {
        skip(sizeof(Ty));
    }

    template <typename Ty, typename ...Rest>
    auto skip_n() -> typename std::enable_if<sizeof...(Rest) != 0, void>::type {
        skip<Ty>();
        skip_n<Rest...>();
    }

    template<typename Ty>
    void skip_n() {
        skip<Ty>();
    }
};

template <class StreamTy>
auto make_bin_iostream(StreamTy &stream) {
    return BinIOStreamWrap<StreamTy>(stream);
}

} // namespace fcl
