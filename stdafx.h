
#define PRODUCT

#include <string.h>
#include <stdio.h>
#include <cstdio>
#include <cstdlib>

   typedef unsigned char       uint8_t;
   typedef unsigned short int  uint16_t;
   typedef signed short int    int16_t;
   typedef unsigned int        uint32_t;
   typedef signed int          int32_t;
   typedef unsigned long long  uint64_t;


#define MAKELONG(a, b)      ((uint16_t)(((uint16_t)(((uint32_t)(a)) & 0xffff)) | ((uint32_t)((uint16_t)(((uint32_t)(b)) & 0xffff))) << 16))
#define MAKEWORD(a, b)      ((uint16_t)(((uint8_t)(((uint32_t)(a)) & 0xff)) | ((uint16_t)((uint8_t)(((uint32_t)(b)) & 0xff))) << 8))


typedef char TCHAR;
#define _T(x)       x
typedef char * LPTSTR;
typedef const char * LPCTSTR;
#define _tfopen     fopen
#define _tfsopen    _fsopen
#define _tcscpy     strcpy
#define _tstat      _stat
#define _tcsrchr    strrchr
#define _tcscmp     strcmp
#define _tcsicmp    strcasecmp
#define _tcslen     strlen
#define _sntprintf  snprintf


#define CALLBACK


#define ASSERT(f)          ((void)0)
#define VERIFY(f)          ((void)f)


// DebugPrint and DebugLog
void DebugPrint(LPCTSTR message);
void DebugPrintFormat(LPCTSTR pszFormat, ...);
void DebugLogClear();
void DebugLogCloseFile();
void DebugLog(LPCTSTR message);
void DebugLogFormat(LPCTSTR pszFormat, ...);


// Processor register names
const LPCTSTR REGISTER_NAME[] = { _T("R0"), _T("R1"), _T("R2"), _T("R3"), _T("R4"), _T("R5"), _T("SP"), _T("PC") };

