// ffmpeg_slice_segment.cpp : �������̨Ӧ�ó������ڵ㡣
//

/*
���ڴ�����ͬʱ��εķ�Ƭ. (5��û��Ӧ���˳�����)
ffmpeg -stimeout 5000000 -i rtsp://169.254.187.54/10001001 -c copy -f segment -strftime 1 -segment_atclocktime 1 -segment_time 2 -segment_format mp4 "file%Y-%m-%d_%H-%M-%S.mp4"
*/

/*
ƴ�Ӷ��mp4�ļ���1��.
ffmpeg -safe 0 -f concat -i ./file_list.txt -c copy output.mp4

����mylist.txt�ĸ�ʽΪ:
file out2019-10-30_15-43-25.mp4
file out2019-10-30_15-43-26.mp4
file out2019-10-30_15-43-28.mp4
�����ӵ�.
*/

/*
��һ��mp4�ļ��н�ȡ����, ����ӵ�2�뿪ʼ��ȡ5��:
ffmpeg -ss 00:00:02 -i origin.mp4 -t 00:00:05 -vcodec copy -acodec copy newfile.mp4
*/

/*
���˼·:
  ���߳�: ����֤ffmpeg��ʱ��������mp4�ļ����̵���������.
      �ļ���ͳһ����Ϊ"F_YYYYMMDDZHHmmSSZ.mp4".
  �����߳�A: �������еĹ̶�ʱ�䳤�ȵ�mp4�ļ�, ��ͣ������Ҫʱ�䳤�ȵ�mp4�ļ�, ��ͬ�ļ�
      ֮���ֵΪ1��. ������ʽΪseconds since epoch.
  �����߳�B: ��һ��ʱ��, ȥɾ��һЩ�ϵ������ļ�.
*/

#include "stdafx.h"
#include "MyLog.h"

#define MY_NANO_SECOND 10000000

CMyLog g_log;

const char *g_szTmpDir = "./tmp_mp4";
const char *g_szFileList = "./file_list.txt";  // �������concat������mp4�ļ���.

// �������
int g_mp4FileSeconds = 0;  // ÿ��MP4�ļ���ʱ�䳤��
int g_expireMinutes = 0;
char g_szRtspUrl[256];
char g_szDestDir[256];     // �������Ժ�, ����'/'��β��.

void ThreadAConcatFiles();
void ThreadBDeleteFiles();

// ����ҵ���, ����true. ���򷵻�false.
bool IsFileExist(const char *szFileName);

int _tmain(int argc, _TCHAR* argv[])
{
	printf("begin\n");

	char exeName[MAX_PATH], CurrentPath[MAX_PATH];
	strcpy(exeName,"ffmpeg_slice_segment.exe");
	GetModuleFileName(GetModuleHandle(exeName), CurrentPath, MAX_PATH);

	char * p = CurrentPath;
	while(strchr(p,'\\')){ p = strchr(p,'\\'); p++; }
	*p = '\0';
	SetCurrentDirectory(CurrentPath);

	g_log.InitLog("./log/slice_segment_");
	g_log.Add("--------------- Start -----------------");

	//char path[MAX_PATH];
	//GetCurrentDirectory(MAX_PATH, path);
	//g_log.Add("cwd: %s\n", path);

	// ��ȡ�����ļ�
	const char *szConfigFile = "./ffmpeg_slice_segment.ini";
	const char *szAppname = "CONFIG";
	const char *szKeyRtspurl = "rtspurl";
	const char *szKeyFileSeconds = "file_time_length";
	const char *szKeyDestDir = "dest_dir";
	const char *szKeyExpire = "expire";
	const char *szUseRtspTcp = "use_tcp";

	g_mp4FileSeconds = GetPrivateProfileIntA(szAppname, szKeyFileSeconds, 6, szConfigFile);
	g_expireMinutes = GetPrivateProfileIntA(szAppname, szKeyExpire, 12, szConfigFile);
	GetPrivateProfileStringA(szAppname, szKeyRtspurl, "", g_szRtspUrl, sizeof(g_szRtspUrl), szConfigFile);
	GetPrivateProfileStringA(szAppname, szKeyDestDir, "C:\\rukoujuchao_mp4", g_szDestDir, sizeof(g_szDestDir), szConfigFile);

	int useTcp;
	useTcp = GetPrivateProfileIntA(szAppname, szUseRtspTcp, 0, szConfigFile);
	char szOptionTcp[100];
	memset(szOptionTcp, 0, sizeof(szOptionTcp));
	if (1 == useTcp) {
		sprintf_s(szOptionTcp, "-rtsp_transport tcp");
		g_log.Add("use RTSP TCP");
	} else {
		g_log.Add("use RTSP UDP");
	}

	int dirLen = strlen(g_szDestDir);
	// ��Ŀ¼�еķ�б��, ����б��(ffmpeg�����п���б�ܱȽϺ�)
	for (int i = 0; i != dirLen; ++i) {
		if (g_szDestDir[i] == '\\') {
			g_szDestDir[i] = '/';
		}
	}
	if (dirLen <= 0) {
		g_log.Add("Output directory empty. exit");
		return 0;
	} else {
		if (g_szDestDir[dirLen - 1] != '\\' && g_szDestDir[dirLen - 1] != '/') {
			g_szDestDir[dirLen] = '/';
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

	// ����Ŀ���ļ��� (û�����, because I'm lazy)
	boost::system::error_code ec;
	boost::filesystem::create_directories(g_szDestDir, ec);

	// ����Job
	// �ο�https://devblogs.microsoft.com/oldnewthing/20131209-00/?p=2433
	HANDLE hJob = ::CreateJobObjectA(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = { 0 };
	info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(
		hJob,
		JobObjectExtendedLimitInformation,
		&info, 
		sizeof(info)
		);

	// ����¼���߳�.
	// �ο�https://docs.microsoft.com/en-us/windows/win32/procthread/creating-processes
	char szCmd[1024];
	sprintf(szCmd, 
		".\\ffmpeg %s -stimeout 5000000 -i %s -c copy "
		"-f segment -strftime 1 -segment_atclocktime 1 "
		"-segment_time 2 -segment_format mp4 \"%s\\F_%%Y%%m%%dT%%H%%M%%SZ.mp4\"",
		szOptionTcp,
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

		// ���ӽ��̼���Job.
		BOOL bAssignJob = ::AssignProcessToJobObject(hJob, pi.hProcess);
		if (!bAssignJob) {
			g_log.Add("AssignProcessToJobObject failed. Error: %d", GetLastError());
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

// ��ʱ���ӡ����־.
void LogSystemTime(const char *prefix, SYSTEMTIME *pSysTime);
// ��UTC��ʽ��SYSTEMTIME, ת��Ϊ����ʱ��.
SYSTEMTIME ConvertUTCToLocal(SYSTEMTIME *timeUTC);
// ����UTCʱ��, ��ȡseconds since epoch(1970).
int64_t GetSecondsSince1970(SYSTEMTIME *timeUTC);
// ��1970�������, ���UTC��SYSTEMTIME
SYSTEMTIME GetUTCFromSecondsSince1970(uint64_t);
// ����, ����endTime - beginTime (����).
int64_t SystemTimeDiff(SYSTEMTIME *endTime, SYSTEMTIME *beginTime);
// ����originTime + diffSeconds.
SYSTEMTIME DiffSecondsToSystemTime(SYSTEMTIME *originTime, int64_t diffSeconds);

// Format�õ��ļ���: F_****.mp4
std::string GetISOFileNameFromTime(SYSTEMTIME *sysTime);
// Format�õ��ļ��� ****.mp4, ����****Ϊ�Դ�1970�������.
std::string GetEpochSecondFileNameFromTime(SYSTEMTIME *sysTime);

// ɾ������Ŀ¼��, �����Ѿ����ٴ�����nMinutes���ӵ������ļ�.
void DeleteEarlierFilesInDir(const char *szDir, int nMinutes);

void ThreadAConcatFiles()
{
	// ���Ȼ�ȡ��һ��ƴ�ӵ��ļ���ʱ���.
	SYSTEMTIME startTime;
	::GetSystemTime(&startTime);

	// �տ�ʼ��û���ļ�����, �����һ���.
	::Sleep(3000);

	//LogSystemTime("System time", &startTime);
	//LogSystemTime("Local time ", &localTime);

	//int64_t seconds = GetSecondsSince1970(&startTime);
	
	// �Ƚ�startTime��1.
	startTime = DiffSecondsToSystemTime(&startTime, -1);
	SYSTEMTIME startTimeLocal;
	int64_t secSinceEpoch;

	// ÿ��ѭ����ʼ��ʱ��, startTime����ӦΪ��ǰ��Ҫ�����ļ��Ŀ�ʼʱ������.
	while (true) {
		// ÿ�ζ���ʱ������1��.
		startTime = DiffSecondsToSystemTime(&startTime, 1);
		startTimeLocal = ConvertUTCToLocal(&startTime);
		secSinceEpoch = GetSecondsSince1970(&startTime);

		// �ȴ�����ǰ��Ҫ���ļ��Ľ���ʱ�����
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

		//g_log.Add("time diff : %lld", SystemTimeDiff(&nowTime, &endTime));

		// ���Ȳ��ҵ�ǰʱ���֮�����Ӧ���ļ��Ƿ����.
		// ��endTime��ʼ��, ���Կ����ҵ��������һ��ʱ���.
		BOOL foundFlag = FALSE;
		const int kMoreSeconds = 6;  // �����ļ����ķ�Χ, endTime ~ endTime + kMoreSeconds
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

		// ��[startTimeLocal - 1, endTimeLocal)֮�������mp4�ļ�ƴ�ӳ�һ��.
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
			continue;
		}

		// �����е��ļ���, ��д�뵽��Ӧ���ļ���.
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

		// ��ʼ�����ӽ���, �����ļ���ƴ��.
		char szConcatFileName[256];
		sprintf(szConcatFileName, "%s%lld_.mp4", g_szDestDir, secSinceEpoch);
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

		// �����ɵ��ļ�, �����ļ���.
		BOOL bMoveSuccess;
		char szDestFileName[256];
		sprintf(szDestFileName, "%s%lld.mp4", g_szDestDir, secSinceEpoch);
		bMoveSuccess = ::MoveFileA(szConcatFileName, szDestFileName);
		if (!bMoveSuccess) {
			g_log.Add("rename file %s failed.", szDestFileName);
		}
	}
}

void ThreadBDeleteFiles()
{
	while (true) {
		DeleteEarlierFilesInDir(g_szDestDir, g_expireMinutes);
		DeleteEarlierFilesInDir(g_szTmpDir, g_expireMinutes);

		::Sleep(5 * 60 * 1000); // TODO ������Ҫ�ӳ�����ʱ����.
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

// szDir������'/'����'\'��β, Ҳ���ܲ�����������֮һ��β, ʵ����Ҫ�����������.
void DeleteEarlierFilesInDir(const char *szDir, int nMinutes)
{
	char szPath[256];
	int len = strlen(szDir);
	const char *szSuffix;
	
	if (szDir[len - 1] == '/' || szDir[len - 1] == '\\') {
		szSuffix = "*";
	} else {
		szSuffix = "/*";
	}
	sprintf(szPath, "%s%s", szDir, szSuffix);
	
	//g_log.Add("Delete files in %s", szPath);

	// ��ʼ�����ļ�.
	WIN32_FIND_DATA findFileData;
	HANDLE hFind;

	hFind = FindFirstFileA(szPath, &findFileData);
	if (INVALID_HANDLE_VALUE == hFind) {
		g_log.Add("FindFirstFileA failed. Error: %d", GetLastError());
		return;
	}

	SYSTEMTIME sysTime;
	::GetSystemTime(&sysTime);
	int secondsBound = nMinutes * 60;
	int numDeleted = 0;

	// ��szPath�ָ���������׺slash/backslash��״̬.
	szPath[strlen(szPath) - 1] = '\0';

	do {
		BOOL isFile = !(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		if (isFile) {
			FILETIME lastModifyTime = findFileData.ftLastWriteTime;
			SYSTEMTIME lastModifySysTime;
			BOOL bConvert = ::FileTimeToSystemTime(&lastModifyTime, &lastModifySysTime);
			if (!bConvert) {
				g_log.Add("DeleteEarlierFilesInDir FileTimeToSystemTime failed. %d", GetLastError());
				continue;
			}

			int diffSeconds = SystemTimeDiff(&sysTime, &lastModifySysTime);
			if (diffSeconds > secondsBound) {
				// ɾ���ļ�.
				char szFileName[256];
				sprintf(szFileName, "%s%s", szPath, findFileData.cFileName);
				//g_log.Add("Delete file : %s. expire minutes: %d", szFileName, diffSeconds / 60);
				BOOL bDelete = DeleteFileA(szFileName);
				if (!bDelete) {
					g_log.Add("Delete %s FAILED. error: %d", GetLastError());
				} else {
					++numDeleted;
				}
			}
		}
	} while (FindNextFileA(hFind, &findFileData));

	g_log.Add("Delete %d files in %s.", numDeleted, szPath);

	::FindClose(hFind);
}
