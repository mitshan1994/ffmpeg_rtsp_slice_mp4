#include "stdafx.h"
#include "GetConfigInfo.h"
#include "httplib.h"
#include "MyLog.h"
#include "rapidjson/document.h"

using namespace std;
using namespace httplib;
using namespace rapidjson;

extern CMyLog g_log;

int GetRecordConfig(string& rtspurl, int& useTcp, int& fileTimeLength, 
    string& dstDir, int& expireMinutes)
{
    char szValue[256];

    // ¶ÁÈ¡ÅäÖÃÎÄ¼þ
    const char* szConfigFile = "./ffmpeg_slice_segment.ini";
    const char* szAppname = "CONFIG";
    const char* szKeyRtspurl = "rtspurl";
    const char* szKeyFileSeconds = "file_time_length";
    const char* szKeyDestDir = "dest_dir";
    const char* szKeyExpire = "expire";
    const char* szUseRtspTcp = "use_tcp";

    const char* szUseHttpConfig = "use_http_config";
    const char* szHttpIp = "http_ip";
    const char* szHttpPort = "http_port";
    const char* szHttpRequest = "http_request";

    GetPrivateProfileStringA(szAppname, szUseHttpConfig, "0", szValue, sizeof(szValue), szConfigFile);

    if (strcmp(szValue, "0") == 0) {
        g_log.Add("NOT use http request to get json. We read file to get config.");

        fileTimeLength = GetPrivateProfileIntA(szAppname, szKeyFileSeconds, 6, szConfigFile);
        expireMinutes = GetPrivateProfileIntA(szAppname, szKeyExpire, 12, szConfigFile);
        GetPrivateProfileStringA(szAppname, szKeyRtspurl, "", szValue, sizeof(szValue), szConfigFile);
        rtspurl = szValue;
        GetPrivateProfileStringA(szAppname, szKeyDestDir, "C:\\rukoujuchao_mp4", szValue, sizeof(szValue), szConfigFile);
        dstDir = szValue;

        useTcp = GetPrivateProfileIntA(szAppname, szUseRtspTcp, 0, szConfigFile);

        return 0;
    }

    GetPrivateProfileStringA(szAppname, szHttpIp, "127.0.0.1", szValue, sizeof(szValue), szConfigFile);
    string httpIp = szValue;
    GetPrivateProfileStringA(szAppname, szHttpPort, "80", szValue, sizeof(szValue), szConfigFile);
    int port = atoi(szValue);
    GetPrivateProfileStringA(szAppname, szHttpRequest, "/", szValue, sizeof(szValue), szConfigFile);
    string requestUrl = szValue;

    g_log.Add("http request json. ip: %s. port: %d. request: %s", httpIp.c_str(),
        port, requestUrl.c_str());

    Client cli(httpIp.c_str(), port);

    auto res = cli.Get(requestUrl.c_str());

    g_log.Add("Get finished.");

    if (!res) {
        g_log.Add("Can't get a valid response. Maybe dst http service problem.");
        return -1;
    }

    // res is valid.
    if (res->status != 200) {
        g_log.Add("Invalid response status: %d", res->status);
        return -2;
    }

    Document d;
    d.Parse(res->body.c_str());

    if (d.HasParseError()) {
        g_log.Add("json parse error: %s", res->body.c_str());
        return -3;
    }

    if (!d.HasMember("stat") || !d.HasMember("rtspurl") || !d.HasMember("use_tcp")
            || !d.HasMember("file_time_length") || !d.HasMember("dest_dir")
            || !d.HasMember("expire")) {
        g_log.Add("Incomplete json. Miss some fields.");
        return -4;
    }

    Value& vStat = d["stat"];
    Value& vRtspurl = d["rtspurl"];
    Value& vUseTcp = d["use_tcp"];
    Value& vFileLength = d["file_time_length"];
    Value& vdstDir = d["dest_dir"];
    Value& vExpire = d["expire"];

    if (!(vStat.IsString() && vRtspurl.IsString() && vUseTcp.IsString() && vFileLength.IsString() && vdstDir.IsString() && vExpire.IsString())) {
        g_log.Add("Not all json field has string value.");
        return -5;
    }

    if (strcmp(vStat.GetString(), "1") != 0) {
        g_log.Add("stat != 1. Will not record. stat=%s.", vStat.GetString());
        return 1;
    }

    rtspurl = vRtspurl.GetString();
    useTcp = atoi(vUseTcp.GetString());
    fileTimeLength = atoi(vFileLength.GetString());
    dstDir = vdstDir.GetString();
    expireMinutes = atoi(vExpire.GetString());
    
    return 0;
}
