// ffmpeg_slice_segment.cpp : 定义控制台应用程序的入口点。
//

/*
用于创建不同时间段的分片. (5秒没响应则退出进程)
ffmpeg -stimeout 5000000 -i rtsp://169.254.187.54/10001001 -c copy -f segment -strftime 1 -segment_atclocktime 1 -segment_time 2 -segment_format mp4 "file%Y-%m-%d_%H-%M-%S.mp4"
*/

/*
拼接多个mp4文件到1个.
ffmpeg -safe 0 -f concat -i ./file_list.txt -c copy output.mp4

其中mylist.txt的格式为:
file out2019-10-30_15-43-25.mp4
file out2019-10-30_15-43-26.mp4
file out2019-10-30_15-43-28.mp4
这样子的.
*/

/*
从一个mp4文件中截取部分, 比如从第2秒开始截取5秒:
ffmpeg -ss 00:00:02 -i origin.mp4 -t 00:00:05 -vcodec copy -acodec copy newfile.mp4
*/

/*
设计思路:
  主线程: 负责保证ffmpeg按时间间隔保存mp4文件进程的正常运行.
      文件名统一命名为"F_YYYYMMDDZHHmmSSZ.mp4".
  额外线程A: 根据已有的固定时间长度的mp4文件, 不停生成想要时间长度的mp4文件, 不同文件
      之间差值为1秒. 命名方式为seconds since epoch.
  额外线程B: 隔一段时间, 去删除一些老的数据文件.
*/

#include "stdafx.h"
#include "MyLog.h"

#define MY_NANO_SECOND 10000000

CMyLog g_log;

const char *g_szTmpDir = "./tmp_mp4";
const char *g_szFileList = "./file_list.txt";  // 存放用于concat的所有mp4文件名.

// 相关配置
int g_mp4FileSeconds = 0;  // 每个MP4文件的时间长度
int g_expireMinutes = 0;
char g_szRtspUrl[256];
char g_szDestDir[256];     // 处理完以后, 是以'\'结尾的.

void ThreadAConcatFiles();
void ThreadBDeleteFiles();

// 如果找到了, 返回true. 否则返回false.
bool IsFileExist(const char *szFileName);

int _tmain(int argc, _TCHAR* argv[])
{
	printf("begin\n");

	char exeName[MAX_PATH], CurrentPath[MAX_PATH];
	strcpy(exeName,"ffmpeg_slice_segment.exe");
	GetModuleFileName(GetModuleHandle(exeName), CurrentPath, MAX_PATH);

	//char path[MAX_PATH];
	//GetCurrentDirectory(MAX_PATH, path);
	//printf("cwd: %s\n", path);

	char * p = CurrentPath;
	while(strchr(p,'\\')){ p = strchr(p,'\\'); p++; }
	*p = '\0';
	SetCurrentDirectory(CurrentPath);

	g_log.InitLog("./log/slice_segment_");
	g_log.Add("--------------- Start -----------------");

	// 读取配置文件
	const char *szConfigFile = "./ffmpeg_slice_segment.ini";
	const char *szAppname = "CONFIG";
	const char *szKeyRtspurl = "rtspurl";
	const char *szKeyFileSeconds = "file_time_length";
	const char *szKeyDestDir = "dest_dir";
	const char *szKeyExpire = "expire";

	g_mp4FileSeconds = GetPrivateProfileIntA(szAppname, szKeyFileSeconds, 6, szConfigFile);
	g_expireMinutes = GetPrivateProfileIntA(szAppname, szKeyExpire, 12, szConfigFile);
	GetPrivateProfileStringA(szAppname, szKeyRtspurl, "", g_szRtspUrl, sizeof(g_szRtspUrl), szConfigFile);
	GetPrivateProfileStringA(szAppname, szKeyDestDir, "C:\\rukoujuchao_mp4", g_szDestDir, sizeof(g_szDestDir), szConfigFile);

	int dirLen = strlen(g_szDestDir);
	if (dirLen <= 0) {
		g_log.Add("Output directory empty. exit");
		return 0;
	} else {
		if (g_szDestDir[dirLen - 1] != '\\' || g_szDestDir[dirLen - 1] != '/') {
			g_szDestDir[dirLen] = '\\';
			g_szDestDir[dirLen + 1] = '\0';
		}
	}

	g_log.Add("seconds: %d. expires: %d. rtsp: %s. dir: %s", g_mp4FileSeconds,
		g_expireMinutes, g_szRtspUrl, g_szDestDir);

	if (strlen(g_szRtspUrl) == 0) {
		g_log.Add("Error rtsp url. Exit");
		return 0;
	}

	// create directory "tmp_mp4"
	DWORD dwAttributes = ::GetFileAttributesA(g_szTmpDir);
	if (dwAttributes != INVALID_FILE_ATTRIBUTES && 
		  (dwAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		g_log.Add("Direcotry already exist.");
	} else {
		BOOL bRet = ::CreateDirectoryA(g_szTmpDir, NULL);
		if (!bRet) {
			g_log.Add("Create directory failed. Last Error: %d", GetLastError());
		} else {
			g_log.Add("Create direcotry successfully.");
		}
	}

	// 创建目标文件夹 (没做检查, because I'm lazy)
	boost::system::error_code ec;
	boost::filesystem::create_directories(g_szDestDir, ec);

	// 创建Job
	// 参考https://devblogs.microsoft.com/oldnewthing/20131209-00/?p=2433
	HANDLE hJob = ::CreateJobObjectA(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = { 0 };
	info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(
		hJob,
		JobObjectExtendedLimitInformation,
		&info, 
		sizeof(info)
		);

	// 开启录制线程.
	// 参考https://docs.microsoft.com/en-us/windows/win32/procthread/creating-processes
	char szCmd[1024];
	sprintf(szCmd, 
		".\\ffmpeg -stimeout 5000000 -i %s -c copy -f segment -strftime 1 -segment_atclocktime 1 "
		"-segment_time 2 -segment_format mp4 \"%s\\F_%%Y%%m%%dT%%H%%M%%SZ.mp4\"",
		g_szRtspUrl,
		g_szTmpDir
		);
	g_log.Add("cmd: %s", szCmd);

	boost::thread threadA(ThreadAConcatFiles);
	boost::thread threadB(ThreadBDeleteFiles);
	while (true) {
		STARTUPINFO si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		// Start the child process. 
		if(!::CreateProcess( 
			NULL,           // No module name (use command line)
			szCmd,          // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi )           // Pointer to PROCESS_INFORMATION structure
		) 
		{
			g_log.Add("CreateProcess failed (%d).\n", GetLastError());
			Sleep(2000);
			continue;
		}

		// 将子进程加入Job.
		BOOL bAssignJob = ::AssignProcessToJobObject(hJob, pi.hProcess);
		if (!bAssignJob) {
			g_log.Add("AssignProcessToJobObject failed. Error: %d", GetLastError);
		}

		WaitForSingleObject(pi.hProcess, INFINITE);

		g_log.Add("Main subprocess ended.");

		// Close process and thread handles. 
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		Sleep(1000);
	}

	::CloseHandle(hJob);

	printf("end\n");
	getchar();

	return 0;
}

// 将时间打印到日志.
void LogSystemTime(const char *prefix, SYSTEMTIME *pSysTime);
// 将UTC格式的SYSTEMTIME, 转化为当地时间.
SYSTEMTIME ConvertUTCToLocal(SYSTEMTIME *timeUTC);
// 根据UTC时间, 获取seconds since epoch(1970).
int64_t GetSecondsSince1970(SYSTEMTIME *timeUTC);
// 从1970后的秒数, 获得UTC的SYSTEMTIME
SYSTEMTIME GetUTCFromSecondsSince1970(uint64_t);
// 作差, 返回endTime - beginTime (秒数).
int64_t SystemTimeDiff(SYSTEMTIME *endTime, SYSTEMTIME *beginTime);
// 返回originTime + diffSeconds.
SYSTEMTIME DiffSecondsToSystemTime(SYSTEMTIME *originTime, int64_t diffSeconds);

// Format得到文件名: F_****.mp4
std::string GetISOFileNameFromTime(SYSTEMTIME *sysTime);
// Format得到文件名 ****.mp4, 其中****为自从1970后的秒数.
std::string GetEpochSecondFileNameFromTime(SYSTEMTIME *sysTime);

void ThreadAConcatFiles()
{
	// 首先获取下一个拼接的文件的时间点.
	SYSTEMTIME startTime;
	::GetSystemTime(&startTime);

	// 刚开始还没有文件产生, 不如等一会儿.
	::Sleep(3000);

	//LogSystemTime("System time", &startTime);
	//LogSystemTime("Local time ", &localTime);

	//int64_t seconds = GetSecondsSince1970(&startTime);
	
	// 先将startTime减1.
	startTime = DiffSecondsToSystemTime(&startTime, -1);
	SYSTEMTIME startTimeLocal;
	int64_t secSinceEpoch;

	// 每次循环开始的时候, startTime都对应为当前需要产生文件的开始时间秒数.
	while (true) {
		// 每次都将时间增加1秒.
		startTime = DiffSecondsToSystemTime(&startTime, 1);
		startTimeLocal = ConvertUTCToLocal(&startTime);
		secSinceEpoch = GetSecondsSince1970(&startTime);

		// 等待到当前需要的文件的结束时间结束
		SYSTEMTIME endTime = DiffSecondsToSystemTime(&startTime, g_mp4FileSeconds);
		SYSTEMTIME endTimeLocal = ConvertUTCToLocal(&endTime);
		SYSTEMTIME waitEndTime = DiffSecondsToSystemTime(&endTime, 3);
		SYSTEMTIME nowTime;
		while (true) {
			::GetSystemTime(&nowTime);
			if (SystemTimeDiff(&nowTime, &endTime) >= 0)
				break;
			::Sleep(100);
		}

		g_log.Add("time diff : %lld", SystemTimeDiff(&nowTime, &endTime));

		// 首先查找当前时间点之后几秒对应的文件是否存在.
		// 从endTime开始找, 可以考虑找到更后面的一个时间点.
		BOOL foundFlag = FALSE;
		const int kMoreSeconds = 6;  // 查找文件名的范围, endTime ~ endTime + kMoreSeconds
		do {
			for (int i = 0; i != kMoreSeconds; ++i) {
				SYSTEMTIME toFindTime = DiffSecondsToSystemTime(&endTimeLocal, i);
				std::string fileName = std::string(g_szTmpDir) + "/" + 
					GetISOFileNameFromTime(&toFindTime);
				if (IsFileExist(fileName.c_str())) {
					foundFlag = TRUE;
					break;
				}
			}
			if (foundFlag) {
				break;
			}
			::Sleep(200);
			::GetSystemTime(&nowTime);
		} while (SystemTimeDiff(&nowTime, &waitEndTime) < 0);

		// 将[startTimeLocal - 1, endTimeLocal)之间的所有mp4文件拼接成一个.
		std::list<std::string> fileNames;
		SYSTEMTIME posTime = DiffSecondsToSystemTime(&startTimeLocal, -1);
		while (SystemTimeDiff(&posTime, &endTimeLocal) < 0) {
			std::string fileName = std::string(g_szTmpDir) + "/" + 
					GetISOFileNameFromTime(&posTime);
			if (IsFileExist(fileName.c_str())) {
				//g_log.Add("file exist: %s", fileName.c_str());
				fileNames.push_back(fileName);
			} else {
				//g_log.Add("file not exist: %s", fileName.c_str());
			}

			posTime = DiffSecondsToSystemTime(&posTime, 1);
		}

		if (fileNames.size() >= 150) {
			g_log.Add("Warning: file number too big: %d", fileNames.size());
		}
		if (fileNames.empty()) {
			g_log.Add("no file to create %lld.mp4", secSinceEpoch);
		}

		// 将所有的文件名, 都写入到对应的文件中.
		FILE *fp = fopen(g_szFileList, "w");
		if (fp == NULL) {
			g_log.Add("open file %s failed.", g_szFileList);
			continue;
		}
		for (auto it = fileNames.begin(); it != fileNames.end(); ++it) {
			char tmp[1000];
			sprintf(tmp, "file %s", (*it).c_str());
			int strLen = strlen(tmp);
			int writeLen = fwrite(tmp, 1, strLen, fp);
			if (writeLen != strLen) {
				g_log.Add("fwrite failed. Want write: %d. Write really: %d", strLen, writeLen);
			}
			fwrite("\n", 1, 1, fp);
		}
		fclose(fp);

		// 开始创建子进程, 进行文件的拼接.
		char szConcatFileName[256];
		sprintf(szConcatFileName, "%s/%lld.mp4", g_szTmpDir, secSinceEpoch);
		char szCmdConcat[1024];
		sprintf(szCmdConcat, 
			"./ffmpeg -safe 0 -f concat -i %s -c copy %s",
			g_szFileList,
			szConcatFileName
			);

		STARTUPINFO si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		// Start the child process. 
		if(!::CreateProcess( 
			NULL,           // No module name (use command line)
			szCmdConcat,    // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi )           // Pointer to PROCESS_INFORMATION structure
		) 
		{
			g_log.Add("CreateProcess failed in A (%d).\n", GetLastError());
			continue;
		}

		DWORD dwRet = ::WaitForSingleObject(pi.hProcess, 3000);
		if (dwRet == WAIT_TIMEOUT) {
			g_log.Add("concat doesn't finish in time. abort. %s", szConcatFileName);
			::TerminateProcess(pi.hProcess, 0);
			::CloseHandle(pi.hProcess);
			::CloseHandle(pi.hThread);
			continue;
		}

		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);

		// 将生成的文件, 拷贝到目标文件夹下.
		BOOL bCopySuccess;
		char szDestFileName[256];
		sprintf(szDestFileName, "%s%lld.mp4", g_szDestDir, secSinceEpoch);
		bCopySuccess = ::CopyFileA(szConcatFileName, szDestFileName, FALSE);
		if (!bCopySuccess) {
			g_log.Add("copy file %s failed.", szDestFileName);
		}
	}
}

void ThreadBDeleteFiles()
{
	while (true) {
		// TODO

		Sleep(1000);
	}
}

bool IsFileExist(const char *szFileName)
{
	DWORD dwAttributes;
	dwAttributes = GetFileAttributesA(szFileName);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES)
		return false;
	return true;
}

std::string GetISOFileNameFromTime(SYSTEMTIME *sysTime)
{
	char szFileName[200];
	sprintf(szFileName, "F_%04d%02d%02dT%02d%02d%02dZ.mp4",
		sysTime->wYear,
		sysTime->wMonth,
		sysTime->wDay,
		sysTime->wHour,
		sysTime->wMinute,
		sysTime->wSecond
		);
	return szFileName;
}


// -------------------- TIME RELATED -----------------------
ULARGE_INTEGER ConvertFileTimeToULarge(FILETIME *pFileTime);
FILETIME ConvertULargeToFileTime(ULARGE_INTEGER uLargeInt);

SYSTEMTIME ConvertUTCToLocal(SYSTEMTIME *timeUTC)
{
	TIME_ZONE_INFORMATION timeZoneInfo;
	DWORD dwRet = GetTimeZoneInformation(&timeZoneInfo);
	//if (dwRet == TIME_ZONE_ID_STANDARD) {
	//	g_log.Add("GetTimeZoneInformation TIME_ZONE_ID_STANDARD");
	//} else if (dwRet == TIME_ZONE_ID_DAYLIGHT) {
	//	g_log.Add("GetTimeZoneInformation TIME_ZONE_ID_DAYLIGHT");
	//} else if (dwRet == TIME_ZONE_ID_UNKNOWN) {
	//	g_log.Add("GetTimeZoneInformation TIME_ZONE_ID_UNKNOWN");
	//} else {
	//	g_log.Add("GetTimeZoneInformation UNKNOWN");
	//}

	// UTC = local + TIME_ZONE_INFORMATION.Bias
	// TODO

	SYSTEMTIME localTime;
	localTime = DiffSecondsToSystemTime(timeUTC, timeZoneInfo.Bias * (-1) * 60);

	return localTime;
}

int64_t GetSecondsSince1970(SYSTEMTIME *timeUTC)
{
	FILETIME fileTime;
	BOOL bRet = SystemTimeToFileTime(timeUTC, &fileTime);
	if (!bRet) {
		g_log.Add("GetSecondsSince1970 failed !!! GetLastError: %d", GetLastError());
		return 0;
	}

	SYSTEMTIME time1970 = {1970, 1, 4, 1, 0, 0, 0, 0};
	return SystemTimeDiff(timeUTC, &time1970);
}

SYSTEMTIME GetUTCFromSecondsSince1970(uint64_t)
{
	SYSTEMTIME systemTime;

	// TODO

	return systemTime;
}

int64_t SystemTimeDiff(SYSTEMTIME *endTime, SYSTEMTIME *beginTime)
{
	FILETIME endFileTime;
	FILETIME beginFileTime;

	// Don't want to check return value every time. (because of lazy)
	SystemTimeToFileTime(endTime, &endFileTime);
	SystemTimeToFileTime(beginTime, &beginFileTime);

	ULARGE_INTEGER endInt = ConvertFileTimeToULarge(&endFileTime);
	ULARGE_INTEGER beginInt = ConvertFileTimeToULarge(&beginFileTime);

	return ((int64_t)(endInt.QuadPart - beginInt.QuadPart) / MY_NANO_SECOND);
}

SYSTEMTIME DiffSecondsToSystemTime(SYSTEMTIME *originTime, int64_t diffSeconds)
{
	FILETIME fileTime;
	BOOL bRet = SystemTimeToFileTime(originTime, &fileTime);
	if (!bRet) {
		g_log.Add("DiffSecondsToSystemTime failed. Error: %d", GetLastError());
	}

	// 100-nanosecond based
	ULARGE_INTEGER timeInteger = ConvertFileTimeToULarge(&fileTime);
	timeInteger.QuadPart += diffSeconds * MY_NANO_SECOND;
	fileTime = ConvertULargeToFileTime(timeInteger);

	SYSTEMTIME systemTime;
	FileTimeToSystemTime(&fileTime, &systemTime);
	
	return systemTime;
}

ULARGE_INTEGER ConvertFileTimeToULarge(FILETIME *pFileTime)
{
	ULARGE_INTEGER timeInteger;
	memcpy(&(timeInteger.LowPart), &(pFileTime->dwLowDateTime), sizeof(timeInteger.LowPart));
	memcpy(&(timeInteger.HighPart), &(pFileTime->dwHighDateTime), sizeof(timeInteger.HighPart));

	return timeInteger;
}

FILETIME ConvertULargeToFileTime(ULARGE_INTEGER uLargeInt)
{
	FILETIME fileTime;
	memcpy(&(fileTime.dwLowDateTime), &(uLargeInt.LowPart), sizeof(fileTime.dwLowDateTime));
	memcpy(&(fileTime.dwHighDateTime), &(uLargeInt.HighPart), sizeof(fileTime.dwHighDateTime));

	return fileTime;
}

void LogSystemTime(const char *prefix, SYSTEMTIME *pSysTime)
{
	g_log.Add("%s : %d-%d-%d %d:%d:%d", prefix, pSysTime->wYear,
		pSysTime->wMonth, pSysTime->wDay, pSysTime->wHour, pSysTime->wMinute,
		pSysTime->wSecond);
}
