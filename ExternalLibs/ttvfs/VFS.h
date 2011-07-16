/* ttvfs -- tiny tree virtual file system

// VFS.h - all the necessary includes to get a basic VFS working
// Only include externally, not inside the library.

See VFSDefines.h for compile configration.


---------[ License ]----------
Copyright (C) 2011 False.Genesis

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*/


#ifndef TTVFS_VFS_H
#define TTVFS_VFS_H

#include <cstring>
#include <string>
#include "VFSDefines.h"
#include "VFSHelper.h"
#include "VFSFile.h"
#include "VFSDir.h"


// Checks to enforce correct including.
// At least on windows, <string> includes <cstdio>,
// but that must be included after "VFSInternal.h",
// and "VFSInternal.h" may only be used inside the library (or by extensions),
// because it redefines fseek and ftell, which would
// mess up the ABI if included elsewhere.
#ifdef VFS_INTERNAL_H
#error Oops, VFS_INTERNAL_H is defined, someone messed up and included VFSInternal.h wrongly.
#endif

#endif
