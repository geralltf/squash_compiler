#ifndef MVEXEHBIT_H
#define MVEXEHBIT_H

enum MvexEHBit
{
	/// <summary>Not hard coded to 0 or 1 so can be used for other purposes</summary>
	MEHB_None = 0,
	/// <summary>EH bit must be 0</summary>
	MEHB_EH0 = 1,
	/// <summary>EH bit must be 1</summary>
	MEHB_EH1 = 2,
};

enum MvexConvFn {
	/// <summary>No conversion function</summary>
	MCF_None = 0,
	/// <summary>Sf32(xxx)</summary>
	MCF_Sf32 = 1,
	/// <summary>Sf64(xxx)</summary>
	MCF_Sf64 = 2,
	/// <summary>Si32(xxx)</summary>
	MCF_Si32 = 3,
	/// <summary>Si64(xxx)</summary>
	MCF_Si64 = 4,
	/// <summary>Uf32(xxx)</summary>
	MCF_Uf32 = 5,
	/// <summary>Uf64(xxx)</summary>
	MCF_Uf64 = 6,
	/// <summary>Ui32(xxx)</summary>
	MCF_Ui32 = 7,
	/// <summary>Ui64(xxx)</summary>
	MCF_Ui64 = 8,
	/// <summary>Df32(xxx)</summary>
	MCF_Df32 = 9,
	/// <summary>Df64(xxx)</summary>
	MCF_Df64 = 10,
	/// <summary>Di32(xxx)</summary>
	MCF_Di32 = 11,
	/// <summary>Di64(xxx)</summary>
	MCF_Di64 = 12,
};

#endif