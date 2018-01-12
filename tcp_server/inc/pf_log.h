#ifndef PF_LOG_36E4E61DEDD14EB680CC0E8D2419B9CC_H
#define PF_LOG_36E4E61DEDD14EB680CC0E8D2419B9CC_H

#include "log_define.h"

unsigned long long __stdcall LOG_Init();

void __stdcall LOG_Release(unsigned long long ullInst);

int __stdcall LOG_SetConfig(unsigned long long ullInst, pf_logger::LogConfig logConf);

int __stdcall LOG_GetConfig(unsigned long long ullInst, pf_logger::LogConfig * pLogConf);

int __stdcall LOG_Log(unsigned long long ullInst, const char * pLogContent, unsigned short, unsigned short);

#endif
