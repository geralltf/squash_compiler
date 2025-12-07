#ifndef PREFIXFLAGS_H
#define PREFIXFLAGS_H

enum PrefixFlags
{
	PF_None = 0,
	PF_Lock = 0x01,
	PF_Repe = 0x02,
	PF_Repne = 0x04,
	PF_Notrack = 0x08,
	PF_PreferVex = 0x10,
	PF_PreferEvex = 0x20,
};

#endif