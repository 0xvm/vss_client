#include "zip_writer.h"
#include <cstring>

namespace
{
uint32_t g_crc32Table[256];
bool g_crcInitialized = false;

void InitializeCrc32Table()
{
    if (g_crcInitialized)
        return;

    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        g_crc32Table[i] = crc;
    }
    g_crcInitialized = true;
}

uint32_t Crc32Advance(uint32_t crc, const uint8_t *data, size_t length)
{
    InitializeCrc32Table();
    for (size_t i = 0; i < length; ++i)
    {
        uint8_t index = static_cast<uint8_t>((crc ^ data[i]) & 0xFFu);
        crc = (crc >> 8) ^ g_crc32Table[index];
    }
    return crc;
}

std::wstring FormatErrorMessage(const wchar_t *prefix, DWORD err)
{
    wchar_t buffer[256];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ls (error %lu)", prefix, err);
    return buffer;
}

#pragma pack(push, 1)
struct ZipLocalFileHeader
{
    uint32_t signature;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLength;
    uint16_t extraFieldLength;
};

struct ZipCentralDirectoryHeader
{
    uint32_t signature;
    uint16_t versionMade;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLength;
    uint16_t extraFieldLength;
    uint16_t fileCommentLength;
    uint16_t diskNumberStart;
    uint16_t internalAttributes;
    uint32_t externalAttributes;
    uint32_t localHeaderOffset;
};

struct ZipEndOfCentralDirectory
{
    uint32_t signature;
    uint16_t diskNumber;
    uint16_t diskWithCentralDirectory;
    uint16_t entryCountDisk;
    uint16_t entryCountTotal;
    uint32_t centralDirectorySize;
    uint32_t centralDirectoryOffset;
    uint16_t commentLength;
};
#pragma pack(pop)
} // namespace

ZipWriter::ZipWriter()
    : handle_(INVALID_HANDLE_VALUE),
      offset_(0),
      xorEnabled_(false),
      xorState_(0),
      memoryBuffer_(nullptr)
{
}

ZipWriter::~ZipWriter()
{
    Close();
}

bool ZipWriter::Open(const std::wstring &path)
{
    Close();
    DisableXor();
    memoryBuffer_ = nullptr;
    handle_ = CreateFileW(path.c_str(),
                          GENERIC_WRITE,
                          0,
                          NULL,
                          CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    if (handle_ == INVALID_HANDLE_VALUE)
    {
        error_ = FormatErrorMessage(L"CreateFileW failed", GetLastError());
        return false;
    }
    offset_ = 0;
    entries_.clear();
    targetPath_ = path;
    return true;
}

bool ZipWriter::OpenMemory(std::vector<uint8_t> &buffer)
{
    Close();
    DisableXor();
    memoryBuffer_ = &buffer;
    memoryBuffer_->clear();
    handle_ = INVALID_HANDLE_VALUE;
    offset_ = 0;
    entries_.clear();
    targetPath_.clear();
    error_.clear();
    return true;
}

void ZipWriter::EnableXor(uint32_t seed)
{
    xorEnabled_ = true;
    xorState_ = seed;
}

void ZipWriter::DisableXor()
{
    xorEnabled_ = false;
    xorState_ = 0;
    xorScratch_.clear();
}

bool ZipWriter::AddStoredFile(const std::wstring &sourcePath, const std::string &entryName)
{
    if (handle_ == INVALID_HANDLE_VALUE && memoryBuffer_ == nullptr)
    {
        error_ = L"Archive not opened";
        return false;
    }
    if (entryName.size() >= 0xFFFF)
    {
        error_ = L"File name too long for ZIP";
        return false;
    }

    HANDLE src = CreateFileW(sourcePath.c_str(),
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             NULL);
    if (src == INVALID_HANDLE_VALUE)
    {
        error_ = FormatErrorMessage(L"CreateFileW (source) failed", GetLastError());
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(src, &fileSize))
    {
        error_ = FormatErrorMessage(L"GetFileSizeEx failed", GetLastError());
        CloseHandle(src);
        return false;
    }
    if (fileSize.QuadPart > 0xFFFFFFFFull)
    {
        error_ = L"Files larger than 4GB are not supported";
        CloseHandle(src);
        return false;
    }

    ZipEntryMetadata meta = {};
    meta.entryName = entryName;
    meta.localHeaderOffset = static_cast<uint32_t>(offset_);

    std::vector<uint8_t> raw(static_cast<size_t>(fileSize.QuadPart));
    DWORD totalRead = 0;
    while (totalRead < raw.size())
    {
        DWORD bytesRead = 0;
        if (!ReadFile(src, raw.data() + totalRead, static_cast<DWORD>(raw.size() - totalRead), &bytesRead, NULL))
        {
            error_ = FormatErrorMessage(L"ReadFile failed", GetLastError());
            CloseHandle(src);
            return false;
        }
        if (bytesRead == 0)
            break;
        totalRead += bytesRead;
    }
    CloseHandle(src);
    if (totalRead != raw.size())
    {
        error_ = L"Unexpected EOF while reading file";
        return false;
    }

    uint32_t crc = 0xFFFFFFFFu;
    crc = Crc32Advance(crc, raw.data(), raw.size());
    crc ^= 0xFFFFFFFFu;

    if (raw.size() > 0xFFFFFFFFull)
    {
        error_ = L"File exceeds ZIP32 limits";
        return false;
    }

    meta.crc32 = crc;
    meta.uncompressedSize = static_cast<uint32_t>(raw.size());
    meta.compressedSize = static_cast<uint32_t>(raw.size());

    ZipLocalFileHeader localHeader = {};
    localHeader.signature = 0x04034b50;
    localHeader.versionNeeded = 20;
    localHeader.flags = 0;
    localHeader.compression = 0;
    localHeader.modTime = 0;
    localHeader.modDate = 0;
    localHeader.crc32 = meta.crc32;
    localHeader.compressedSize = meta.compressedSize;
    localHeader.uncompressedSize = meta.uncompressedSize;
    localHeader.fileNameLength = static_cast<uint16_t>(entryName.size());
    localHeader.extraFieldLength = 0;

    if (!Write(&localHeader, sizeof(localHeader)) ||
        !Write(entryName.data(), static_cast<DWORD>(entryName.size())) ||
        !Write(raw.data(), static_cast<DWORD>(raw.size())))
    {
        return false;
    }

    entries_.push_back(meta);
    return true;
}

bool ZipWriter::Finalize()
{
    if (handle_ == INVALID_HANDLE_VALUE && memoryBuffer_ == nullptr)
    {
        error_ = L"Archive not opened";
        return false;
    }
    if (entries_.size() > 0xFFFF)
    {
        error_ = L"ZIP archives with >65535 files are not supported";
        return false;
    }

    uint32_t centralDirectoryOffset = static_cast<uint32_t>(offset_);

    for (const auto &entry : entries_)
    {
        ZipCentralDirectoryHeader header = {};
        header.signature = 0x02014b50;
        header.versionMade = 20;
        header.versionNeeded = 20;
        header.flags = 0;
        header.compression = 0;
        header.modTime = 0;
        header.modDate = 0;
        header.crc32 = entry.crc32;
        header.compressedSize = entry.compressedSize;
        header.uncompressedSize = entry.uncompressedSize;
        header.fileNameLength = static_cast<uint16_t>(entry.entryName.size());
        header.extraFieldLength = 0;
        header.fileCommentLength = 0;
        header.diskNumberStart = 0;
        header.internalAttributes = 0;
        header.externalAttributes = 0;
        header.localHeaderOffset = entry.localHeaderOffset;

        if (!Write(&header, sizeof(header)) ||
            !Write(entry.entryName.data(), static_cast<DWORD>(entry.entryName.size())))
            return false;
    }

    uint32_t centralDirectorySize = static_cast<uint32_t>(offset_) - centralDirectoryOffset;

    ZipEndOfCentralDirectory footer = {};
    footer.signature = 0x06054b50;
    footer.diskNumber = 0;
    footer.diskWithCentralDirectory = 0;
    footer.entryCountDisk = static_cast<uint16_t>(entries_.size());
    footer.entryCountTotal = static_cast<uint16_t>(entries_.size());
    footer.centralDirectorySize = centralDirectorySize;
    footer.centralDirectoryOffset = centralDirectoryOffset;
    footer.commentLength = 0;

    if (!Write(&footer, sizeof(footer)))
        return false;

    return true;
}

void ZipWriter::Close()
{
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    memoryBuffer_ = nullptr;
}

const std::wstring &ZipWriter::GetError() const
{
    return error_;
}

std::wstring ZipWriter::GetTargetPath() const
{
    return targetPath_;
}

bool ZipWriter::Write(const void *data, DWORD size)
{
    if (size == 0)
        return true;

    const uint8_t *cursor = static_cast<const uint8_t *>(data);
    const DWORD maxChunk = 1 << 16;
    DWORD remaining = size;
    while (remaining > 0)
    {
        DWORD chunk = remaining;
        if (chunk > maxChunk)
            chunk = maxChunk;

        const uint8_t *chunkData = cursor;
        if (xorEnabled_)
        {
            if (xorScratch_.size() < chunk)
                xorScratch_.resize(chunk);
            std::memcpy(xorScratch_.data(), cursor, chunk);
            for (DWORD idx = 0; idx < chunk; ++idx)
            {
                xorState_ = 214013u * xorState_ + 2531011u;
                xorScratch_[idx] ^= static_cast<uint8_t>(xorState_ >> 24);
            }
            chunkData = xorScratch_.data();
        }

        if (memoryBuffer_)
        {
            memoryBuffer_->insert(memoryBuffer_->end(), chunkData, chunkData + chunk);
        }
        else
        {
            DWORD written = 0;
            if (!WriteFile(handle_, chunkData, chunk, &written, NULL))
            {
                error_ = FormatErrorMessage(L"WriteFile failed", GetLastError());
                return false;
            }
            if (written != chunk)
            {
                error_ = L"WriteFile wrote fewer bytes than expected";
                return false;
            }
        }

        cursor += chunk;
        remaining -= chunk;
        offset_ += chunk;
        if (offset_ > 0xFFFFFFFFull)
        {
            error_ = L"ZIP64 archives are not supported";
            return false;
        }
    }

    return true;
}
