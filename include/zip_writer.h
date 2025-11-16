#pragma once

#include "common.h"

struct ZipEntryMetadata
{
    std::string entryName;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t localHeaderOffset;
};

class ZipWriter
{
public:
    ZipWriter();
    ~ZipWriter();

    bool Open(const std::wstring &path);
    bool OpenMemory(std::vector<uint8_t> &buffer);
    bool AddStoredFile(const std::wstring &sourcePath, const std::string &entryName);
    bool Finalize();
    void Close();
    void EnableXor(uint32_t seed);
    void DisableXor();

    const std::wstring &GetError() const;
    std::wstring GetTargetPath() const;

private:
    bool Write(const void *data, DWORD size);

    HANDLE handle_;
    uint64_t offset_;
    std::vector<ZipEntryMetadata> entries_;
    std::wstring error_;
    std::wstring targetPath_;
    bool xorEnabled_;
    uint32_t xorState_;
    std::vector<uint8_t> xorScratch_;
    std::vector<uint8_t> *memoryBuffer_;
};
