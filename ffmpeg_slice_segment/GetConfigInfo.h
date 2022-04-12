#pragma once

#include <string>

// Get config of record.
// @return 0 if success, negative if failure, positive if cancelled..
int GetRecordConfig(std::string &rtspurl, int &useTcp, int &fileTimeLength,
    std::string &dstDir, int &expireMinutes);
