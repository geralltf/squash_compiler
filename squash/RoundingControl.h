#ifndef ROUNDINGCONTROL_H
#define ROUNDINGCONTROL_H

enum RoundingControl
{
	/// <summary>No rounding mode</summary>
	RC_None = 0,
	/// <summary>Round to nearest (even)</summary>
	RC_RoundToNearest = 1,
	/// <summary>Round down (toward -inf)</summary>
	RC_RoundDown = 2,
	/// <summary>Round up (toward +inf)</summary>
	RC_RoundUp = 3,
	/// <summary>Round toward zero (truncate)</summary>
	RC_RoundTowardZero = 4,
};

#endif