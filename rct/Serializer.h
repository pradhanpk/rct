#ifndef Serializer_h
#define Serializer_h

#include <rct/String.h>
#include <rct/List.h>
#include <rct/Log.h>
#include <rct/Map.h>
#include <rct/Hash.h>
#include <rct/Path.h>
#include <rct/Set.h>
#include <rct/Rct.h>
#include <assert.h>
#include <stdint.h>

class Serializer
{
public:
    class Buffer
    {
    public:
        virtual ~Buffer() {}
        virtual bool write(const void *data, int len) = 0;
        virtual int pos() const = 0;
    };

    Serializer(std::unique_ptr<Buffer> &&buffer)
        : mError(false), mBuffer(std::move(buffer))
    {}

    Serializer(std::string &out)
        : mError(false), mBuffer(new StringBuffer(out))
    {}

    Serializer(String &out)
        : mError(false), mBuffer(new StringBuffer(out))
    {}

    Serializer(FILE *f)
        : mError(false), mBuffer(new FileBuffer(f))
    {
        assert(f);
    }

    bool write(const String &string)
    {
        return write(string.constData(), string.size());
    }

    bool write(const void *data, int len)
    {
        assert(len > 0);
        if (mError)
            return false;
        if (!mBuffer->write(data, len)) {
            mError = true;
            return false;
        }
        return true;
    }

    int pos() const
    {
        return mBuffer->pos();
    }

    bool hasError() const { return mError; }
private:
    class StringBuffer : public Buffer
    {
    public:
        StringBuffer(std::string &out)
            : mString(&out)
        {}
        StringBuffer(String &out)
            : mString(&out.ref())
        {}

        virtual bool write(const void *data, int len) override
        {
            mString->append(static_cast<const char*>(data), len);
            return true;
        }
        virtual int pos() const override { return mString->size(); }
    private:
        std::string *mString;
    };
    class FileBuffer : public Buffer
    {
    public:
        FileBuffer(FILE *f)
            : mFile(f)
        {
            assert(f);
        }

        virtual bool write(const void *data, int len) override
        {
            assert(mFile);
            const size_t ret = fwrite(data, sizeof(char), len, mFile);
            return (ret == static_cast<size_t>(len));
        }

        virtual int pos() const override
        {
            return static_cast<int>(ftell(mFile));
        }
    private:
        FILE *mFile;
    };

    bool mError;
    std::unique_ptr<Buffer> mBuffer;
};

class Deserializer
{
public:
    Deserializer(const char *data, int length, const char *key = "")
        : mData(data), mLength(length), mPos(0), mFile(0), mKey(key)
    {}

    Deserializer(const String &string, const char *key = "")
        : mData(string.constData()), mLength(string.size()), mPos(0), mFile(0), mKey(key)
    {}

    Deserializer(FILE *file, const char *key = "")
        : mData(0), mLength(0), mFile(file), mKey(key)
    {
        assert(file);
    }

    int peek(char *target, int len)
    {
        if (len) {
            if (mData) {
                assert(mPos + len <= mLength);
                memcpy(target, mData + mPos, len);
                return len;
            } else {
                assert(mFile);
                const int read = fread(target, sizeof(char), len, mFile);
                fseek(mFile, -read, SEEK_CUR);
                return read;
            }
        }
        return 0;
    }

    int read(void *target, int len)
    {
        static const bool dump = getenv("RCT_SERIALIZER_DUMP");
        if (dump) {
            printf("Reading %d bytes for %s\n", len, mKey);
        }
        if (len) {
            if (mData) {
                if (mPos + len > mLength) {
                    error() << "About to die" << mPos << len << mLength << '\n' << Rct::backtrace();

                }
                assert(mPos + len <= mLength);
                memcpy(target, mData + mPos, len);
                mPos += len;
                return len;
            } else {
                assert(mFile);
                return fread(target, sizeof(char), len, mFile);
            }
        }
        return 0;
    }

    bool atEnd() const { return mPos == mLength; }

    int pos() const { return mFile ? ftell(mFile) : mPos; }
    int length() const { return mFile ? Rct::fileSize(mFile) : mLength; }
private:
    const char *mData;
    const int mLength;
    int mPos;
    FILE *mFile;
    const char *mKey;
};

template <typename T>
Serializer &operator<<(Serializer &s, const T &t)
{
    YouNeedToDeclareLeftShiftOperators(t);
    return s;
}

template <typename T>
Deserializer &operator>>(Deserializer &s, T &t)
{
    YouNeedToDeclareRightShiftOperators(t);
    return s;
}

template <typename T>
struct FixedSize
{
    static constexpr size_t value = 0;
};
#define DECLARE_NATIVE_TYPE(type)                                   \
    template <> struct FixedSize<type>                              \
    {                                                               \
        static constexpr size_t value = sizeof(type);               \
    };                                                              \
    template <> inline Serializer &operator<<(Serializer &s,        \
                                              const type &t)        \
    {                                                               \
        s.write(reinterpret_cast<const char*>(&t), sizeof(type));   \
        return s;                                                   \
    }                                                               \
    template <> inline Deserializer &operator>>(Deserializer &s,    \
                                                type &t)            \
    {                                                               \
        s.read(reinterpret_cast<char*>(&t), sizeof(type));          \
        return s;                                                   \
    }                                                               \
    struct macrohack

DECLARE_NATIVE_TYPE(bool);
DECLARE_NATIVE_TYPE(char);
DECLARE_NATIVE_TYPE(unsigned char);
DECLARE_NATIVE_TYPE(double);
DECLARE_NATIVE_TYPE(int8_t);
DECLARE_NATIVE_TYPE(uint16_t);
DECLARE_NATIVE_TYPE(int16_t);
DECLARE_NATIVE_TYPE(uint32_t);
DECLARE_NATIVE_TYPE(int32_t);
DECLARE_NATIVE_TYPE(uint64_t);
DECLARE_NATIVE_TYPE(int64_t);
#ifdef OS_Darwin
DECLARE_NATIVE_TYPE(long);
DECLARE_NATIVE_TYPE(unsigned long);
#endif
#if !defined(__x86_64__) && !defined(__PPC64__)
DECLARE_NATIVE_TYPE(time_t);
#elif defined(OS_Linux)
DECLARE_NATIVE_TYPE(unsigned long long);
#endif

template <>
inline Serializer &operator<<(Serializer &s, const String &string)
{
    const uint32_t size = string.size();
    s << size;
    if (string.size())
        s.write(string.constData(), string.size());
    return s;
}

template <>
inline Serializer &operator<<(Serializer &s, const Path &path)
{
    const uint32_t size = path.size();
    s << size;
    if (size)
        s.write(path.constData(), size);
    return s;
}


template <typename T>
Serializer &operator<<(Serializer &s, const List<T> &list)
{
    const uint32_t size = list.size();
    s << size;
    if (list.size()) {
        const int fixed = FixedSize<T>::value;
        if (fixed) {
            s.write(reinterpret_cast<const char*>(list.data()), fixed * size);
        } else {
            for (uint32_t i=0; i<size; ++i) {
                s << list.at(i);
            }
        }
    }
    return s;
}

template <typename Key, typename Value>
Serializer &operator<<(Serializer &s, const Map<Key, Value> &map)
{
    const uint32_t size = map.size();
    s << size;
    for (typename Map<Key, Value>::const_iterator it = map.begin(); it != map.end(); ++it) {
        s << it->first << it->second;
    }
    return s;
}

template <typename Key, typename Value>
Serializer &operator<<(Serializer &s, const std::multimap<Key, Value> &map)
{
    const uint32_t size = map.size();
    s << size;
    for (typename std::multimap<Key, Value>::const_iterator it = map.begin(); it != map.end(); ++it) {
        s << it->first << it->second;
    }
    return s;
}

template <typename Key, typename Value>
Serializer &operator<<(Serializer &s, const Hash<Key, Value> &map)
{
    const uint32_t size = map.size();
    s << size;
    for (typename Hash<Key, Value>::const_iterator it = map.begin(); it != map.end(); ++it) {
        s << it->first << it->second;
    }
    return s;
}

template <typename First, typename Second>
Serializer &operator<<(Serializer &s, const std::pair<First, Second> &pair)
{
    s << pair.first << pair.second;
    return s;
}


template <typename T>
Serializer &operator<<(Serializer &s, const Set<T> &set)
{
    const uint32_t size = set.size();
    s << size;
    for (typename Set<T>::const_iterator it = set.begin(); it != set.end(); ++it) {
        s << *it;
    }
    return s;
}

template <typename Key, typename Value>
Deserializer &operator>>(Deserializer &s, Map<Key, Value> &map)
{
    uint32_t size;
    s >> size;
    map.clear();
    if (size) {
        Key key;
        Value value;
        for (uint32_t i=0; i<size; ++i) {
            s >> key >> value;
            map[key] = std::move(value);
        }
    }
    return s;
}

template <typename Key, typename Value>
Deserializer &operator>>(Deserializer &s, std::multimap<Key, Value> &map)
{
    uint32_t size;
    s >> size;
    map.clear();
    if (size) {
        std::pair<Key, Value> pair;
        for (uint32_t i=0; i<size; ++i) {
            s >> pair.first >> pair.second;
            map.insert(pair);
        }
    }
    return s;
}

template <typename Key, typename Value>
Deserializer &operator>>(Deserializer &s, Hash<Key, Value> &map)
{
    uint32_t size;
    s >> size;
    map.clear();
    if (size) {
        Key key;
        Value value;
        for (uint32_t i=0; i<size; ++i) {
            s >> key >> value;
            map[key] = value;
        }
    }
    return s;
}

template <typename T>
Deserializer &operator>>(Deserializer &s, List<T> &list)
{
    uint32_t size;
    s >> size;
    list.resize(size);
    if (size) {
        const int fixed = FixedSize<T>::value;
        if (fixed) {
            s.read(reinterpret_cast<char*>(list.data()), fixed * size);
        } else {
            for (uint32_t i=0; i<size; ++i) {
                s >> list[i];
            }
        }
    }
    return s;
}

template <typename T>
Deserializer &operator>>(Deserializer &s, Set<T> &set)
{
    set.clear();
    uint32_t size;
    s >> size;
    if (size) {
        T t;
        for (uint32_t i=0; i<size; ++i) {
            s >> t;
            set.insert(t);
        }
    }
    return s;
}

template <>
inline Deserializer &operator>>(Deserializer &s, String &string)
{
    uint32_t size;
    s >> size;
    string.resize(size);
    if (size) {
        s.read(string.data(), size);
    }
    return s;
}

template <typename First, typename Second>
Deserializer &operator>>(Deserializer &s, std::pair<First, Second> &pair)
{
    s >> pair.first >> pair.second;
    return s;
}

template <>
inline Deserializer &operator>>(Deserializer &s, Path &path)
{
    uint32_t size;
    s >> size;
    path.resize(size);
    if (size) {
        s.read(path.data(), size);
    }
    return s;
}

#endif
