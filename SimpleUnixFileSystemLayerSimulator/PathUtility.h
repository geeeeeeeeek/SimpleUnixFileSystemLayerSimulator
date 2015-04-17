#include <Windows.h>
#define MAX_FILENAME_LENGTH 50
#define MAX_PATH_FILE_LENGTH (MAX_PATH + MAX_FILENAME_LENGTH)
#pragma once
class PathUtility
{
public:
	PathUtility(void);
	~PathUtility(void);
	static int getCurrentDirectory(){
	}
	
	//�����ߴ���һ���ַ���ָ�룬�ɸú�������ռ䣬��������������
	//��������Ҫ�ͷ�����ռ䣬��������й¶
	static int ConstructFullFilePath(TCHAR **szPath, const TCHAR *filename){
		
		//ȷ���ļ������Ȳ���������
		if(_tcslen(filename)>MAX_FILENAME_LENGTH)
			return  -1;

		//Ϊ·����ȡ�洢�ռ�
		*szPath = (TCHAR*)malloc(MAX_PATH_FILE_LENGTH * sizeof(TCHAR));

		if(!GetModuleFileName(NULL,*szPath,MAX_PATH_FILE_LENGTH))
		{
			return -1;
		}
		(_tcsrchr(*szPath, _T('\\')))[1] = 0;
		_tcscat(*szPath,filename);
		return 0;
	}
	static int ConstructFullFilePathA(char **szPath, const char *filename){
		if(strlen(filename)>MAX_FILENAME_LENGTH)
			return -1;
		
		*szPath = (char*)malloc(MAX_PATH_FILE_LENGTH * sizeof(char));

		if(!GetModuleFileNameA(NULL,*szPath,MAX_PATH_FILE_LENGTH))
		{
			return -1;
		}
		(strrchr(*szPath, _T('\\')))[1] = 0;
		strcat(*szPath,filename);
		return 0;
	}
};

