// stream.cpp : serialization functions
//

#include "stream.h"



// OutStream class

void OutStream::initialize()
{
    hash.init();  pos = 0;
}

void OutStream::put_overflow(const char *data, size_t size)
{
    size_t avail = buf.size() - pos;
    while(size > avail)
    {
        std::memcpy(buf.data() + pos, data, avail);
        for(size_t offs = 0; offs < buf.size(); offs += Hash::block_size)
            hash.process_block(buf.data() + offs);

        overflow(buf.data(), buf.size(), false);  pos = 0;
        data += avail;  size -= avail;  avail = buf.size();
    }
    std::memcpy(buf.data() + pos, data, size);  pos += size;
}

void OutStream::finalize()
{
    size_t offs = 0;
    for(; offs + Hash::block_size < pos; offs += Hash::block_size)
        hash.process_block(buf.data() + offs);
    hash.process_last(buf.data() + offs, pos - offs);
    overflow(buf.data(), pos, true);
}



// InStream class

bool InStream::load_buffer()
{
    pos = 0;
    ready = underflow(buf.data(), buf.size());
    if(!ready)return false;

    last = (ready <= buf.size());
    if(last)
    {
        size_t offs = 0;
        for(; offs + Hash::block_size < ready; offs += Hash::block_size)
            hash.process_block(buf.data() + offs);
        hash.process_last(buf.data() + offs, ready - offs);
    }
    else
    {
        ready = buf.size();
        for(size_t offs = 0; offs < buf.size(); offs += Hash::block_size)
            hash.process_block(buf.data() + offs);
    }
    return true;
}

bool InStream::initialize()
{
    hash.init();  return load_buffer();
}

bool InStream::get_underflow(char *data, size_t size)
{
    if(!ready)return false;

    size_t avail = ready - pos;
    while(size > avail)
    {
        if(last)
        {
            pos = ready = 0;  return false;
        }
        std::memcpy(data, buf.data() + pos, avail);
        if(!load_buffer())return false;

        data += avail;  size -= avail;  avail = ready;
    }
    std::memcpy(data, buf.data() + pos, size);  pos += size;  return true;
}

bool InStream::finalize(const void *checksum)
{
    return ready && pos == ready && last &&
        !std::memcmp(hash.result(), checksum, Hash::result_size);
}



// OutFileStream class

bool OutFileStream::error()
{
    std::fclose(file);  file = nullptr;  return false;
}

bool OutFileStream::open(const char *path)
{
    assert(!file);
    file = std::fopen(path, "wb");  if(!file)return false;
    if(std::setvbuf(file, nullptr, _IONBF, 0))return error();
    initialize();  return true;
}

void OutFileStream::overflow(const char *data, size_t size, bool last)
{
    if(!file)return;
    if(std::fwrite(data, 1, size, file) != size || last &&
        std::fwrite(checksum(), 1, Hash::result_size, file) != Hash::result_size)
            error();
}

bool OutFileStream::close()
{
    if(!file)return false;  finalize();
    bool res = file && !std::fclose(file);
    file = nullptr;  return res;
}



// InFileStream class

bool InFileStream::error()
{
    std::fclose(file);  file = nullptr;  return false;
}

bool InFileStream::open(const char *path)
{
    assert(!file);
    file = std::fopen(path, "rb");  if(!file)return false;
    if(std::setvbuf(file, nullptr, _IONBF, 0))return error();
    if(std::fseek(file, -long(Hash::result_size), SEEK_END))return error();
    total_size = std::ftell(file);  if(total_size <= 0)return error();
    if(std::fread(checksum, 1, Hash::result_size, file) != Hash::result_size)return error();
    std::rewind(file);  initialize();  return true;
}

size_t InFileStream::underflow(char *data, size_t size)
{
    if(!file)return 0;
    size_t left = total_size, n = std::min(size, left);
    if(std::fread(data, 1, n, file) != n)return error();
    total_size -= n;  return left;
}

bool InFileStream::close()
{
    if(!file)return false;
    bool res = !std::fclose(file) && finalize(checksum);
    file = nullptr;  return res;
}
