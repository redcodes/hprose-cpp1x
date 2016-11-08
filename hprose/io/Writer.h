/**********************************************************\
|                                                          |
|                          hprose                          |
|                                                          |
| Official WebSite: http://www.hprose.com/                 |
|                   http://www.hprose.org/                 |
|                                                          |
\**********************************************************/

/**********************************************************\
 *                                                        *
 * hprose/io/Writer.h                                     *
 *                                                        *
 * hprose writer header for cpp.                          *
 *                                                        *
 * LastModified: Nov 8, 2016                              *
 * Author: Chen fei <cf@hprose.com>                       *
 *                                                        *
\**********************************************************/

#pragma once

#include <hprose/io/Tags.h>
#include <hprose/io/ClassManager.h>
#include <hprose/util/Util.h>

#include <ostream>
#include <numeric>
#include <limits>
#include <complex>
#include <ratio>
#include <string>
#include <locale>
#include <codecvt>
#include <chrono>
#include <array>
#include <vector>
#include <forward_list>
#include <tuple>
#include <bitset>
#include <unordered_map>
#include <type_traits>
#include <typeindex>

namespace hprose {
namespace io {

namespace internal {

class WriterRefer {
public:
    inline void addCount(int count) {
        lastref += count;
    }

    inline void set(uintptr_t ptr) {
        ref[ptr] = lastref++;
    }

    bool write(std::ostream &stream, uintptr_t ptr);

    inline void reset() {
        ref.clear();
        lastref = 0;
    }

private:
    std::unordered_map<uintptr_t, int> ref;
    int lastref;
};

} // hprose::io::internal

class Writer {
public:
    Writer(std::ostream &stream, bool simple = false)
        : stream(stream), refer(simple ? nullptr : new internal::WriterRefer()) {
    }

    template<class T>
    inline Writer &serialize(const T &v) {
        writeValue(v);
        return *this;
    }

    template<class T>
    inline void writeValue(const T &v) {
        encode(v, *this);
    }

    inline void writeNull() {
        stream << tags::TagNull;
    }

    inline void writeBool(bool b) {
        stream << (b ? tags::TagTrue : tags::TagFalse);
    }

    template<class T>
    typename std::enable_if<
        (std::is_integral<T>::value &&
         std::is_signed<T>::value &&
         sizeof(T) <= sizeof(int32_t))
    >::type
    writeInteger(T i) {
        if (i >= 0 && i <= 9) {
            stream << static_cast<char>('0' + i);
            return;
        }
        stream << tags::TagInteger;
        util::WriteInt(stream, i);
        stream << tags::TagSemicolon;
    }

    template<class T>
    typename std::enable_if<
        (std::is_integral<T>::value &&
         std::is_signed<T>::value &&
         sizeof(T) > sizeof(int32_t))
    >::type
    writeInteger(T i) {
        if (i >= 0 && i <= 9) {
            stream << static_cast<char>('0' + i);
            return;
        }
        if (i >= std::numeric_limits<int32_t>::min() && i <= std::numeric_limits<int32_t>::max()) {
            stream << tags::TagInteger;
        } else {
            stream << tags::TagLong;
        }
        util::WriteInt(stream, i);
        stream << tags::TagSemicolon;
    }

    template<class T>
    typename std::enable_if<
        (std::is_integral<T>::value &&
         std::is_unsigned<T>::value &&
         sizeof(T) <= sizeof(uint16_t))
    >::type
    writeInteger(T u) {
        if (u <= 9) {
            stream << static_cast<char>('0' + u);
            return;
        }
        stream << tags::TagInteger;
        util::WriteUint(stream, u);
        stream << tags::TagSemicolon;
    }

    template<class T>
    typename std::enable_if<
        (std::is_integral<T>::value &&
         std::is_unsigned<T>::value &&
         sizeof(T) > sizeof(uint16_t))
    >::type
    writeInteger(T u) {
        if (u <= 9) {
            stream << static_cast<char>('0' + u);
            return;
        }
        if (u <= std::numeric_limits<int32_t>::max()) {
            stream << tags::TagInteger;
        } else {
            stream << tags::TagLong;
        }
        util::WriteUint(stream, u);
        stream << tags::TagSemicolon;
    }

    template<class T>
    typename std::enable_if<
        std::is_floating_point<T>::value
    >::type
    writeFloat(T f) {
        if (f != f) {
            stream << tags::TagNaN;
        } else if (f == std::numeric_limits<T>::infinity()) {
            stream << tags::TagInfinity << tags::TagPos;
        } else if (f == -std::numeric_limits<T>::infinity()) {
            stream << tags::TagInfinity << tags::TagNeg;
        } else {
            stream.precision(std::numeric_limits<T>::digits10);
            stream << tags::TagDouble << f << tags::TagSemicolon;
        }
    }

    template<class T>
    void writeComplex(const std::complex<T> &c) {
        if (c.imag() == 0) {
            writeFloat(c.real());
            return;
        }
        setRef(0);
        writeListHeader(2);
        writeFloat(c.real());
        writeFloat(c.imag());
        writeListFooter();
    }

    template<intmax_t N, intmax_t D>
    void writeRatio(const std::ratio<N, D> &r) {
        if (r.den == 1) {
            writeInteger(r.num);
            return;
        }
        setRef(0);
        std::string s = std::to_string(r.num) + "/" + std::to_string(r.den);
        writeString(s, s.length());
    }

    void writeString(const std::string &str) {
        int length = util::UTF16Length(str);
        if (length == 0) {
            stream << tags::TagEmpty;
        } else if (length == 1) {
            stream << tags::TagUTF8Char << str;
        } else if (length < 0) {
            writeBytes(reinterpret_cast<const uint8_t *>(str.data()), str.length());
        } else {
            if (writeRef(reinterpret_cast<uintptr_t>(&str))) {
                return;
            }
            setRef(reinterpret_cast<uintptr_t>(&str));
            writeString(str, length);
        }
    }

    void writeString(const std::wstring &str) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        writeString(conv.to_bytes(str));
    }

    void writeString(const std::u16string &str) {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
        writeString(conv.to_bytes(str));
    }

    void writeString(const std::u32string &str) {
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
        writeString(conv.to_bytes(str));
    }

    void writeBytes(const uint8_t *data, size_t count) {
        setRef(0);
        if (count == 0) {
            stream << tags::TagBytes << tags::TagQuote << tags::TagQuote;
            return;
        }
        stream << tags::TagBytes;
        util::WriteUint(stream, count);
        stream << tags::TagQuote;
        stream.write(reinterpret_cast<const char *>(data), count);
        stream << tags::TagQuote;
    }

    void writeTime(const std::tm &t) {
        if (writeRef(reinterpret_cast<uintptr_t>(&t))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&t));
        if (t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0) {
            writeDate(1900 + t.tm_year, t.tm_mon + 1, t.tm_mday);
        } else if (t.tm_year == 70 && t.tm_mon == 0 && t.tm_mday == 1) {
            writeTime(t.tm_hour, t.tm_min, t.tm_sec, 0);
        } else {
            writeDate(1900 + t.tm_year, t.tm_mon + 1, t.tm_mday);
            writeTime(t.tm_hour, t.tm_min, t.tm_sec, 0);
        }
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
        stream << tags::TagSemicolon;
#else
        stream << (t.tm_gmtoff == 0 ? tags::TagUTC : tags::TagSemicolon);
#endif
    }

    template<class Clock, class Duration>
    void writeTime(const std::chrono::time_point<Clock, Duration> &t) {

    }

    template<class T>
    void writeList(const T &lst) {
        if (writeRef(reinterpret_cast<uintptr_t>(&lst))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&lst));
        size_t count = lst.size();
        if (count == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(count);
        for (auto &&e : lst) {
            writeValue(e);
        }
        writeListFooter();
    }

    template<size_t N>
    inline void writeList(const uint8_t (&v)[N]) {
        writeBytes(v, N);
    }

    template<size_t N>
    inline void writeList(const std::array<uint8_t, N> &a) {
        writeBytes(a.data(), a.size());
    }

    template<class Allocator>
    inline void writeList(const std::vector<uint8_t, Allocator> &v) {
        writeBytes(v.data(), v.size());
    }

    template<class T, size_t N>
    void writeList(const T (&v)[N]) {
        setRef(0);
        if (N == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(N);
        for (auto i = 0; i < N; ++i) {
            writeValue(v[i]);
        }
        writeListFooter();
    }

    template<class Allocator>
    void writeList(const std::vector<bool, Allocator> &lst) {
        if (writeRef(reinterpret_cast<uintptr_t>(&lst))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&lst));
        size_t count = lst.size();
        if (count == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(count);
        for (auto &&e : lst) {
            writeBool(e);
        }
        writeListFooter();
    }

    template<class T>
    void writeList(const std::forward_list<T> &lst) {
        if (writeRef(reinterpret_cast<uintptr_t>(&lst))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&lst));
        size_t count = std::distance(std::begin(lst), std::end(lst));
        if (count == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(count);
        for (auto &&e : lst) {
            writeValue(e);
        }
        writeListFooter();
    }

    template<size_t N>
    void writeList(const std::bitset<N> &b) {
        if (writeRef(reinterpret_cast<uintptr_t>(&b))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&b));
        if (N == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(N);
        for (auto i = 0; i < N; ++i) {
            writeBool(b.test(i));
        }
        writeListFooter();
    }

    template<class... Type>
    void writeList(const std::tuple<Type...> &lst) {
        if (writeRef(reinterpret_cast<uintptr_t>(&lst))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&lst));
        size_t count = std::tuple_size<std::tuple<Type...> >::value;
        if (count == 0) {
            writeEmptyList();
            return;
        }
        writeListHeader(count);
        writeTupleElement(lst);
        writeListFooter();
    }

    template<class T>
    void writeMap(const T &map) {
        if (writeRef(reinterpret_cast<uintptr_t>(&map))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&map));
        size_t count = map.size();
        if (count == 0) {
            writeEmptyMap();
            return;
        }
        writeMapHeader(count);
        for (auto &&e : map) {
            writeValue(e.first);
            writeValue(e.second);
        }
        writeMapFooter();
    }

    template<class T>
    void writeObject(const T &o) {
        const auto &cache = ClassManager::SharedInstance().getClassCache<T>();
        if (!classRefs) {
            classRefs = std::unique_ptr<std::unordered_map<std::type_index, int> >(
                new std::unordered_map<std::type_index, int>()
            );
        }
        auto type = std::type_index(typeid(T));
        auto iter = classRefs->find(type);
        auto index = 0;
        if (iter == classRefs->end()) {
            stream << cache.data;
            if (refer) {
                refer->addCount(cache.fields.size());
            }
            index = classRefs->size();
            (*classRefs)[type] = index;
        } else {
            index = iter->second;
        }
        if (writeRef(reinterpret_cast<uintptr_t>(&o))) {
            return;
        }
        setRef(reinterpret_cast<uintptr_t>(&o));
        stream << tags::TagObject << index << tags::TagOpenbrace;
        auto fields = cache.fields;
        for (auto &&field : fields) {
            field.encode(&o, *this);
        }
        stream << tags::TagClosebrace;
    }

    inline bool writeRef(uintptr_t ptr) {
        return refer ? refer->write(stream, ptr) : false;
    }

    inline void setRef(uintptr_t ptr) {
        if (refer) refer->set(ptr);
    }

    inline void reset() {
        if (refer) refer->reset();
    }

    std::ostream &stream;

private:
    void writeString(const std::string &str, int length) {
        stream << tags::TagString;
        util::WriteInt(stream, length);
        stream << tags::TagQuote << str << tags::TagQuote;
    }

    inline void writeDate(int year, int month, int day) {
        stream << tags::TagDate;
        util::WriteDate(stream, year, month, day);
    }

    inline void writeTime(int hour, int min, int sec, int nsec) {
        stream << tags::TagTime;
        util::WriteTime(stream, hour, min, sec);
    }

    void writeListHeader(size_t count) {
        stream << tags::TagList;
        util::WriteUint(stream, count);
        stream << tags::TagOpenbrace;
    }

    inline void writeListFooter() {
        stream << tags::TagClosebrace;
    }

    inline void writeEmptyList() {
        stream << tags::TagList << tags::TagOpenbrace << tags::TagClosebrace;
    }

    template<std::size_t Index = 0, class... Tuple>
    inline typename std::enable_if<
        Index == sizeof...(Tuple)
    >::type
    writeTupleElement(const std::tuple<Tuple...> &) {
    }

    template<std::size_t Index = 0, class... Tuple>
    inline typename std::enable_if<
        Index < sizeof...(Tuple)
    >::type
    writeTupleElement(const std::tuple<Tuple...> &tuple) {
        writeValue(std::get<Index>(tuple));
        writeTupleElement<Index + 1, Tuple...>(tuple);
    }

    void writeMapHeader(size_t count) {
        stream << tags::TagMap;
        util::WriteUint(stream, count);
        stream << tags::TagOpenbrace;
    }

    inline void writeMapFooter() {
        stream << tags::TagClosebrace;
    }

    inline void writeEmptyMap() {
        stream << tags::TagMap << tags::TagOpenbrace << tags::TagClosebrace;
    }

    std::unique_ptr<internal::WriterRefer> refer;
    std::unique_ptr<std::unordered_map<std::type_index, int> > classRefs;
};

}
} // hprose::io

#include <hprose/io/Writer-inl.h>
