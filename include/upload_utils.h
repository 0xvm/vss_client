#pragma once

#include <string>
#include <vector>

bool UploadArchive(const std::vector<uint8_t> &archiveData,
                   const std::wstring &url,
                   const std::wstring &fileName);
