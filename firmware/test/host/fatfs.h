// Host stand-in for FatFs: an SD card in memory, just enough for SceneStore.h. The card can be
// taken out (card.present), filled and read back (card.files, keyed by full path), and made to
// refuse writes (card.read_only).
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

typedef unsigned int UINT;
typedef unsigned char BYTE;

enum FRESULT
{
    FR_OK = 0,
    FR_DISK_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_EXIST,
    FR_DENIED,
};

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

struct FATFS
{
};
struct FILINFO
{
};
struct FIL
{
    std::string path;
    size_t fptr = 0;
    bool write = false;
};
#define f_eof(fp) ((int)((fp)->fptr == FakeCard::Get().files[(fp)->path].size()))

struct FakeCard
{
    bool present = true;
    bool read_only = false;
    std::string cwd = "";
    std::map<std::string, std::string> files;
    std::map<std::string, bool> dirs;

    static FakeCard& Get()
    {
        static FakeCard card;
        return card;
    }
    std::string Path(const char* name) const
    {
        return name[0] == '/' ? std::string(name) : cwd + "/" + name;
    }
};

inline FRESULT f_mount(FATFS*, const char*, BYTE)
{
    FakeCard& c = FakeCard::Get();
    c.cwd = "";
    return c.present ? FR_OK : FR_NOT_READY;
}
inline FRESULT f_chdir(const char* path)
{
    FakeCard& c = FakeCard::Get();
    if (!c.present || !c.dirs.count(path))
        return FR_NO_FILE;
    c.cwd = path;
    return FR_OK;
}
inline FRESULT f_mkdir(const char* path)
{
    FakeCard& c = FakeCard::Get();
    if (!c.present || c.read_only)
        return FR_DENIED;
    c.dirs[path] = true;
    return FR_OK;
}
inline FRESULT f_stat(const char* name, FILINFO*)
{
    FakeCard& c = FakeCard::Get();
    return c.present && c.files.count(c.Path(name)) ? FR_OK : FR_NO_FILE;
}
inline FRESULT f_unlink(const char* name)
{
    FakeCard& c = FakeCard::Get();
    if (!c.present)
        return FR_NOT_READY;
    if (!c.files.count(c.Path(name)))
        return FR_NO_FILE;
    if (c.read_only)
        return FR_DENIED;
    c.files.erase(c.Path(name));
    return FR_OK;
}
inline FRESULT f_rename(const char* from, const char* to)
{
    FakeCard& c = FakeCard::Get();
    if (!c.present)
        return FR_NOT_READY;
    if (!c.files.count(c.Path(from)))
        return FR_NO_FILE;
    if (c.files.count(c.Path(to)))
        return FR_EXIST;
    if (c.read_only)
        return FR_DENIED;
    c.files[c.Path(to)] = c.files[c.Path(from)];
    c.files.erase(c.Path(from));
    return FR_OK;
}
inline FRESULT f_open(FIL* f, const char* name, BYTE mode)
{
    FakeCard& c = FakeCard::Get();
    if (!c.present)
        return FR_NOT_READY;
    f->path = c.Path(name);
    f->fptr = 0;
    f->write = mode & FA_WRITE;
    if (f->write)
    {
        if (c.read_only)
            return FR_DENIED;
        c.files[f->path].clear();
        return FR_OK;
    }
    return c.files.count(f->path) ? FR_OK : FR_NO_FILE;
}
inline FRESULT f_read(FIL* f, void* buf, UINT n, UINT* read)
{
    const std::string& data = FakeCard::Get().files[f->path];
    const size_t len = std::min<size_t>(n, data.size() - f->fptr);
    memcpy(buf, data.data() + f->fptr, len);
    f->fptr += len;
    *read = static_cast<UINT>(len);
    return FR_OK;
}
inline FRESULT f_write(FIL* f, const void* buf, UINT n, UINT* written)
{
    FakeCard::Get().files[f->path].append(static_cast<const char*>(buf), n);
    f->fptr += n;
    *written = n;
    return FR_OK;
}
inline FRESULT f_close(FIL*) { return FR_OK; }
