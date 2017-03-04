// stream.h : serialization functions
//

#pragma once

#include "hash.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>



class StreamAlign
{
    unsigned mask;

    StreamAlign(unsigned n) : mask(n - 1)
    {
        assert(!(n & mask) && mask < Hash::block_size);
    }

    friend StreamAlign align(unsigned n);
    friend class OutStream;
    friend class InStream;
};

inline StreamAlign align(unsigned n)
{
    return StreamAlign(n);
}



class OutStream
{
    Hash hash;
    std::vector<char> buf;
    size_t pos;


    void put_overflow(const char *data, size_t size);

protected:
    virtual void overflow(const char *data, size_t size, bool last)
    {
    }

public:
    explicit OutStream(size_t size = 1ul << 16) : buf(size), pos(0)
    {
        assert(size / Hash::block_size * Hash::block_size == size);
    }

    virtual ~OutStream()
    {
    }

    void initialize();
    void finalize();


    void put(const void *data, size_t size)
    {
        if(size > buf.size() - pos)return put_overflow(static_cast<const char *>(data), size);
        std::memcpy(buf.data() + pos, data, size);  pos += size;
    }


    void assert_align(unsigned n)
    {
        assert(!(pos & StreamAlign(n).mask));
    }

    OutStream &operator << (const StreamAlign &align)
    {
        unsigned tail = unsigned(-pos) & align.mask;
        std::memset(buf.data() + pos, 0, tail);
        pos += tail;  return *this;
    }


    OutStream &operator << (uint8_t val)
    {
        put(&val, sizeof(val));  return *this;
    }

    OutStream &operator << (uint16_t val)
    {
        val = to_le16(val);  put(&val, sizeof(val));  return *this;
    }

    OutStream &operator << (uint32_t val)
    {
        val = to_le32(val);  put(&val, sizeof(val));  return *this;
    }

    OutStream &operator << (uint64_t val)
    {
        val = to_le64(val);  put(&val, sizeof(val));  return *this;
    }


    OutStream &operator << (int8_t val)
    {
        return *this << uint8_t(val);
    }

    OutStream &operator << (int16_t val)
    {
        return *this << uint16_t(val);
    }

    OutStream &operator << (int32_t val)
    {
        return *this << uint32_t(val);
    }

    OutStream &operator << (int64_t val)
    {
        return *this << uint64_t(val);
    }


    template<typename T> OutStream &operator << (const T &obj)
    {
        obj.save(*this);  return *this;
    }


    const void *checksum() const
    {
        return hash.result();
    }
};



class InStream
{
    Hash hash;
    std::vector<char> buf;
    size_t pos, ready;
    bool last;


    bool load_buffer();
    bool get_underflow(char *data, size_t size);

protected:
    virtual size_t underflow(char *data, size_t size) = 0;

public:
    explicit InStream(size_t size = 1ul << 16) : buf(size), pos(0), ready(0)
    {
        assert(size / Hash::block_size * Hash::block_size == size);
    }

    virtual ~InStream()
    {
    }

    bool initialize();
    bool finalize(const void *checksum);


    bool get(void *data, size_t size)
    {
        if(size > ready - pos)return get_underflow(static_cast<char *>(data), size);
        std::memcpy(data, buf.data() + pos, size);  pos += size;  return true;
    }


    void assert_align(unsigned n)
    {
        assert(!(pos & StreamAlign(n).mask));
    }

    InStream &operator >> (const StreamAlign &align)
    {
        static const char zero[Hash::block_size] = {};

        unsigned tail = unsigned(-pos) & align.mask;
        if(tail > ready - pos || std::memcmp(buf.data() + pos, zero, tail))pos = ready = 0;
        else pos += tail;  return *this;
    }


    InStream &operator >> (uint8_t &val)
    {
        get(&val, sizeof(val));  return *this;
    }

    InStream &operator >> (uint16_t &val)
    {
        get(&val, sizeof(val));  val = to_le16(val);  return *this;
    }

    InStream &operator >> (uint32_t &val)
    {
        get(&val, sizeof(val));  val = to_le32(val);  return *this;
    }

    InStream &operator >> (uint64_t &val)
    {
        get(&val, sizeof(val));  val = to_le64(val);  return *this;
    }


    InStream &operator >> (int8_t val)
    {
        return *this >> reinterpret_cast<uint8_t &>(val);
    }

    InStream &operator << (int16_t val)
    {
        return *this >> reinterpret_cast<uint16_t &>(val);
    }

    InStream &operator << (int32_t val)
    {
        return *this >> reinterpret_cast<uint32_t &>(val);
    }

    InStream &operator >> (int64_t val)
    {
        return *this >> reinterpret_cast<uint64_t &>(val);
    }


    template<typename T> InStream &operator >> (T &obj)
    {
        if(!obj.load(*this))pos = ready = 0;  return *this;
    }


    operator bool () const
    {
        return ready;
    }

    bool operator ! () const
    {
        return !ready;
    }
};



class OutFileStream : public OutStream
{
    FILE *file;

    bool error();

protected:
    void overflow(const char *data, size_t size, bool last) final;

public:
    explicit OutFileStream(size_t size = 1ul << 16) : OutStream(size), file(nullptr)
    {
    }

    ~OutFileStream()
    {
        assert(!file);
    }

    bool open(const char *path);
    bool close();
};


class InFileStream : public InStream
{
    FILE *file;
    long total_size;
    char checksum[Hash::result_size];

    bool error();

protected:
    size_t underflow(char *data, size_t size) final;

public:
    explicit InFileStream(size_t size = 1ul << 16) : InStream(size), file(nullptr)
    {
    }

    ~InFileStream()
    {
        assert(!file);
    }

    bool open(const char *path);
    bool close();
};
